/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_hosted.h"
#include "esp_hosted_ota.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_hosted_api_types.h"

#if CONFIG_OTA_METHOD_HTTPS
#include "ota_https.h"
#elif CONFIG_OTA_METHOD_LITTLEFS
#include "ota_littlefs.h"
#elif CONFIG_OTA_METHOD_PARTITION
#include "ota_partition.h"
#endif

static const char *TAG = "host_performs_slave_ota";

#ifdef CONFIG_OTA_VERSION_CHECK_HOST_SLAVE
/* Compare host and slave firmware versions */
static int compare_self_version_with_slave_version(uint32_t slave_version)
{
    uint32_t host_version = ESP_HOSTED_VERSION_VAL(ESP_HOSTED_VERSION_MAJOR_1,
            ESP_HOSTED_VERSION_MINOR_1,
            ESP_HOSTED_VERSION_PATCH_1);

    // mask out patch level
    // compare major.minor only
    slave_version &= 0xFFFFFF00;
    host_version &= 0xFFFFFF00;

    if (host_version == slave_version) {
        // versions match
        return 0;
    } else if (host_version > slave_version) {
        // host version > slave version
#ifndef CONFIG_ESP_HOSTED_FW_VERSION_MISMATCH_WARNING_SUPPRESS
        ESP_LOGW(TAG, "Version mismatch: Host [%u.%u.%u] > Co-proc [%u.%u.%u] ==> Upgrade co-proc to avoid RPC timeouts",
            ESP_HOSTED_VERSION_PRINTF_ARGS(host_version), ESP_HOSTED_VERSION_PRINTF_ARGS(slave_version));
#endif
        return -1;
    } else {
        // host version < slave version
#ifndef CONFIG_ESP_HOSTED_FW_VERSION_MISMATCH_WARNING_SUPPRESS
        ESP_LOGW(TAG, "Version mismatch: Host [%u.%u.%u] < Co-proc [%u.%u.%u] ==> Upgrade host to avoid compatibility issues",
            ESP_HOSTED_VERSION_PRINTF_ARGS(host_version), ESP_HOSTED_VERSION_PRINTF_ARGS(slave_version));
#endif
        return 1;
    }
}

/* Check host-slave version compatibility */
static int compare_host_slave_version(void)
{
    /* Get slave version via RPC */
    esp_hosted_coprocessor_fwver_t slave_version_struct = {0};
    esp_err_t ret = esp_hosted_get_coprocessor_fwversion(&slave_version_struct);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Could not get slave firmware version (error: %s)", esp_err_to_name(ret));
        ESP_LOGW(TAG, "Proceeding without version compatibility check");
        return ESP_OK;
    }

    /* Convert slave version to 32-bit value for comparison */
    uint32_t slave_version = ESP_HOSTED_VERSION_VAL(slave_version_struct.major1,
            slave_version_struct.minor1,
            slave_version_struct.patch1);

    /* Log versions */
    ESP_LOGI(TAG, "Host firmware version: %d.%d.%d", ESP_HOSTED_VERSION_MAJOR_1, ESP_HOSTED_VERSION_MINOR_1, ESP_HOSTED_VERSION_PATCH_1);
    ESP_LOGI(TAG, "Slave firmware version: %" PRIu32 ".%" PRIu32 ".%" PRIu32,
             slave_version_struct.major1, slave_version_struct.minor1, slave_version_struct.patch1);

    return compare_self_version_with_slave_version(slave_version);
}
#endif

void app_main(void)
{
	int ret;
    int host_slave_version_not_compatible = 1;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_hosted_init());
    ESP_ERROR_CHECK(esp_hosted_connect_to_slave());

    ESP_LOGI(TAG, "ESP-Hosted initialized successfully");

#ifdef CONFIG_OTA_VERSION_CHECK_HOST_SLAVE
    /* Check host-slave version compatibility */
    host_slave_version_not_compatible = compare_host_slave_version();
#endif

    if (!host_slave_version_not_compatible) {
        ESP_LOGW(TAG, "Slave OTA not required, so nothing to do!");
        return;
    }

    /* Perform OTA based on Kconfig selection */
#if CONFIG_OTA_METHOD_HTTPS
    ESP_LOGI(TAG, "Using HTTP OTA method");
    ret = ota_https_perform(CONFIG_OTA_SERVER_URL);
#elif CONFIG_OTA_METHOD_LITTLEFS
	uint8_t delete_post_flash = 0;
    ESP_LOGI(TAG, "Using LittleFS OTA method");
  #ifdef CONFIG_OTA_DELETE_FILE_AFTER_FLASH
	delete_post_flash = 1;
  #endif
    ret = ota_littlefs_perform(delete_post_flash);
#elif CONFIG_OTA_METHOD_PARTITION
    ESP_LOGI(TAG, "Using Partition OTA method");
    ret = ota_partition_perform(CONFIG_OTA_PARTITION_LABEL);
#else
    ESP_LOGE(TAG, "No OTA method selected!");
    return;
#endif

    if (ret == ESP_HOSTED_SLAVE_OTA_COMPLETED) {
        ESP_LOGI(TAG, "OTA completed successfully");

        /* Activate the new firmware */
        ret = esp_hosted_slave_ota_activate();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Slave will reboot with new firmware");
            ESP_LOGI(TAG, "********* Restarting host to avoid sync issues **********************");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Failed to activate OTA: %s", esp_err_to_name(ret));
        }
    } else if (ret == ESP_HOSTED_SLAVE_OTA_NOT_REQUIRED) {
        ESP_LOGI(TAG, "OTA not required");
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }
}
