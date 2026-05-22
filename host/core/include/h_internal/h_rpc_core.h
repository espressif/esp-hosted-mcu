/* host/core/include/h_internal/h_rpc_core.h */
#ifndef H_RPC_CORE_H
#define H_RPC_CORE_H

#include "h_types.h"
#include <stdint.h>
#include <string.h>

#ifndef H_MIN
#define H_MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* RPC request/response dispatch.
 * Serializes protobuf requests, sends via transport, waits for response. */

h_err_t h_rpc_send_request(uint8_t msg_type, const uint8_t *req, uint16_t req_len,
                           uint8_t *rsp, uint16_t *rsp_len, int32_t timeout_ms);

h_err_t h_rpc_register_handler(uint8_t msg_type,
                               h_err_t (*handler)(const uint8_t *req, uint16_t req_len,
                                                  uint8_t *rsp, uint16_t *rsp_len));

int is_event_callback_registered(int event);

/* ── Helper macros for RPC parse/compose (new framework, no old port deps) ──
 *
 * These replace the old rpc_core.h macros that depended on legacy log and
 * free.  Consumers must ensure H_LOGE is available (via h_wrapper.h).
 */

#define RPC_FAIL_ON_NULL(msGparaM)                                            \
    if (!rpc_msg->msGparaM) {                                                 \
        H_LOGE(TAG, "Failed to process rx data\n");                           \
        goto fail_parse_rpc_msg;                                              \
    }

#define RPC_FAIL_ON_NULL_PRINT(msGparaM, prinTmsG)                            \
    if (!msGparaM) {                                                          \
        H_LOGE(TAG, prinTmsG"\n");                                            \
        goto fail_parse_rpc_msg;                                              \
    }

#define RPC_REQ_COPY_BYTES(DsT, SrC, SizE) {                                  \
  if (SizE && SrC) {                                                          \
    DsT.data = SrC;                                                           \
    DsT.len = SizE;                                                           \
  }                                                                           \
}

#define RPC_REQ_COPY_STR(DsT, SrC, MaxSizE) {                                 \
  if (SrC) {                                                                  \
    RPC_REQ_COPY_BYTES(DsT, SrC, H_MIN(strlen((char*)SrC)+1, MaxSizE));       \
  }                                                                           \
}

#endif
