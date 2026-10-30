/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#include "eh_cp_master_config.h"
#if EH_CP_FEAT_RPC_EXT_V2_READY
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

#include "eh_cp_feat_rpc.h"
#include "eh_cp_rpc.h"
#include "eh_cp_feat_rpc_ext_v2_priv.h"
#include "eh_transport_cp.h"

#if EH_CP_FEAT_NW_SPLIT_READY
#include "eh_cp_feat_nw_split_apis.h"
#include "eh_cp_feat_nw_split_events.h"
#include "eh_common_ctx.h"
#endif
#if EH_CP_FEAT_WIFI_READY
#include "eh_cp_feat_wifi.h"
#include "eh_check.h"
#endif   

static const char *TAG = "mcu_nw_split_rpc";

#if EH_CP_FEAT_NW_SPLIT_READY
/* Get DHCP/DNS status handler */
esp_err_t req_get_dhcp_dns_status(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespGetDhcpDnsStatus, resp_get_dhcp_dns,
			RpcReqGetDhcpDnsStatus, req_get_dhcp_dns,
			rpc__resp__get_dhcp_dns_status__init);

	{
		eh_cp_feat_nw_split_status_t status = {0};
		esp_err_t ret = eh_cp_feat_nw_split_get_status(req_payload->iface, &status);

		if (ret == ESP_OK) {
			resp_payload->net_link_up = status.net_link_up;
			resp_payload->dhcp_up = status.dhcp_up;
			resp_payload->dns_up = status.dns_up;
			resp_payload->dns_type = status.dns_type;

			RPC_RESP_COPY_STR(resp_payload->dhcp_ip, status.dhcp_ip, strlen(status.dhcp_ip) + 1);
			RPC_RESP_COPY_STR(resp_payload->dhcp_nm, status.dhcp_nm, strlen(status.dhcp_nm) + 1);
			RPC_RESP_COPY_STR(resp_payload->dhcp_gw, status.dhcp_gw, strlen(status.dhcp_gw) + 1);
			RPC_RESP_COPY_STR(resp_payload->dns_ip, status.dns_ip, strlen(status.dns_ip) + 1);

			ESP_LOGI(TAG, "Fetched IP: %s, NM: %s, GW: %s, DNS IP: %s, Type: %u",
					status.dhcp_ip, status.dhcp_nm, status.dhcp_gw, status.dns_ip, status.dns_type);

			resp_payload->resp = SUCCESS;
		} else {
			ESP_LOGE(TAG, "Failed to get DHCP/DNS status from provider");
			resp_payload->resp = FAILURE;
		}
	}

	return ESP_OK;
}

esp_err_t req_set_dhcp_dns_status(Rpc *req, Rpc *resp, void *priv_data)
{
	RPC_TEMPLATE(RpcRespSetDhcpDnsStatus, resp_set_dhcp_dns,
			RpcReqSetDhcpDnsStatus, req_set_dhcp_dns,
			rpc__resp__set_dhcp_dns_status__init);

	{
		uint8_t iface = req_payload->iface;
		uint8_t dns_type = req_payload->dns_type;
		char dhcp_ip[64] = {0};
		char dhcp_nm[64] = {0};
		char dhcp_gw[64] = {0};
		char dns_ip[64] = {0};
		ESP_LOGI(TAG, "iface: %u dns_type:%u", iface, dns_type);
		RPC_REQ_COPY_BYTES(dhcp_ip, req_payload->dhcp_ip, sizeof(dhcp_ip));
		RPC_REQ_COPY_BYTES(dhcp_nm, req_payload->dhcp_nm, sizeof(dhcp_nm));
		RPC_REQ_COPY_BYTES(dhcp_gw, req_payload->dhcp_gw, sizeof(dhcp_gw));
		RPC_REQ_COPY_BYTES(dns_ip, req_payload->dns_ip, sizeof(dns_ip));
		ESP_LOGI(TAG, "Setting network config: IP=%s NM=%s GW=%s DNS=%s", dhcp_ip, dhcp_nm, dhcp_gw, dns_ip);
		esp_err_t ret = eh_cp_feat_nw_split_set_config(iface, dhcp_ip, dhcp_nm, dhcp_gw, dns_ip, dns_type);
		if (ret == ESP_OK) {
			resp_payload->resp = SUCCESS;
		} else {
			ESP_LOGE(TAG, "Failed to set network config");
			resp_payload->resp = FAILURE;
		}
	}

	return ESP_OK;
}

int32_t ctx_nw_split_ip_owner(const ProtobufCBinaryData *in, ProtobufCBinaryData *out)
{
	eh_ctx_ip_owner_req_t rq;
	eh_ctx_ip_owner_t a = {0};
	bool pending = false;

	if (!eh_ctx_unpack(&rq, sizeof(rq), sizeof(rq), in->data, in->len)) {
		return ESP_ERR_INVALID_ARG;
	}
	esp_err_t rc = eh_cp_feat_nw_split_set_ip_owner(rq.owner, &a.owner, &pending);
	a.pending = pending;
	out->data = malloc(sizeof(a));
	if (!out->data) {
		return ESP_ERR_NO_MEM;
	}
	memcpy(out->data, &a, sizeof(a));
	out->len = sizeof(a);
	return rc;
}

#endif /* EH_CP_FEAT_NW_SPLIT_READY */
#endif /* EH_CP_FEAT_RPC_EXT_V2_READY */
