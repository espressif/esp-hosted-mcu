/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EH_CP_FEAT_WIFI_H
#define EH_CP_FEAT_WIFI_H

#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t eh_cp_feat_wifi_init(void);
esp_err_t eh_cp_feat_wifi_deinit(void);

/* Station requests and queries (eh_cp_feat_wifi_sta_fsm.c), AP query. */
esp_err_t eh_cp_feat_wifi_set_sta_config(wifi_interface_t iface, const wifi_config_t *cfg);
esp_err_t eh_cp_feat_wifi_request_start(void);
esp_err_t eh_cp_feat_wifi_request_connect(void);
esp_err_t eh_cp_feat_wifi_request_connect_with_config(const wifi_config_t *cfg);
esp_err_t eh_cp_feat_wifi_request_disconnect(void);
bool eh_cp_feat_wifi_is_station_connected(void);
bool eh_cp_feat_wifi_is_softap_started(void);
bool eh_cp_feat_wifi_is_started(void);

/* The host woke, or (re)connected to the CP, with the CP holding a link: replay it. */
void eh_cp_feat_wifi_resync_host(void);
/* Connect whenever the radio starts (network split's CP-managed station). */
void eh_cp_feat_wifi_set_auto_connect(bool on);
/* The driver's station state, independent of this feature's FSM. */
void eh_cp_feat_wifi_driver_state(bool *started, bool *linked, wifi_event_sta_connected_t *conn);
/* Before an RPC reply: the station reports its request caused go out first. */
void eh_cp_feat_wifi_reports_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* EH_CP_FEAT_WIFI_H */
