/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eh_cp_master_config.h"
#if EH_CP_FEAT_RPC_EXT_V2_READY
#include <inttypes.h>
#include "esp_log.h"
#include "eh_common_ctx.h"
#include "eh_cp_feat_rpc_ext_v2_priv.h"

static const char *TAG = "mcu_ctx_rpc";

/* One case per eh_ctx_type_t; a type this build lacks answers ESP_ERR_NOT_SUPPORTED. */
esp_err_t req_set_context(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespSetContext, resp_set_context,
			RpcReqSetContext, req_set_context,
			rpc__resp__set_context__init);

	(void)priv_data;
	resp_payload->type = req_payload->type;
	switch (req_payload->type) {
#if EH_CP_FEAT_NW_SPLIT_READY
	case EH_CTX_NW_SPLIT_IP_OWNER:
		resp_payload->resp = ctx_nw_split_ip_owner(&req_payload->data, &resp_payload->data);
		break;
#endif
	default:
		ESP_LOGW(TAG, "context type %" PRIu32 " not supported", req_payload->type);
		resp_payload->resp = ESP_ERR_NOT_SUPPORTED;
		break;
	}
	return ESP_OK;
}

#endif /* EH_CP_FEAT_RPC_EXT_V2_READY */
