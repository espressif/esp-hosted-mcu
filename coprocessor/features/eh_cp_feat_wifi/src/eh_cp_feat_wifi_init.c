/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* esp_wifi_init for the CP app and the host alike: one init, re-done only when the
 * config changes (the host's RPC init after a CP app init). */
#include "eh_cp_master_config.h"

#if EH_CP_FEAT_WIFI_READY
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "eh_cp_linker_tags.h"

static const char *TAG = "ehcp_wifi_init";

extern esp_err_t __real_esp_wifi_init(const wifi_init_config_t *config);

/* Compare only fields set by req_wifi_init() — decides if reinit is needed. */
static bool wifi_init_config_changed(const wifi_init_config_t *new_cfg, const wifi_init_config_t *cached_cfg)
{
	if (!new_cfg || !cached_cfg) {
		ESP_LOGI(TAG, "WiFi init config comparison: One of the configs is NULL");
		return true;
	}

	/* Compare only the fields that are explicitly set in req_wifi_init */
	bool changed = false;

	if (new_cfg->static_rx_buf_num != cached_cfg->static_rx_buf_num) {
		ESP_LOGI(TAG, "WiFi init config: static_rx_buf_num changed: %lu -> %lu",
				(unsigned long)cached_cfg->static_rx_buf_num, (unsigned long)new_cfg->static_rx_buf_num);
		changed = true;
	}

	if (new_cfg->dynamic_rx_buf_num != cached_cfg->dynamic_rx_buf_num) {
		ESP_LOGI(TAG, "WiFi init config: dynamic_rx_buf_num changed: %lu -> %lu",
				(unsigned long)cached_cfg->dynamic_rx_buf_num, (unsigned long)new_cfg->dynamic_rx_buf_num);
		changed = true;
	}

	if (new_cfg->tx_buf_type != cached_cfg->tx_buf_type) {
		ESP_LOGI(TAG, "WiFi init config: tx_buf_type changed: %lu -> %lu",
				(unsigned long)cached_cfg->tx_buf_type, (unsigned long)new_cfg->tx_buf_type);
		changed = true;
	}

	if (new_cfg->static_tx_buf_num != cached_cfg->static_tx_buf_num) {
		ESP_LOGI(TAG, "WiFi init config: static_tx_buf_num changed: %lu -> %lu",
				(unsigned long)cached_cfg->static_tx_buf_num, (unsigned long)new_cfg->static_tx_buf_num);
		changed = true;
	}

	if (new_cfg->dynamic_tx_buf_num != cached_cfg->dynamic_tx_buf_num) {
		ESP_LOGI(TAG, "WiFi init config: dynamic_tx_buf_num changed: %lu -> %lu",
				(unsigned long)cached_cfg->dynamic_tx_buf_num, (unsigned long)new_cfg->dynamic_tx_buf_num);
		changed = true;
	}

	if (new_cfg->cache_tx_buf_num != cached_cfg->cache_tx_buf_num) {
		ESP_LOGI(TAG, "WiFi init config: cache_tx_buf_num changed: %lu -> %lu",
				(unsigned long)cached_cfg->cache_tx_buf_num, (unsigned long)new_cfg->cache_tx_buf_num);
		changed = true;
	}

	if (new_cfg->csi_enable != cached_cfg->csi_enable) {
		ESP_LOGI(TAG, "WiFi init config: csi_enable changed: %lu -> %lu",
				(unsigned long)cached_cfg->csi_enable, (unsigned long)new_cfg->csi_enable);
		changed = true;
	}

	if (new_cfg->ampdu_rx_enable != cached_cfg->ampdu_rx_enable) {
		ESP_LOGI(TAG, "WiFi init config: ampdu_rx_enable changed: %lu -> %lu",
				(unsigned long)cached_cfg->ampdu_rx_enable, (unsigned long)new_cfg->ampdu_rx_enable);
		changed = true;
	}

	if (new_cfg->ampdu_tx_enable != cached_cfg->ampdu_tx_enable) {
		ESP_LOGI(TAG, "WiFi init config: ampdu_tx_enable changed: %lu -> %lu",
				(unsigned long)cached_cfg->ampdu_tx_enable, (unsigned long)new_cfg->ampdu_tx_enable);
		changed = true;
	}

	if (new_cfg->amsdu_tx_enable != cached_cfg->amsdu_tx_enable) {
		ESP_LOGI(TAG, "WiFi init config: amsdu_tx_enable changed: %lu -> %lu",
				(unsigned long)cached_cfg->amsdu_tx_enable, (unsigned long)new_cfg->amsdu_tx_enable);
		changed = true;
	}

	if (new_cfg->nvs_enable != cached_cfg->nvs_enable) {
		ESP_LOGI(TAG, "WiFi init config: nvs_enable changed: %lu -> %lu",
				(unsigned long)cached_cfg->nvs_enable, (unsigned long)new_cfg->nvs_enable);
		changed = true;
	}

	if (new_cfg->nano_enable != cached_cfg->nano_enable) {
		ESP_LOGI(TAG, "WiFi init config: nano_enable changed: %lu -> %lu",
				(unsigned long)cached_cfg->nano_enable, (unsigned long)new_cfg->nano_enable);
		changed = true;
	}

	if (new_cfg->rx_ba_win != cached_cfg->rx_ba_win) {
		ESP_LOGI(TAG, "WiFi init config: rx_ba_win changed: %lu -> %lu",
				(unsigned long)cached_cfg->rx_ba_win, (unsigned long)new_cfg->rx_ba_win);
		changed = true;
	}

	if (new_cfg->wifi_task_core_id != cached_cfg->wifi_task_core_id) {
		ESP_LOGI(TAG, "WiFi init config: wifi_task_core_id changed: %lu -> %lu",
				(unsigned long)cached_cfg->wifi_task_core_id, (unsigned long)new_cfg->wifi_task_core_id);
		changed = true;
	}

	if (new_cfg->beacon_max_len != cached_cfg->beacon_max_len) {
		ESP_LOGI(TAG, "WiFi init config: beacon_max_len changed: %lu -> %lu",
				(unsigned long)cached_cfg->beacon_max_len, (unsigned long)new_cfg->beacon_max_len);
		changed = true;
	}

	if (new_cfg->mgmt_sbuf_num != cached_cfg->mgmt_sbuf_num) {
		ESP_LOGI(TAG, "WiFi init config: mgmt_sbuf_num changed: %lu -> %lu",
				(unsigned long)cached_cfg->mgmt_sbuf_num, (unsigned long)new_cfg->mgmt_sbuf_num);
		changed = true;
	}

	if (new_cfg->feature_caps != cached_cfg->feature_caps) {
		ESP_LOGI(TAG, "WiFi init config: feature_caps changed: %lu -> %lu",
				(unsigned long)cached_cfg->feature_caps, (unsigned long)new_cfg->feature_caps);
		changed = true;
	}

	if (new_cfg->sta_disconnected_pm != cached_cfg->sta_disconnected_pm) {
		ESP_LOGI(TAG, "WiFi init config: sta_disconnected_pm changed: %lu -> %lu",
				(unsigned long)cached_cfg->sta_disconnected_pm, (unsigned long)new_cfg->sta_disconnected_pm);
		changed = true;
	}

	if (new_cfg->espnow_max_encrypt_num != cached_cfg->espnow_max_encrypt_num) {
		ESP_LOGI(TAG, "WiFi init config: espnow_max_encrypt_num changed: %lu -> %lu",
				(unsigned long)cached_cfg->espnow_max_encrypt_num, (unsigned long)new_cfg->espnow_max_encrypt_num);
		changed = true;
	}

	if (new_cfg->magic != cached_cfg->magic) {
		ESP_LOGI(TAG, "WiFi init config: magic changed: %lu -> %lu",
				(unsigned long)cached_cfg->magic, (unsigned long)new_cfg->magic);
		changed = true;
	}

#ifdef WIFI_PRIVACY_ENHANCEMENTS_ENABLED
	if (new_cfg->privacy_enhancements != cached_cfg->privacy_enhancements) {
		ESP_LOGI(TAG, "WiFi init config: privacy_enhancements changed: %d -> %d",
				cached_cfg->privacy_enhancements, new_cfg->privacy_enhancements);
		changed = true;
	}
#endif

#ifdef WIFI_RMAC_AUTO_RESET_INTERVAL
	if (new_cfg->rmac_auto_reset_int != cached_cfg->rmac_auto_reset_int) {
		ESP_LOGI(TAG, "WiFi init config: rmac_auto_reset_int changed: %u -> %u",
				cached_cfg->rmac_auto_reset_int, new_cfg->rmac_auto_reset_int);
		changed = true;
	}
#endif

#ifdef WIFI_TASK_STACK_SIZE
	if (new_cfg->wifi_task_stack_size != cached_cfg->wifi_task_stack_size) {
		ESP_LOGI(TAG, "WiFi init config: wifi_task_stack_size changed: %d -> %d",
				cached_cfg->wifi_task_stack_size, new_cfg->wifi_task_stack_size);
		changed = true;
	}
#endif

	return changed;
}

/* IDF owns the init state. Wi-Fi up with the same config is left as is; a
 * changed config stops/deinits first. The config is cached on a real init. */
static esp_err_t wifi_init_locked(const wifi_init_config_t *config)
{
	EH_CP_BSS_STORE static wifi_init_config_t cached_wifi_init_config = {0};
	EH_CP_BSS_STORE static bool has_cached_config = false;
	wifi_mode_t mode;
	esp_err_t ret;

	if (esp_wifi_get_mode(&mode) != ESP_ERR_WIFI_NOT_INIT) {
		if (!has_cached_config || !wifi_init_config_changed(config, &cached_wifi_init_config)) {
			ESP_LOGW(TAG, "WiFi already initialized with same parameters");
			return ESP_OK;
		}
		ESP_LOGI(TAG, "WiFi init config changed, reinitializing");
		esp_wifi_stop();
		ret = esp_wifi_deinit();
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "WiFi deinit before reinit failed: 0x%x", ret);
			return ret;
		}
	}

	ret = __real_esp_wifi_init(config);
	ESP_LOGI(TAG, "esp_wifi_init: 0x%x", ret);
	if (ret == ESP_OK && config) {
		memcpy(&cached_wifi_init_config, config, sizeof(wifi_init_config_t));
		has_cached_config = true;
	}
	return ret;
}

/* The CP app and the host's RPC can both init Wi-Fi at boot. */
esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t *config);
esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t *config)
{
	static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
	static StaticSemaphore_t mtx_buf;
	static SemaphoreHandle_t mtx;
	esp_err_t ret;

	taskENTER_CRITICAL(&lock);
	if (!mtx) {
		mtx = xSemaphoreCreateMutexStatic(&mtx_buf);
	}
	taskEXIT_CRITICAL(&lock);

	xSemaphoreTake(mtx, portMAX_DELAY);
	ret = wifi_init_locked(config);
	xSemaphoreGive(mtx);
	return ret;
}
#endif /* EH_CP_FEAT_WIFI_READY */
