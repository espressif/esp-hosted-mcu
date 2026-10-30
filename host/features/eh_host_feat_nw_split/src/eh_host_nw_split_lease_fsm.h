/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Private: the IP lease the host and CP share, for one STA netif. */

#ifndef EH_HOST_NW_SPLIT_LEASE_FSM_H_
#define EH_HOST_NW_SPLIT_LEASE_FSM_H_

#include "esp_err.h"
#include "esp_netif.h"
#include "eh_host_feat_nw_split.h"

/* netif: NULL until the app creates it (APP_EXTERNAL). boot_owner: the CP's
 * Kconfig owner (ext caps). want: the netif mode's. cp_split: the CP runs split. */
esp_err_t eh_host_nw_split_lease_fsm_start(esp_netif_t *netif, eh_host_nw_split_ip_owner_t boot_owner,
                                          eh_host_nw_split_ip_owner_t want, bool cp_split);
/* The app's WIFI_STA_DEF appeared. */
void      eh_host_nw_split_lease_fsm_netif(esp_netif_t *netif);
/* The CP applied an owner switch on its own (a pending one, at its disconnect). */
void      eh_host_nw_split_lease_fsm_cp_owner(eh_host_nw_split_ip_owner_t owner);
void      eh_host_nw_split_lease_fsm_stop(void);

#endif /* EH_HOST_NW_SPLIT_LEASE_FSM_H_ */
