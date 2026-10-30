/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Private: the CP station FSM. Queries and requests are in eh_cp_feat_wifi.h. */

#ifndef EH_CP_FEAT_WIFI_STA_FSM_H
#define EH_CP_FEAT_WIFI_STA_FSM_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_wifi_types.h"
#include "esp_err.h"

esp_err_t eh_cp_sta_fsm_init(void);
void      eh_cp_sta_fsm_deinit(void);

/* IDF station WIFI_EVENTs (START/STOP/CONNECTED/DISCONNECTED). */
void      eh_cp_sta_fsm_wifi_event(int32_t id, void *data);

/* The soft-AP FSM (eh_cp_feat_wifi_ap_fsm.c): AP_START / AP_STOP from IDF. */
esp_err_t eh_cp_ap_fsm_init(void);
void      eh_cp_ap_fsm_deinit(void);
void      eh_cp_ap_fsm_wifi_event(int32_t id);

/* Link up: the driver delivers ifx's frames to core's RX entry; down: to nobody. */
void      eh_cp_feat_wifi_link(wifi_interface_t ifx, bool up);

#endif /* EH_CP_FEAT_WIFI_STA_FSM_H */
