/* SPDX-License-Identifier: Apache-2.0 */
/* ESP-IDF-shaped aliases for the RF cert surface.
 *
 * esp_phy_X on a local chip becomes esp_phy_remote_X here, the way
 * esp_wifi_X becomes esp_wifi_remote_X.  Native name: eh_host_phy_cert_X. */

#ifndef ESP_PHY_REMOTE_H_
#define ESP_PHY_REMOTE_H_

#include "eh_host_phy_cert.h"

#define esp_phy_remote_query_t           eh_host_phy_cert_query_t
#define ESP_PHY_REMOTE_QUERY_CONFIGURED  EH_HOST_PHY_CERT_QUERY_CONFIGURED
#define ESP_PHY_REMOTE_QUERY_INITED      EH_HOST_PHY_CERT_QUERY_INITED
#define ESP_PHY_REMOTE_QUERY_READY       EH_HOST_PHY_CERT_QUERY_READY

#define esp_phy_remote_11ax_tx_set       eh_host_phy_cert_11ax_tx_set
#define esp_phy_remote_ble_rx            eh_host_phy_cert_ble_rx
#define esp_phy_remote_ble_tx            eh_host_phy_cert_ble_tx
#define esp_phy_remote_bt_tx_tone        eh_host_phy_cert_bt_tx_tone
#define esp_phy_remote_cbw40m_en         eh_host_phy_cert_cbw40m_en
#define esp_phy_remote_get_rx_result     eh_host_phy_cert_get_rx_result
#define esp_phy_remote_query             eh_host_phy_cert_query
#define esp_phy_remote_rftest_deinit     eh_host_phy_cert_deinit
#define esp_phy_remote_rftest_init       eh_host_phy_cert_init
#define esp_phy_remote_test_start_stop   eh_host_phy_cert_test_start_stop
#define esp_phy_remote_tx_contin_en      eh_host_phy_cert_tx_contin_en
#define esp_phy_remote_wifi_rx           eh_host_phy_cert_wifi_rx
#define esp_phy_remote_wifi_tx           eh_host_phy_cert_wifi_tx
#define esp_phy_remote_wifi_tx_tone      eh_host_phy_cert_wifi_tx_tone

#endif /* ESP_PHY_REMOTE_H_ */
