/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* IDF esp_bt.h BLE TX power API for a host without its own BT controller.
 * Calls run on the CP controller. The level values are those of the
 * configured CP chip (ESP_HOSTED_CP_TARGET), as IDF on that chip. */

#ifndef __ESP_BT_H__
#define __ESP_BT_H__

#include <stdint.h>
#include "esp_err.h"
#include "eh_host_port_master_config.h"
#include "eh_compat_bt_tx_power_table.h"

#ifndef EH_COMPAT_BT_PWR_TABLE
#error "ESP-Hosted: the configured CP target (ESP_HOSTED_CP_TARGET) has no BLE"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
#define EH_COMPAT_BT_PWR_ENUM_(s, x, v) ESP_PWR_LVL_##s##x = v,
    EH_COMPAT_BT_PWR_TABLE(EH_COMPAT_BT_PWR_ENUM_, EH_COMPAT_BT_PWR_ENUM_)
#undef EH_COMPAT_BT_PWR_ENUM_
#if EH_COMPAT_BT_PWR_HAS_INVALID
    ESP_PWR_LVL_INVALID = 0xFF,
#endif
} esp_power_level_t;

typedef enum {
    ESP_BLE_PWR_TYPE_CONN_HDL0 = 0,
    ESP_BLE_PWR_TYPE_CONN_HDL1 = 1,
    ESP_BLE_PWR_TYPE_CONN_HDL2 = 2,
    ESP_BLE_PWR_TYPE_CONN_HDL3 = 3,
    ESP_BLE_PWR_TYPE_CONN_HDL4 = 4,
    ESP_BLE_PWR_TYPE_CONN_HDL5 = 5,
    ESP_BLE_PWR_TYPE_CONN_HDL6 = 6,
    ESP_BLE_PWR_TYPE_CONN_HDL7 = 7,
    ESP_BLE_PWR_TYPE_CONN_HDL8 = 8,
    ESP_BLE_PWR_TYPE_ADV       = 9,
    ESP_BLE_PWR_TYPE_SCAN      = 10,
    ESP_BLE_PWR_TYPE_DEFAULT   = 11,
    ESP_BLE_PWR_TYPE_NUM       = 12,
} esp_ble_power_type_t;

esp_err_t esp_ble_tx_power_set(esp_ble_power_type_t power_type, esp_power_level_t power_level);
esp_power_level_t esp_ble_tx_power_get(esp_ble_power_type_t power_type);

#if EH_COMPAT_BT_PWR_HAS_ENHANCED
typedef enum {
    ESP_BLE_ENHANCED_PWR_TYPE_DEFAULT = 0,
    ESP_BLE_ENHANCED_PWR_TYPE_ADV,
    ESP_BLE_ENHANCED_PWR_TYPE_SCAN,
    ESP_BLE_ENHANCED_PWR_TYPE_INIT,
    ESP_BLE_ENHANCED_PWR_TYPE_CONN,
#if EH_COMPAT_BT_PWR_HAS_ISO
    ESP_BLE_ENHANCED_PWR_TYPE_BIG_CTRL,
    ESP_BLE_ENHANCED_PWR_TYPE_BIS,
    ESP_BLE_ENHANCED_PWR_TYPE_CIS,
#endif
    ESP_BLE_ENHANCED_PWR_TYPE_MAX,
} esp_ble_enhanced_power_type_t;

esp_err_t esp_ble_tx_power_set_enhanced(esp_ble_enhanced_power_type_t power_type, uint16_t handle,
                                        esp_power_level_t power_level);
esp_power_level_t esp_ble_tx_power_get_enhanced(esp_ble_enhanced_power_type_t power_type,
                                                uint16_t handle);
#endif /* EH_COMPAT_BT_PWR_HAS_ENHANCED */

#ifdef __cplusplus
}
#endif

#endif /* __ESP_BT_H__ */
