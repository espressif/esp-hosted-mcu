/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Network-split: host-side netif setup + DHCP/DNS status exchange. */

#ifndef EH_HOST_FEAT_NW_SPLIT_H_
#define EH_HOST_FEAT_NW_SPLIT_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_netif_types.h"         /* esp_netif_ip_info_t, esp_ip*_addr_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Init creates the STA netif (zero MAC; caller binds later) and
 * pre-registers the DhcpDnsStatus handler. Idempotent. */
int eh_host_feat_nw_split_init(void);
int eh_host_feat_nw_split_deinit(void);

/* Re-bind netif MAC; call once the app learns the CP's STA MAC. */
esp_err_t eh_host_nw_split_bind_mac(const uint8_t mac[6], uint16_t mtu);

/* CP DHCP/DNS snapshot — same shape for sync poll and async event payload.
 * v1 wire fills v4 + dns_main only; v6 fields reserved.
 * Sub-struct validity follows has_ipv4 / has_ipv6; dns_*.type==0 means no entry. */
typedef struct {
    bool                link_up;
    bool                dhcp_up;

    bool                has_ipv4;
    esp_netif_ip_info_t ipv4;

    bool                has_ipv6;
    esp_ip6_addr_t      ipv6;
    esp_ip6_addr_t      ipv6_gateway;

    esp_ip_addr_t       dns_main;
    esp_ip_addr_t       dns_backup;
} eh_host_nw_split_status_t;

esp_err_t eh_host_nw_split_get_status(eh_host_nw_split_status_t *out);

/* Push host lease to CP. iface=0 STA / 1 AP. IP/nm/gw/dns_ip dotted-quad
 * strings (NULL leaves empty). */
esp_err_t eh_host_nw_split_set_status(uint32_t iface,
                                      uint8_t link_up, uint8_t dhcp_up,
                                      const char *dhcp_ip,
                                      const char *dhcp_nm,
                                      const char *dhcp_gw,
                                      uint8_t dns_up,
                                      const char *dns_ip,
                                      uint8_t dns_type);

/* Who acquires the shared IPv4. The CP decides; the host asks at every CP
 * connection (from the netif mode, or the last eh_host_nw_split_set_ip_owner()). */
typedef enum {
    EH_HOST_NW_SPLIT_IP_OWNER_UNKNOWN = 0,
    EH_HOST_NW_SPLIT_IP_OWNER_CP      = 1,   /* the CP's DHCP client; the host gets a copy */
    EH_HOST_NW_SPLIT_IP_OWNER_HOST    = 2,   /* host DHCP or static; the CP gets a copy */
} eh_host_nw_split_ip_owner_t;

/* EH_HOST_EVENT_NW_SPLIT_IP_OWNER payload: the CP's answer to a request. */
typedef struct {
    eh_host_nw_split_ip_owner_t want;
    eh_host_nw_split_ip_owner_t owner;      /* in effect now */
    bool                        pending;    /* want applies at the next disconnect */
    esp_err_t                   rc;
} eh_host_nw_split_ip_owner_evt_t;

/* Ask the CP for an IP owner. ESP_OK: granted (*owner == want, or *pending).
 * ESP_ERR_NOT_ALLOWED: refused, the host runs with *owner. Before the CP is
 * connected the request is kept and sent at connection: ESP_ERR_NOT_FINISHED. */
esp_err_t eh_host_nw_split_set_ip_owner(eh_host_nw_split_ip_owner_t want,
                                        eh_host_nw_split_ip_owner_t *owner, bool *pending);
eh_host_nw_split_ip_owner_t eh_host_nw_split_get_ip_owner(void);

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_FEAT_NW_SPLIT_H_ */
