/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EH_CP_NW_SPLIT_IP_OWNER_FSM_H
#define EH_CP_NW_SPLIT_IP_OWNER_FSM_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_netif.h"

/* owned: network split created the netif. owner: ESP_EXT_CAP_NW_SPLIT_DHCP_CP or
 * _HOST. host_allowed: the policy lets the host own it. The link state is read
 * from the driver at start. */
esp_err_t eh_cp_nw_split_ip_owner_fsm_start(esp_netif_t *netif, bool owned, uint8_t owner,
                                            bool host_allowed);
void eh_cp_nw_split_ip_owner_fsm_stop(void);

void eh_cp_nw_split_ip_owner_link(bool up);
void eh_cp_nw_split_ip_owner_resync(void);

/* nw_split: the lease status to the host, when the CP netif holds one. */
void eh_cp_nw_split_replay_status(void);

/* The host's lease (ip 0: none) for the CP netif; refused unless the host owns the IP. */
esp_err_t eh_cp_nw_split_ip_owner_host_lease(const esp_netif_ip_info_t *ip,
                                             const esp_netif_dns_info_t *dns, uint8_t dns_type);

/* Lock-free: the owner in effect now. */
uint8_t eh_cp_nw_split_ip_owner(void);

#endif /* EH_CP_NW_SPLIT_IP_OWNER_FSM_H */
