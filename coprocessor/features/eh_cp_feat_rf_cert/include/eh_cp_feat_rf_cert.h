/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enter RF certification test mode.
 *
 * Powers on the Wi-Fi power domain, then calls esp_phy_rftest_config(1) and
 * esp_phy_rftest_init().
 *
 * Cert mode takes the radio.  Call this before esp_wifi_init().  The call
 * fails with ESP_ERR_INVALID_STATE if Wi-Fi is already initialised.
 *
 * There is no supported way to leave cert mode.  Restart the coprocessor to
 * go back to normal operation.
 *
 * @return ESP_OK on success, or ESP_ERR_INVALID_STATE if Wi-Fi is up.
 */
esp_err_t eh_cp_feat_rf_cert_init(void);

/**
 * @brief Power off the Wi-Fi power domain.
 *
 * This does not leave cert mode.  The PHY keeps its cert-mode state, and only
 * a coprocessor restart clears it.
 */
esp_err_t eh_cp_feat_rf_cert_deinit(void);

/**
 * @brief Report whether cert mode has been entered.
 */
bool eh_cp_feat_rf_cert_is_inited(void);

/**
 * @brief Mark the start of a TX or RX test.
 *
 * Calls esp_phy_test_start_stop(3), which the PHY library needs before every
 * TX or RX command.  The RPC handlers call this; the host does not.
 */
void eh_cp_feat_rf_cert_test_begin(void);

#ifdef __cplusplus
}
#endif
