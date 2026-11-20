/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eh_host_port_master_config.h"
#include "rpc_ext_v2_decode_priv.h"

#if EH_HOST_FEAT_BT_TX_POWER_READY

int rpc_ext_v2_parse_resp_bt(const Rpc *rpc, eh_rpc_ctrl_cmd_t *c)
{
    switch (rpc->msg_id) {
    case RPC_ID__Resp_BtSetTxPower:
        if (!rpc->resp_bt_set_tx_power) return -1;
        c->resp_event_status = rpc->resp_bt_set_tx_power->resp;
        return 0;
    case RPC_ID__Resp_BtGetTxPower:
        if (!rpc->resp_bt_get_tx_power) return -1;
        c->resp_event_status = rpc->resp_bt_get_tx_power->resp;
        c->u.bt_tx_power.level = rpc->resp_bt_get_tx_power->level;
        return 0;
    default:
        return 0;
    }
}

#endif /* EH_HOST_FEAT_BT_TX_POWER_READY */
