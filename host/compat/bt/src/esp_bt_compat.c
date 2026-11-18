/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* IDF BLE TX power API -> eh_host_bt_ble_tx_power_*: native value <-> level name. */

#include "eh_host_port_master_config.h"
#include "esp_bt.h"
#include "eh_host_bt.h"

esp_err_t esp_ble_tx_power_set(esp_ble_power_type_t power_type, esp_power_level_t power_level)
{
    return eh_host_bt_ble_tx_power_set((eh_ble_power_type_t)power_type,
                                       eh_compat_bt_pwr_to_eh((int)power_level));
}

esp_power_level_t esp_ble_tx_power_get(esp_ble_power_type_t power_type)
{
    return (esp_power_level_t)eh_compat_bt_pwr_from_eh(
               eh_host_bt_ble_tx_power_get((eh_ble_power_type_t)power_type));
}

#if EH_COMPAT_BT_PWR_HAS_ENHANCED
esp_err_t esp_ble_tx_power_set_enhanced(esp_ble_enhanced_power_type_t power_type, uint16_t handle,
                                        esp_power_level_t power_level)
{
    return eh_host_bt_ble_tx_power_set_enhanced((eh_ble_enhanced_power_type_t)power_type, handle,
                                                eh_compat_bt_pwr_to_eh((int)power_level));
}

esp_power_level_t esp_ble_tx_power_get_enhanced(esp_ble_enhanced_power_type_t power_type,
                                                uint16_t handle)
{
    return (esp_power_level_t)eh_compat_bt_pwr_from_eh(
               eh_host_bt_ble_tx_power_get_enhanced((eh_ble_enhanced_power_type_t)power_type, handle));
}
#endif
