/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_partition.h"
#include "esp_hosted_ota.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "esp_hosted.h"
#include "esp_hosted_api_types.h"

static const char* TAG = "ota_partition";

#ifndef CHUNK_SIZE
#define CHUNK_SIZE 1500
#endif

/* Function to parse ESP32 image header and get firmware info */
static esp_err_t parse_image_header(const esp_partition_t* partition, size_t* firmware_size, char* app_version_str, size_t version_str_len)
{
	esp_image_header_t image_header;
	esp_image_segment_header_t segment_header;
	esp_app_desc_t app_desc;
	esp_err_t ret;
	size_t offset = 0;
	size_t total_size = 0;

	/* Read image header */
	ret = esp_partition_read(partition, offset, &image_header, sizeof(image_header));
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to read image header: %s", esp_err_to_name(ret));
		return ret;
	}

	/* Validate magic number */
	if (image_header.magic != ESP_IMAGE_HEADER_MAGIC) {
		ESP_LOGE(TAG, "Invalid image magic: 0x%" PRIx8, image_header.magic);
		return ESP_ERR_INVALID_ARG;
	}

	ESP_LOGI(TAG, "Image header: magic=0x%" PRIx8 ", segment_count=%" PRIu8 ", hash_appended=%" PRIu8,
			image_header.magic, image_header.segment_count, image_header.hash_appended);

	/* Calculate total size by reading all segments */
	offset = sizeof(image_header);
	total_size = sizeof(image_header);

	for (int i = 0; i < image_header.segment_count; i++) {
		/* Read segment header */
		ret = esp_partition_read(partition, offset, &segment_header, sizeof(segment_header));
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to read segment %d header: %s", i, esp_err_to_name(ret));
			return ret;
		}

		ESP_LOGI(TAG, "Segment %d: data_len=%" PRIu32 ", load_addr=0x%" PRIx32, i, segment_header.data_len, segment_header.load_addr);

		/* Add segment header size + data size */
		total_size += sizeof(segment_header) + segment_header.data_len;
		offset += sizeof(segment_header) + segment_header.data_len;

		/* Read app description from the first segment */
		if (i == 0) {
			size_t app_desc_offset = sizeof(image_header) + sizeof(segment_header);
			ret = esp_partition_read(partition, app_desc_offset, &app_desc, sizeof(app_desc));
			if (ret == ESP_OK) {
				strncpy(app_version_str, app_desc.version, version_str_len - 1);
				app_version_str[version_str_len - 1] = '\0';
				ESP_LOGI(TAG, "Found app description: version='%s', project_name='%s'",
						app_desc.version, app_desc.project_name);
			} else {
				ESP_LOGW(TAG, "Failed to read app description: %s", esp_err_to_name(ret));
				strncpy(app_version_str, "unknown", version_str_len - 1);
				app_version_str[version_str_len - 1] = '\0';
			}
		}
	}

	/* Add padding to align to 16 bytes */
	size_t padding = (16 - (total_size % 16)) % 16;
	if (padding > 0) {
		ESP_LOGD(TAG, "Adding %u bytes of padding for alignment", (unsigned int)padding);
		total_size += padding;
	}

	/* Add the checksum byte (always present) */
	total_size += 1;
	ESP_LOGD(TAG, "Added 1 byte for checksum");

	/* Add SHA256 hash if appended */
	bool has_hash = (image_header.hash_appended == 1);
	if (has_hash) {
		total_size += 32;  // SHA256 hash is 32 bytes
		ESP_LOGD(TAG, "Added 32 bytes for SHA256 hash (hash_appended=1)");
	} else {
		ESP_LOGD(TAG, "No SHA256 hash appended (hash_appended=0)");
	}

	*firmware_size = total_size;
	ESP_LOGI(TAG, "Total image size: %u bytes", (unsigned int)*firmware_size);

	/* Debug: Read last 48 bytes to verify structure */
	uint8_t tail_data[48];
	size_t tail_offset = (total_size > 48) ? (total_size - 48) : 0;
	ret = esp_partition_read(partition, tail_offset, tail_data, 48);
	if (ret == ESP_OK) {
		ESP_LOGD(TAG, "Last 48 bytes of image (offset %u):", (unsigned int)tail_offset);
		ESP_LOG_BUFFER_HEX_LEVEL(TAG, tail_data, 48, ESP_LOG_DEBUG);
	}

	return ESP_OK;
}

esp_err_t ota_partition_perform(const char* partition_label)
{
	const esp_partition_t* partition;
	esp_err_t ret = ESP_OK;
	uint8_t chunk[CHUNK_SIZE];
	size_t bytes_read;
	size_t offset = 0;
	size_t firmware_size;
	char new_app_version[32];

	ESP_LOGI(TAG, "Starting Partition OTA from partition: %s", partition_label);

	/* Find the partition */
	partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, partition_label);
	if (partition == NULL) {
		ESP_LOGE(TAG, "Partition '%s' not found", partition_label);
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "Found partition: %s, size: %" PRIu32 " bytes", partition->label, partition->size);

	/* Parse image header to get firmware size and version */
	ret = parse_image_header(partition, &firmware_size, new_app_version, sizeof(new_app_version));
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to parse image header: %s", esp_err_to_name(ret));
		return ESP_HOSTED_SLAVE_OTA_FAILED;
	}

	ESP_LOGI(TAG, "Firmware verified - Size: %u bytes, Version: %s", (unsigned int)firmware_size, new_app_version);

#ifdef CONFIG_OTA_VERSION_CHECK_SLAVEFW_SLAVE
	/* Get current running slave firmware version */
	esp_hosted_coprocessor_fwver_t current_slave_version = {0};
	esp_err_t version_ret = esp_hosted_get_coprocessor_fwversion(&current_slave_version);

	if (version_ret == ESP_OK) {
		char current_version_str[32];
		snprintf(current_version_str, sizeof(current_version_str), "%" PRIu32 ".%" PRIu32 ".%" PRIu32,
				current_slave_version.major1, current_slave_version.minor1, current_slave_version.patch1);

		ESP_LOGI(TAG, "Current slave firmware version: %s", current_version_str);
		ESP_LOGI(TAG, "New slave firmware version: %s", new_app_version);

		if (strcmp(new_app_version, current_version_str) == 0) {
			ESP_LOGW(TAG, "Current slave firmware version (%s) is the same as new version (%s). Skipping OTA.",
					current_version_str, new_app_version);
			return ESP_HOSTED_SLAVE_OTA_NOT_REQUIRED;
		}

		ESP_LOGI(TAG, "Version differs - proceeding with OTA from %s to %s", current_version_str, new_app_version);
	} else {
		ESP_LOGW(TAG, "Could not get current slave firmware version (error: %s), proceeding with OTA",
				esp_err_to_name(version_ret));
	}
#else
	ESP_LOGI(TAG, "Version check disabled - proceeding with OTA (new firmware version: %s)", new_app_version);
#endif

	/* Validate firmware size */
	if (firmware_size == 0) {
		ESP_LOGE(TAG, "Firmware size is 0, cannot proceed with OTA");
		return ESP_HOSTED_SLAVE_OTA_FAILED;
	}
	if (firmware_size > partition->size) {
		ESP_LOGE(TAG, "Firmware size (%u) exceeds partition size (%" PRIu32 ")", (unsigned int)firmware_size, partition->size);
		return ESP_HOSTED_SLAVE_OTA_FAILED;
	}

	ESP_LOGI(TAG, "Proceeding with OTA - Firmware size: %u bytes", (unsigned int)firmware_size);

	/* Begin OTA */
	ret = esp_hosted_slave_ota_begin();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to begin OTA: %s", esp_err_to_name(ret));
		return ESP_HOSTED_SLAVE_OTA_FAILED;
	}

	/* Read firmware from partition in chunks - only up to actual firmware size */
	uint32_t total_bytes_sent = 0;
	uint32_t chunk_count = 0;

	while (offset < firmware_size) {
		bytes_read = (firmware_size - offset > CHUNK_SIZE) ? CHUNK_SIZE : (firmware_size - offset);

		ret = esp_partition_read(partition, offset, chunk, bytes_read);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to read partition: %s", esp_err_to_name(ret));
			esp_hosted_slave_ota_end();
			return ESP_HOSTED_SLAVE_OTA_FAILED;
		}

		ret = esp_hosted_slave_ota_write(chunk, bytes_read);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to write OTA chunk %" PRIu32 ": %s", chunk_count, esp_err_to_name(ret));
			esp_hosted_slave_ota_end();
			return ESP_HOSTED_SLAVE_OTA_FAILED;
		}

		total_bytes_sent += bytes_read;
		offset += bytes_read;
		chunk_count++;

		/* Progress indicator */
		if (chunk_count % 50 == 0) {
			ESP_LOGD(TAG, "Progress: %" PRIu32 "/%u bytes (%.1f%%)",
					total_bytes_sent, (unsigned int)firmware_size, (float)total_bytes_sent * 100 / firmware_size);
		}
	}

	ESP_LOGD(TAG, "Total chunks sent: %" PRIu32 ", Total bytes sent: %" PRIu32, chunk_count, total_bytes_sent);

	/* End OTA */
	ret = esp_hosted_slave_ota_end();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to end OTA: %s", esp_err_to_name(ret));
		return ESP_HOSTED_SLAVE_OTA_FAILED;
	}

	ESP_LOGI(TAG, "Partition OTA completed successfully - Sent %u bytes", (unsigned int)firmware_size);
	return ESP_HOSTED_SLAVE_OTA_COMPLETED;
}
