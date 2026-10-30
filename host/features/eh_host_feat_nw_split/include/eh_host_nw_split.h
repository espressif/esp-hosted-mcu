/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* RPC API wrappers for the Network-Split host-feature. Inbound
 * Event_DhcpDnsStatus dispatches to EH_HOST_EVENT_NW_SPLIT_STATUS. */

#ifndef EH_HOST_NW_SPLIT_H_
#define EH_HOST_NW_SPLIT_H_

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "eh_host_feat_nw_split.h"   /* eh_host_nw_split_status_t + payload struct */

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t eh_host_nw_split_get_status(eh_host_nw_split_status_t *out);

/* Push host-originated lease to CP. iface=0 STA / 1 SoftAP. IP/nm/gw/
 * dns_ip are dotted-quad strings (or NULL). dns_type selects main/backup. */
esp_err_t eh_host_nw_split_set_status(uint32_t iface,
                                      uint8_t link_up, uint8_t dhcp_up,
                                      const char *dhcp_ip,
                                      const char *dhcp_nm,
                                      const char *dhcp_gw,
                                      uint8_t dns_up,
                                      const char *dns_ip,
                                      uint8_t dns_type);

/* Push a lease to the CP without blocking (link_up, dhcp_up, ipv4, dns_main
 * are sent): done(rc) runs in the RPC RX task once the CP answered. */
typedef void (*eh_host_nw_split_done_cb_t)(esp_err_t rc);
esp_err_t eh_host_nw_split_set_status_async(const eh_host_nw_split_status_t *st,
                                            eh_host_nw_split_done_cb_t done);

/* Req_SetContext EH_CTX_NW_SPLIT_IP_OWNER. done(rc, owner, pending) runs in the RPC RX task. */
typedef void (*eh_host_nw_split_owner_cb_t)(esp_err_t rc, uint8_t owner, bool pending);
esp_err_t eh_host_nw_split_rpc_set_ip_owner(uint8_t want, uint8_t *owner, bool *pending);
esp_err_t eh_host_nw_split_rpc_set_ip_owner_async(uint8_t want, eh_host_nw_split_owner_cb_t done);

esp_err_t eh_host_nw_split_register_event_handlers(void);
esp_err_t eh_host_nw_split_unregister_event_handlers(void);

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_NW_SPLIT_H_ */
