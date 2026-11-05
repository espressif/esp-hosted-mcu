/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* RPC mode, wire version and endpoint names. Part of eh_cp_master_config.h: include that header, not this one. */

#ifndef EH_CP_CFG_RPC_H
#define EH_CP_CFG_RPC_H

#ifndef EH_CP_MASTER_CONFIG_H
#error "include eh_cp_master_config.h, not eh_cp_cfg_rpc.h"
#endif

/* RPC mode (Kconfig choice ESP_HOSTED_CP_RPC_MODE) */

#ifdef CONFIG_ESP_HOSTED_CP_RPC_V3
#  define EH_CP_RPC_V3              1
#else
#  define EH_CP_RPC_V3              0
#endif

#ifdef CONFIG_ESP_HOSTED_CP_RPC_V1
#  define EH_CP_RPC_V1        1
#else
#  define EH_CP_RPC_V1        0
#endif

#ifdef CONFIG_ESP_HOSTED_CP_RPC_V2
#  define EH_CP_RPC_V2          1
#else
#  define EH_CP_RPC_V2          0
#endif

/* Wire RPC version advertised in priv-handshake TLV 0x1A (EH_PRIV_RPC_VERSION).
 * Constants now live in eh_tlv_tags.h (single TLV owner) and the
 * eh_transport.h enum collision is dissolved. */
#include "eh_tlv_tags.h"
#if EH_CP_RPC_V1
#  define EH_CP_RPC_WIRE_VERSION    ESP_HOSTED_RPC_VERSION_V1
#elif EH_CP_RPC_V2
#  define EH_CP_RPC_WIRE_VERSION    ESP_HOSTED_RPC_VERSION_V2
#elif EH_CP_RPC_V3
#  define EH_CP_RPC_WIRE_VERSION    ESP_HOSTED_RPC_VERSION_V3
#else
#  define EH_CP_RPC_WIRE_VERSION    0  /* RPC disabled */
#endif

/* RPC endpoint names (wire constants) */

#ifndef RPC_EP_NAME_REQ
#  if EH_CP_RPC_V1
#    define RPC_EP_NAME_REQ          "ctrlResp"
#  elif EH_CP_RPC_V2
#    define RPC_EP_NAME_REQ          "RPCRsp"
#  else
#    define RPC_EP_NAME_REQ          "RPCReqV2"
#  endif
#endif

#ifndef RPC_EP_NAME_EVT
#  if EH_CP_RPC_V1
#    define RPC_EP_NAME_EVT          "ctrlEvnt"
#  elif EH_CP_RPC_V2
#    define RPC_EP_NAME_EVT          "RPCEvt"
#  else
#    define RPC_EP_NAME_EVT          "RPCEvtV2"
#  endif
#endif

#ifndef RPC_EP_NAME_REQ_LEGACY_FG
#define RPC_EP_NAME_REQ_LEGACY_FG    "ctrlResp"
#endif

#ifndef RPC_EP_NAME_EVT_LEGACY_FG
#define RPC_EP_NAME_EVT_LEGACY_FG    "ctrlEvnt"
#endif

#endif /* EH_CP_CFG_RPC_H */
