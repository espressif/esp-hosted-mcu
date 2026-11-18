/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eh_cp_master_config.h"
#if EH_CP_FEAT_RPC_EXT_V2_READY
#if EH_CP_FEAT_BT_READY

#include "esp_log.h"
#include "eh_cp_feat_rpc.h"
#include "eh_cp_feat_rpc_ext_v2_priv.h"
#include "eh_cp_feat_bt_core.h"

static const char *TAG = "ehcp_rpc_bt";

esp_err_t req_feature_control_bt(RpcReqFeatureControl *req_payload,
                                 RpcRespFeatureControl *resp_payload)
{
	switch (req_payload->command) {
	case RPC_FEATURE_COMMAND__Feature_Command_BT_Init:
		RPC_RET_FAIL_IF(eh_cp_bt_init());
		break;
	case RPC_FEATURE_COMMAND__Feature_Command_BT_Deinit: {
		bool mem_release =
			(req_payload->option == RPC_FEATURE_OPTION__Feature_Option_BT_Deinit_Release_Memory);
		RPC_RET_FAIL_IF(eh_cp_bt_deinit(mem_release));
		break;
	}
	case RPC_FEATURE_COMMAND__Feature_Command_BT_Enable:
		RPC_RET_FAIL_IF(eh_cp_bt_enable());
		break;
	case RPC_FEATURE_COMMAND__Feature_Command_BT_Disable:
		RPC_RET_FAIL_IF(eh_cp_bt_disable());
		break;
	default:
		ESP_LOGE(TAG, "error: invalid Bluetooth Feature Control");
		resp_payload->resp = ESP_ERR_INVALID_ARG;
		break;
	}
	return ESP_OK;
}

#if EH_CP_FEAT_BT_TX_POWER_READY
/* Wire values are unchecked integers: range-check before the IDF enum casts.
 * A legacy type >= NUM is invalid on every chip; an enhanced type >= this chip's
 * MAX may exist on another chip (ISO types on H4/S31), so it is not supported. */
static esp_err_t tx_power_args_check(bool enhanced, uint32_t type, uint32_t handle)
{
	if (!enhanced) {
		return type < ESP_BLE_PWR_TYPE_NUM ? ESP_OK : ESP_ERR_INVALID_ARG;
	}
#if EH_CP_BT_PWR_HAS_ENHANCED
	if (type >= ESP_BLE_ENHANCED_PWR_TYPE_MAX) {
		return ESP_ERR_NOT_SUPPORTED;
	}
	return handle <= UINT16_MAX ? ESP_OK : ESP_ERR_INVALID_ARG;
#else
	(void)handle;
	return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t req_bt_set_tx_power(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespBtSetTxPower, resp_bt_set_tx_power,
			RpcReqBtSetTxPower, req_bt_set_tx_power,
			rpc__resp__bt_set_tx_power__init);

	const RpcReqBtSetTxPower *r = req_payload;
	esp_err_t ret = tx_power_args_check(r->enhanced, r->type, r->handle);
	if (ret == ESP_OK && r->level > UINT8_MAX) {
		ret = ESP_ERR_INVALID_ARG;
	}
	if (ret == ESP_OK && !r->enhanced) {
		ret = eh_cp_bt_tx_power_set((esp_ble_power_type_t)r->type, (eh_power_level_t)r->level);
	}
#if EH_CP_BT_PWR_HAS_ENHANCED
	else if (ret == ESP_OK) {
		ret = eh_cp_bt_tx_power_set_enhanced((esp_ble_enhanced_power_type_t)r->type,
				(uint16_t)r->handle, (eh_power_level_t)r->level);
	}
#endif
	resp_payload->resp = ret;
	return ESP_OK;
}

esp_err_t req_bt_get_tx_power(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespBtGetTxPower, resp_bt_get_tx_power,
			RpcReqBtGetTxPower, req_bt_get_tx_power,
			rpc__resp__bt_get_tx_power__init);

	const RpcReqBtGetTxPower *r = req_payload;
	resp_payload->level = EH_PWR_LVL_INVALID;
	resp_payload->resp = tx_power_args_check(r->enhanced, r->type, r->handle);
	if (resp_payload->resp == ESP_OK && !r->enhanced) {
		resp_payload->level = eh_cp_bt_tx_power_get((esp_ble_power_type_t)r->type);
	}
#if EH_CP_BT_PWR_HAS_ENHANCED
	else if (resp_payload->resp == ESP_OK) {
		resp_payload->level = eh_cp_bt_tx_power_get_enhanced(
				(esp_ble_enhanced_power_type_t)r->type, (uint16_t)r->handle);
	}
#endif
	return ESP_OK;
}
#endif /* EH_CP_FEAT_BT_TX_POWER_READY */

#endif /* EH_CP_FEAT_BT_READY */
#endif /* EH_CP_FEAT_RPC_EXT_V2_READY */
