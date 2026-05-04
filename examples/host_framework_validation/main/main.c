/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "nvs_flash.h"

#define VALIDATION_CYCLES  CONFIG_EXAMPLE_VALIDATION_CYCLES
#define CONNECT_TIMEOUT_MS CONFIG_EXAMPLE_CONNECT_TIMEOUT_MS
#define CYCLE_DELAY_MS     CONFIG_EXAMPLE_CYCLE_DELAY_MS

#define BIT_CP_INIT          BIT0
#define BIT_TRANSPORT_UP     BIT1
#define BIT_TRANSPORT_DOWN   BIT2
#define BIT_TRANSPORT_FAIL   BIT3

static const char *TAG = "host_fw_validate";

static EventGroupHandle_t s_hosted_events;
static esp_event_handler_instance_t s_hosted_handler;

static uint32_t s_cp_init_count;
static uint32_t s_transport_up_count;
static uint32_t s_transport_down_count;
static uint32_t s_transport_fail_count;

static esp_err_t init_default_event_loop(void)
{
	esp_err_t ret = esp_event_loop_create_default();

	if ((ret == ESP_OK) || (ret == ESP_ERR_INVALID_STATE)) {
		return ESP_OK;
	}

	return ret;
}

static esp_err_t init_nvs_flash(void)
{
	esp_err_t ret = nvs_flash_init();

	if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}

	return ret;
}

static void hosted_event_handler(void *arg, esp_event_base_t event_base,
		int32_t event_id, void *event_data)
{
	(void)arg;
	(void)event_base;

	if (event_id == ESP_HOSTED_EVENT_CP_INIT) {
		esp_hosted_event_init_t *event = (esp_hosted_event_init_t *)event_data;
		s_cp_init_count++;
		xEventGroupSetBits(s_hosted_events, BIT_CP_INIT);
		ESP_LOGI(TAG, "got CP_INIT event, reset reason=%" PRIu16, event->reason);
		return;
	}

	if (event_id == ESP_HOSTED_EVENT_TRANSPORT_UP) {
		s_transport_up_count++;
		xEventGroupSetBits(s_hosted_events, BIT_TRANSPORT_UP);
		ESP_LOGI(TAG, "got TRANSPORT_UP event");
		return;
	}

	if (event_id == ESP_HOSTED_EVENT_TRANSPORT_DOWN) {
		s_transport_down_count++;
		xEventGroupSetBits(s_hosted_events, BIT_TRANSPORT_DOWN);
		ESP_LOGI(TAG, "got TRANSPORT_DOWN event");
		return;
	}

	if (event_id == ESP_HOSTED_EVENT_TRANSPORT_FAILURE) {
		s_transport_fail_count++;
		xEventGroupSetBits(s_hosted_events, BIT_TRANSPORT_FAIL);
		ESP_LOGW(TAG, "got TRANSPORT_FAILURE event");
		return;
	}

	ESP_LOGI(TAG, "got ESP_HOSTED event id=%" PRIi32, event_id);
}

static esp_err_t register_hosted_event_handler(void)
{
	return esp_event_handler_instance_register(ESP_HOSTED_EVENT,
			ESP_EVENT_ANY_ID,
			hosted_event_handler,
			NULL,
			&s_hosted_handler);
}

static void unregister_hosted_event_handler(void)
{
	if (s_hosted_handler) {
		ESP_ERROR_CHECK(esp_event_handler_instance_unregister(ESP_HOSTED_EVENT,
				ESP_EVENT_ANY_ID,
				s_hosted_handler));
		s_hosted_handler = NULL;
	}
}

static void maybe_log_fw_version(void)
{
#if CONFIG_EXAMPLE_QUERY_FW_VERSION
	esp_hosted_coprocessor_fwver_t fwver = { 0 };
	esp_err_t ret = esp_hosted_get_coprocessor_fwversion(&fwver);

	if (ret == ESP_OK) {
		ESP_LOGI(TAG, "co-processor fw version: %" PRIu32 ".%" PRIu32 ".%" PRIu32,
				fwver.major1, fwver.minor1, fwver.patch1);
	} else {
		ESP_LOGW(TAG, "firmware-version RPC unavailable or timed out: %s", esp_err_to_name(ret));
	}
#endif
}

static esp_err_t run_validation_cycle(int cycle_index)
{
	esp_err_t ret = ESP_OK;
	EventBits_t bits;
	const EventBits_t required_bits = BIT_CP_INIT | BIT_TRANSPORT_UP;

	xEventGroupClearBits(s_hosted_events,
			BIT_CP_INIT | BIT_TRANSPORT_UP | BIT_TRANSPORT_DOWN | BIT_TRANSPORT_FAIL);

	ESP_LOGI(TAG, "==== cycle %d/%d: esp_hosted_init ====" , cycle_index, VALIDATION_CYCLES);
	ret = esp_hosted_init();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "esp_hosted_init failed: %s", esp_err_to_name(ret));
		return ret;
	}

	ESP_LOGI(TAG, "==== cycle %d/%d: connect to slave ====" , cycle_index, VALIDATION_CYCLES);
	ret = esp_hosted_connect_to_slave();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "esp_hosted_connect_to_slave failed: %s", esp_err_to_name(ret));
		goto cleanup;
	}

	bits = xEventGroupWaitBits(s_hosted_events,
			required_bits,
			pdFALSE,
			pdTRUE,
			pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));
	if ((bits & required_bits) != required_bits) {
		ESP_LOGE(TAG,
				"cycle %d/%d timed out waiting for CP_INIT+TRANSPORT_UP, bits=0x%02" PRIx32,
				cycle_index,
				VALIDATION_CYCLES,
				(uint32_t)bits);
		ret = ESP_ERR_TIMEOUT;
		goto cleanup;
	}

	ESP_LOGI(TAG, "cycle %d/%d transport lifecycle is up", cycle_index, VALIDATION_CYCLES);
	maybe_log_fw_version();

cleanup:
	ESP_LOGI(TAG, "==== cycle %d/%d: esp_hosted_deinit ====" , cycle_index, VALIDATION_CYCLES);
	if (esp_hosted_deinit() != ESP_OK) {
		ESP_LOGE(TAG, "esp_hosted_deinit failed");
		if (ret == ESP_OK) {
			ret = ESP_FAIL;
		}
	}

	if (CYCLE_DELAY_MS > 0) {
		vTaskDelay(pdMS_TO_TICKS(CYCLE_DELAY_MS));
	}

	return ret;
}

void app_main(void)
{
	esp_err_t ret;
	int cycle;

	ESP_LOGI(TAG, "ESP-Hosted host framework validation example");
	ESP_LOGI(TAG, "validation cycles=%d, connect timeout=%d ms, delay=%d ms",
			VALIDATION_CYCLES, CONNECT_TIMEOUT_MS, CYCLE_DELAY_MS);

	ret = init_nvs_flash();
	ESP_ERROR_CHECK(ret);

	ret = init_default_event_loop();
	ESP_ERROR_CHECK(ret);

	s_hosted_events = xEventGroupCreate();
	assert(s_hosted_events);
	s_hosted_handler = NULL;

	ret = register_hosted_event_handler();
	ESP_ERROR_CHECK(ret);

	for (cycle = 1; cycle <= VALIDATION_CYCLES; cycle++) {
		ret = run_validation_cycle(cycle);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "validation stopped at cycle %d/%d", cycle, VALIDATION_CYCLES);
			break;
		}
	}

	ESP_LOGI(TAG,
			"summary: cp_init=%" PRIu32 ", transport_up=%" PRIu32 ", transport_down=%" PRIu32 ", transport_failure=%" PRIu32,
			s_cp_init_count,
			s_transport_up_count,
			s_transport_down_count,
			s_transport_fail_count);

	if (ret == ESP_OK) {
		ESP_LOGI(TAG, "host framework validation passed");
	} else {
		ESP_LOGE(TAG, "host framework validation failed: %s", esp_err_to_name(ret));
	}

	unregister_hosted_event_handler();
}