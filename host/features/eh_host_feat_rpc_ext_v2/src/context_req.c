/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "rpc_ext_v2_pack_priv.h"

static int compose_req_set_context(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                   alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqSetContext, req_set_context,
                  rpc__req__set_context__init);
    p->type      = c->u.ctx.type;
    p->data.data = (uint8_t *)c->u.ctx.data;
    p->data.len  = c->u.ctx.len;
    return 0;
}

compose_fn rpc_ext_v2_pick_req_context(int32_t msg_id)
{
    return msg_id == RPC_ID__Req_SetContext ? compose_req_set_context : NULL;
}
