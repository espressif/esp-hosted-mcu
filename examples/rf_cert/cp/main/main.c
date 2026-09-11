/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP-Hosted Coprocessor — RF Certification Test Example
 *
 * This side is only a server. It answers the RF cert RPCs and runs
 * nothing by itself: the host decides when to enter cert mode and which
 * test to start. Which commands exist follows the chip — Wi-Fi commands
 * need SOC_WIFI_SUPPORTED, BLE commands need SOC_BT_SUPPORTED, and the
 * 802.11ax command needs SOC_WIFI_HE_SUPPORT.
 *
 * Cert mode is NOT entered at boot. The host sends Req_FeatureControl
 * with Feature_Command_Init first (phy_cert_init on the host console).
 * Restart this co-processor to leave cert mode.
 *
 * For conducted RF measurement only. Do not ship this firmware.
 */

#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

static const char *TAG = "rf_cert_cp";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGW(TAG, "RF cert test co-processor ready — waiting for the host");
    ESP_LOGW(TAG, "conducted measurement only; do not ship this firmware");
}
