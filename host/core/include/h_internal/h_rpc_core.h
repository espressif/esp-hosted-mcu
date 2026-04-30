/* host/core/include/h_internal/h_rpc_core.h */
#ifndef H_RPC_CORE_H
#define H_RPC_CORE_H

#include "h_types.h"
#include <stdint.h>

/* RPC request/response dispatch.
 * Serializes protobuf requests, sends via transport, waits for response. */

h_err_t h_rpc_send_request(uint8_t msg_type, const uint8_t *req, uint16_t req_len,
                           uint8_t *rsp, uint16_t *rsp_len, int32_t timeout_ms);

h_err_t h_rpc_register_handler(uint8_t msg_type,
                               h_err_t (*handler)(const uint8_t *req, uint16_t req_len,
                                                  uint8_t *rsp, uint16_t *rsp_len));

#endif
