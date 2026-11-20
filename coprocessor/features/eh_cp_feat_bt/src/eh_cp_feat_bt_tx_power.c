/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* esp_ble_tx_power_set/get[_enhanced] with the level as an eh_power_level_t
 * name; mapped here to this chip's esp_power_level_t. */

#include "eh_cp_master_config.h"

#if EH_CP_FEAT_BT_TX_POWER_READY

#include "esp_bt.h"
#include "eh_cp_feat_bt_core.h"
#include "eh_cp_bt_tx_power_map.h"

/* IDF does not check the controller state; a host call can come at any time. */
static bool controller_up(void)
{
	return esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE;
}

esp_err_t eh_cp_bt_tx_power_set(esp_ble_power_type_t power_type, eh_power_level_t power_level)
{
	esp_power_level_t native = {0};

	if (!controller_up()) {
		return ESP_ERR_INVALID_STATE;
	}
	if (!eh_cp_bt_pwr_sym_to_native((uint8_t)power_level, &native)) {
		return ESP_ERR_NOT_SUPPORTED;
	}
	return esp_ble_tx_power_set(power_type, native);
}

eh_power_level_t eh_cp_bt_tx_power_get(esp_ble_power_type_t power_type)
{
	if (!controller_up()) {
		return EH_PWR_LVL_INVALID;
	}
	esp_power_level_t native = esp_ble_tx_power_get(power_type);
	uint8_t sym = eh_cp_bt_pwr_native_to_sym((int)native);

	return (eh_power_level_t)sym;
}

#if EH_CP_BT_PWR_HAS_ENHANCED
esp_err_t eh_cp_bt_tx_power_set_enhanced(esp_ble_enhanced_power_type_t power_type, uint16_t handle,
		eh_power_level_t power_level)
{
	esp_power_level_t native = {0};

	if (!controller_up()) {
		return ESP_ERR_INVALID_STATE;
	}
	if (!eh_cp_bt_pwr_sym_to_native((uint8_t)power_level, &native)) {
		return ESP_ERR_NOT_SUPPORTED;
	}
	return esp_ble_tx_power_set_enhanced(power_type, handle, native);
}

eh_power_level_t eh_cp_bt_tx_power_get_enhanced(esp_ble_enhanced_power_type_t power_type, uint16_t handle)
{
	if (!controller_up()) {
		return EH_PWR_LVL_INVALID;
	}
	esp_power_level_t native = esp_ble_tx_power_get_enhanced(power_type, handle);
	uint8_t sym = eh_cp_bt_pwr_native_to_sym((int)native);

	return (eh_power_level_t)sym;
}
#endif

#endif /* EH_CP_FEAT_BT_TX_POWER_READY */
