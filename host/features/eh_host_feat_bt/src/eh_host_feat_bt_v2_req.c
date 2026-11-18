/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eh_host_port_master_config.h"
#include "rpc_ext_v2_pack_priv.h"

#if EH_HOST_FEAT_BT_TX_POWER_READY

static int compose_req_bt_set_tx_power(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                       alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqBtSetTxPower, req_bt_set_tx_power,
                  rpc__req__bt_set_tx_power__init);
    p->enhanced = c->u.bt_tx_power.enhanced != 0;
    p->type     = c->u.bt_tx_power.type;
    p->handle   = c->u.bt_tx_power.handle;
    p->level    = c->u.bt_tx_power.level;
    return 0;
}

static int compose_req_bt_get_tx_power(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                       alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqBtGetTxPower, req_bt_get_tx_power,
                  rpc__req__bt_get_tx_power__init);
    p->enhanced = c->u.bt_tx_power.enhanced != 0;
    p->type     = c->u.bt_tx_power.type;
    p->handle   = c->u.bt_tx_power.handle;
    return 0;
}

compose_fn rpc_ext_v2_pick_req_bt(int32_t msg_id)
{
    switch (msg_id) {
    case RPC_ID__Req_BtSetTxPower: return compose_req_bt_set_tx_power;
    case RPC_ID__Req_BtGetTxPower: return compose_req_bt_get_tx_power;
    default:                       return NULL;
    }
}

#endif /* EH_HOST_FEAT_BT_TX_POWER_READY */
