/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* BLE TX power on the CP controller. The level travels as its name
 * (eh_bt_tx_power.h); the CP maps it to its own esp_power_level_t. */

#include "eh_host_port_master_config.h"
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "eh_host_feat_rpc_ext_v2.h"
#include "eh_host_feat_rpc_ext_v2_types.h"
#include "eh_host_feat_rpc.h"
#include "gen_v2.h"

#include "eh_host_bt.h"

#if EH_HOST_FEAT_BT_TX_POWER_READY

static bool is_level_name(uint32_t level)
{
    switch (level) {
#define EH_HOST_BT_PWR_CASE_(s, x) case EH_PWR_LVL_##s##x:
    EH_PWR_LVL_LIST(EH_HOST_BT_PWR_CASE_)
#undef EH_HOST_BT_PWR_CASE_
        return true;
    default:
        return false;
    }
}

/* Returns the CP status; *level gets the CP's level on a get. */
static esp_err_t tx_power_rpc(int32_t msg_id, bool enhanced, uint32_t type,
                              uint16_t handle, uint32_t *level)
{
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) {
        return ESP_ERR_NO_MEM;
    }
    req->u.bt_tx_power.enhanced = enhanced;
    req->u.bt_tx_power.type     = type;
    req->u.bt_tx_power.handle   = handle;
    req->u.bt_tx_power.level    = *level;

    eh_rpc_ctrl_cmd_t *r = NULL;
    if (eh_host_feat_rpc_request_sync(msg_id, req, (void **)&r) != 0) {
        return ESP_FAIL;
    }
    esp_err_t status = (esp_err_t)r->resp_event_status;
    *level = r->u.bt_tx_power.level;
    eh_rpc_ctrl_cmd_free(r);
    return status;
}

static esp_err_t set_level(bool enhanced, uint32_t type, uint16_t handle,
                           eh_power_level_t power_level)
{
    uint32_t level = (uint32_t)power_level;
    if (!is_level_name(level)) {
        return ESP_ERR_INVALID_ARG;
    }
    return tx_power_rpc(RPC_ID__Req_BtSetTxPower, enhanced, type, handle, &level);
}

static eh_power_level_t get_level(bool enhanced, uint32_t type, uint16_t handle)
{
    uint32_t level = EH_PWR_LVL_INVALID;
    /* A level newer than this host is still a valid name: pass it through. */
    if (tx_power_rpc(RPC_ID__Req_BtGetTxPower, enhanced, type, handle, &level) != ESP_OK ||
        level > EH_PWR_LVL_INVALID) {
        return EH_PWR_LVL_INVALID;
    }
    return (eh_power_level_t)level;
}

esp_err_t eh_host_bt_ble_tx_power_set(eh_ble_power_type_t power_type,
                                      eh_power_level_t power_level)
{
    return set_level(false, (uint32_t)power_type, 0, power_level);
}

eh_power_level_t eh_host_bt_ble_tx_power_get(eh_ble_power_type_t power_type)
{
    return get_level(false, (uint32_t)power_type, 0);
}

esp_err_t eh_host_bt_ble_tx_power_set_enhanced(eh_ble_enhanced_power_type_t power_type,
                                               uint16_t handle,
                                               eh_power_level_t power_level)
{
    return set_level(true, (uint32_t)power_type, handle, power_level);
}

eh_power_level_t eh_host_bt_ble_tx_power_get_enhanced(eh_ble_enhanced_power_type_t power_type,
                                                      uint16_t handle)
{
    return get_level(true, (uint32_t)power_type, handle);
}

#endif /* EH_HOST_FEAT_BT_TX_POWER_READY */
