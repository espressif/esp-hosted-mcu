/* SPDX-License-Identifier: Apache-2.0 */
/* BT host-feature RPC API: CP controller lifecycle via Req_FeatureControl. */

#ifndef EH_HOST_BT_H_
#define EH_HOST_BT_H_

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "eh_host_port_master_config.h"
#if EH_HOST_FEAT_BT_TX_POWER_READY
#include "eh_bt_tx_power.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t eh_host_bt_controller_init(void);
esp_err_t eh_host_bt_controller_deinit(bool release_memory);
esp_err_t eh_host_bt_controller_enable(void);
esp_err_t eh_host_bt_controller_disable(void);

#if EH_HOST_FEAT_BT_TX_POWER_READY
/* Same values as IDF esp_ble_power_type_t on every chip. */
typedef enum {
    EH_BLE_PWR_TYPE_CONN_HDL0 = 0,
    EH_BLE_PWR_TYPE_CONN_HDL1 = 1,
    EH_BLE_PWR_TYPE_CONN_HDL2 = 2,
    EH_BLE_PWR_TYPE_CONN_HDL3 = 3,
    EH_BLE_PWR_TYPE_CONN_HDL4 = 4,
    EH_BLE_PWR_TYPE_CONN_HDL5 = 5,
    EH_BLE_PWR_TYPE_CONN_HDL6 = 6,
    EH_BLE_PWR_TYPE_CONN_HDL7 = 7,
    EH_BLE_PWR_TYPE_CONN_HDL8 = 8,
    EH_BLE_PWR_TYPE_ADV       = 9,
    EH_BLE_PWR_TYPE_SCAN      = 10,
    EH_BLE_PWR_TYPE_DEFAULT   = 11,
    EH_BLE_PWR_TYPE_NUM       = 12,
} eh_ble_power_type_t;

/* Same values as IDF esp_ble_enhanced_power_type_t. BIG_CTRL/BIS/CIS exist on
 * H4/S31 (IDF >= 6.1.1); other CPs return ESP_ERR_NOT_SUPPORTED for them. */
typedef enum {
    EH_BLE_ENHANCED_PWR_TYPE_DEFAULT  = 0,
    EH_BLE_ENHANCED_PWR_TYPE_ADV      = 1,
    EH_BLE_ENHANCED_PWR_TYPE_SCAN     = 2,
    EH_BLE_ENHANCED_PWR_TYPE_INIT     = 3,
    EH_BLE_ENHANCED_PWR_TYPE_CONN     = 4,
    EH_BLE_ENHANCED_PWR_TYPE_BIG_CTRL = 5,
    EH_BLE_ENHANCED_PWR_TYPE_BIS      = 6,
    EH_BLE_ENHANCED_PWR_TYPE_CIS      = 7,
} eh_ble_enhanced_power_type_t;

/* esp_ble_tx_power_set/get[_enhanced] on the CP controller. power_level is a
 * named level (EH_PWR_LVL_P9 = +9 dBm), the same on every CP.
 * Set: ESP_ERR_INVALID_ARG for a value that is not a level name, or a legacy
 *      power_type >= EH_BLE_PWR_TYPE_NUM;
 *      ESP_ERR_NOT_SUPPORTED if the CP chip lacks the level or the API (ESP32
 *      has no _enhanced), or the CP is built without this feature;
 *      ESP_ERR_INVALID_STATE before eh_host_bt_controller_init();
 *      ESP_FAIL after the RPC timeout from CP firmware older than these RPCs.
 * Get: EH_PWR_LVL_INVALID on any error, as IDF. */
esp_err_t        eh_host_bt_ble_tx_power_set(eh_ble_power_type_t power_type,
                                             eh_power_level_t power_level);
eh_power_level_t eh_host_bt_ble_tx_power_get(eh_ble_power_type_t power_type);
esp_err_t        eh_host_bt_ble_tx_power_set_enhanced(eh_ble_enhanced_power_type_t power_type,
                                                      uint16_t handle,
                                                      eh_power_level_t power_level);
eh_power_level_t eh_host_bt_ble_tx_power_get_enhanced(eh_ble_enhanced_power_type_t power_type,
                                                      uint16_t handle);
#endif /* EH_HOST_FEAT_BT_TX_POWER_READY */

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_BT_H_ */
