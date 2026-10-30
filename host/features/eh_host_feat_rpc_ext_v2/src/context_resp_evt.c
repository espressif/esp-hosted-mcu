/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>

#include "rpc_ext_v2_decode_priv.h"

/* Resp_SetContext and Event_Context: a longer data than the host knows is cut
 * to EH_RPC_CTX_DATA_MAX; the type's reader keeps only its known fields. */
int rpc_ext_v2_parse_context(const Rpc *rpc, eh_rpc_ctrl_cmd_t *c)
{
    int32_t resp;
    uint32_t type;
    const ProtobufCBinaryData *d;

    if (rpc->msg_id == RPC_ID__Resp_SetContext) {
        if (!rpc->resp_set_context) return -1;
        resp = rpc->resp_set_context->resp;
        type = rpc->resp_set_context->type;
        d    = &rpc->resp_set_context->data;
    } else {
        if (!rpc->event_context) return -1;
        resp = rpc->event_context->resp;
        type = rpc->event_context->type;
        d    = &rpc->event_context->data;
    }
    c->resp_event_status = resp;
    c->u.ctx.type = type;
    c->u.ctx.len  = (uint16_t)(d->len < EH_RPC_CTX_DATA_MAX ? d->len : EH_RPC_CTX_DATA_MAX);
    if (c->u.ctx.len) {
        memcpy(c->u.ctx.data, d->data, c->u.ctx.len);
    }
    return 0;
}
