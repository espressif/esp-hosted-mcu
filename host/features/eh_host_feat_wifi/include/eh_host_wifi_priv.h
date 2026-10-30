/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Private WiFi-feature surface — not part of the public eh_host_wifi API.
 * Applications must not call these; they are cross-feature plumbing between
 * the WiFi feature, network-split, and the wifi_remote glue. */

#ifndef EH_HOST_WIFI_PRIV_H_
#define EH_HOST_WIFI_PRIV_H_

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RX gate checked per frame by the glue. is_ap selects AP. */
bool eh_host_wifi_rx_admitted(bool is_ap);

/* Ask the CP whether it holds the station's association; a CP that came back
 * without it is reported to the station FSM (CP_UNLINKED). */
esp_err_t eh_host_wifi_check_cp_link(void);

/* The station FSM's policy connect: the RPC alone, not an app request. */
esp_err_t eh_host_wifi_policy_connect(void);

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_WIFI_PRIV_H_ */
