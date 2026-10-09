/* SPDX-License-Identifier: Apache-2.0 */
/* RF certification test — native host API over RPC.
 *
 * Each call maps 1:1 onto an ESP-IDF esp_phy_cert_test.h API on the
 * coprocessor: esp_phy_X becomes eh_host_phy_cert_X, with that header's argument
 * types.  The return type is the one difference: an RPC can fail where a local
 * call cannot, so every call returns esp_err_t instead of void.
 *
 * esp_phy_remote.h aliases this surface under ESP-IDF-shaped names.
 *
 * For conducted RF measurement only.  Enter cert mode before the coprocessor
 * Wi-Fi driver starts, and restart the coprocessor to leave cert mode.
 */

#ifndef EH_HOST_PHY_CERT_H_
#define EH_HOST_PHY_CERT_H_

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_phy_cert_test.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What eh_host_phy_cert_query() asks the coprocessor. */
typedef enum {
    EH_HOST_PHY_CERT_QUERY_CONFIGURED = 0, /**< built with cert support */
    EH_HOST_PHY_CERT_QUERY_INITED,         /**< cert mode entered */
    EH_HOST_PHY_CERT_QUERY_READY,          /**< ready to run tests */
} eh_host_phy_cert_query_t;

int eh_host_feat_rf_cert_init(void);
int eh_host_feat_rf_cert_deinit(void);

/**
 * @brief Put the coprocessor into RF cert mode.
 *
 * Runs the coprocessor-side equivalent of the ESP-IDF cert_test app_main
 * sequence: esp_wifi_power_domain_on(), esp_phy_rftest_config(1) and
 * esp_phy_rftest_init().
 *
 * Fails with ESP_ERR_INVALID_STATE if the coprocessor has already
 * initialised Wi-Fi.  Call this before esp_hosted Wi-Fi is started.
 */
esp_err_t eh_host_phy_cert_init(void);

/**
 * @brief Stop any running test and power down the Wi-Fi power domain.
 *
 * This does not leave cert mode.  Restart the coprocessor for that.
 */
esp_err_t eh_host_phy_cert_deinit(void);

/** @brief Ask the coprocessor about its cert-mode state. */
esp_err_t eh_host_phy_cert_query(eh_host_phy_cert_query_t what);

/** @brief esp_phy_wifi_tx() on the coprocessor. */
esp_err_t eh_host_phy_cert_wifi_tx(uint32_t chan, esp_phy_wifi_rate_t rate,
                                 int8_t backoff, uint32_t length_byte,
                                 uint32_t packet_delay, uint32_t packet_num);

/** @brief esp_phy_wifi_rx() on the coprocessor. */
esp_err_t eh_host_phy_cert_wifi_rx(uint32_t chan, esp_phy_wifi_rate_t rate);

/** @brief esp_phy_wifi_tx_tone() on the coprocessor. */
esp_err_t eh_host_phy_cert_wifi_tx_tone(uint32_t start, uint32_t chan,
                                      uint32_t backoff);

/** @brief esp_phy_get_rx_result() on the coprocessor. */
esp_err_t eh_host_phy_cert_get_rx_result(esp_phy_rx_result_t *rx_result);

/** @brief esp_phy_test_start_stop() on the coprocessor. */
esp_err_t eh_host_phy_cert_test_start_stop(uint8_t value);

/** @brief esp_phy_tx_contin_en() on the coprocessor. */
esp_err_t eh_host_phy_cert_tx_contin_en(bool contin_en);

/** @brief esp_phy_cbw40m_en() on the coprocessor. */
esp_err_t eh_host_phy_cert_cbw40m_en(bool en);

/**
 * @brief esp_phy_11ax_tx_set() on the coprocessor.
 *
 * Returns ESP_ERR_NOT_SUPPORTED from a coprocessor without
 * SOC_WIFI_HE_SUPPORT.
 */
esp_err_t eh_host_phy_cert_11ax_tx_set(uint32_t he_format, uint32_t pe,
                                     uint32_t giltf_num, uint32_t ru_index);

/* ---- BLE PHY cert commands ----
 *
 * These drive the PHY directly.  They do NOT use the Bluetooth controller or
 * any host stack, so they are a different measurement from HCI Direct Test
 * Mode, which runs through the controller.
 */

/** @brief esp_phy_ble_tx() on the coprocessor. */
esp_err_t eh_host_phy_cert_ble_tx(uint32_t txpwr, uint32_t chan, uint32_t len,
                                esp_phy_ble_type_t data_type, uint32_t syncw,
                                esp_phy_ble_rate_t rate, uint32_t tx_num_in);

/** @brief esp_phy_ble_rx() on the coprocessor. */
esp_err_t eh_host_phy_cert_ble_rx(uint32_t chan, uint32_t syncw,
                                esp_phy_ble_rate_t rate);

/** @brief esp_phy_bt_tx_tone() on the coprocessor. */
esp_err_t eh_host_phy_cert_bt_tx_tone(uint32_t start, uint32_t chan,
                                    uint32_t power);

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_PHY_CERT_H_ */
