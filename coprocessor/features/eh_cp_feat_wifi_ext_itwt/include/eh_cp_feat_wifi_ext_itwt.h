/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EH_CP_FEAT_WIFI_EXT_ITWT_H
#define EH_CP_FEAT_WIFI_EXT_ITWT_H

#include "esp_err.h"

#ifdef CONFIG_SOC_WIFI_HE_SUPPORT
#include "esp_wifi_he_types.h"
_Static_assert(sizeof(((wifi_event_sta_itwt_suspend_t *)0)->actual_suspend_time_ms)
               / sizeof(uint32_t) == 8,
               "IDF changed actual_suspend_time_ms[]; update"
               " EH_RPC_ITWT_MAX_FLOWS and EH_HOST_ITWT_MAX_FLOWS to match");
#endif

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t eh_cp_feat_wifi_ext_itwt_init(void);
esp_err_t eh_cp_feat_wifi_ext_itwt_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* EH_CP_FEAT_WIFI_EXT_ITWT_H */
