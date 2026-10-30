/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* WiFi feature lifecycle; owns sub-extension lifecycle (Ent/iTWT/DPP). */

#include <stdbool.h>

#include "esp_err.h"
#include "eh_host_port_master_config.h"
#include "eh_host_feat_wifi.h"
#include "eh_host_feat_wifi_priv.h"
#if EH_HOST_AUTO_FEAT_INIT
#include "eh_host_auto_init.h"
#endif
#include "eh_host_feat_rpc_ext_v2.h"
#include "eh_host_port.h"
#include "esp_event.h"
#include "esp_log.h"
#include "eh_host_wifi.h"
#include "eh_host_sta_fsm.h"
#include "eh_host_wifi_priv.h"
#include "eh_host_event.h"

#if EH_HOST_FEAT_WIFI_EXT_ENT_READY
int eh_host_feat_wifi_ext_ent_init(void);
int eh_host_feat_wifi_ext_ent_deinit(void);
#endif
#if EH_HOST_FEAT_WIFI_EXT_ITWT_READY
int eh_host_feat_wifi_ext_itwt_init(void);
int eh_host_feat_wifi_ext_itwt_deinit(void);
#endif
#if EH_HOST_FEAT_WIFI_EXT_DPP_READY
int eh_host_feat_wifi_ext_dpp_init(void);
int eh_host_feat_wifi_ext_dpp_deinit(void);
#endif

#define WIFI_TAG "eh_wifi"

#if CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_CONNECT_ON_START
#define CONNECT_ON_START true
#else
#define CONNECT_ON_START false
#endif
#if CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_RECONNECT_ON_DISCONNECT
#define RECONNECT_ON_DROP true
#else
#define RECONNECT_ON_DROP false
#endif

/* The station FSM decided to connect (policy); the RPC runs here, off its lock. */
static void on_sta_action(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == EH_HOST_STA_ACTION_CONNECT && eh_host_sta_fsm_auto_connect_begin()) {
        esp_err_t rc = eh_host_wifi_policy_connect();

        if (rc != ESP_OK && rc != ESP_ERR_WIFI_CONN) {
            ESP_LOGE(WIFI_TAG, "policy connect failed: 0x%x; station stays disconnected", rc);
            eh_host_sta_fsm_request(EH_STA_EV_CONNECT_FAIL);
        }
    }
}

/* A CP (re)connection: does the CP still hold the station's association? */
static void on_cp_init(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    (void)eh_host_wifi_check_cp_link();     /* the station FSM decides what it means */
}

int eh_host_feat_wifi_init(void)
{
    int rc = eh_host_feat_rpc_ext_v2_register_wifi_event_handlers();
    if (rc != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "event handler register failed (%d)", rc);
        return -1;
    }
    eh_host_sta_fsm_set_connect_policy(CONNECT_ON_START, RECONNECT_ON_DROP);
    (void)esp_event_handler_register(EH_HOST_EVENT, EH_HOST_EVENT_CP_INIT, on_cp_init, NULL);
    if (CONNECT_ON_START || RECONNECT_ON_DROP) {
        (void)esp_event_handler_register(EH_HOST_STA_ACTION_EVENT, ESP_EVENT_ANY_ID,
                                         on_sta_action, NULL);
    }

    /* enterprise → iTWT → DPP (matches CP order). */
#if EH_HOST_FEAT_WIFI_EXT_ENT_READY
    eh_host_feat_wifi_ext_ent_init();
#endif
#if EH_HOST_FEAT_WIFI_EXT_ITWT_READY
    eh_host_feat_wifi_ext_itwt_init();
#endif
#if EH_HOST_FEAT_WIFI_EXT_DPP_READY
    eh_host_feat_wifi_ext_dpp_init();
#endif
    return 0;
}

int eh_host_feat_wifi_deinit(void)
{
    (void)esp_event_handler_unregister(EH_HOST_STA_ACTION_EVENT, ESP_EVENT_ANY_ID, on_sta_action);
    (void)esp_event_handler_unregister(EH_HOST_EVENT, EH_HOST_EVENT_CP_INIT, on_cp_init);
#if EH_HOST_FEAT_WIFI_EXT_DPP_READY
    eh_host_feat_wifi_ext_dpp_deinit();
#endif
#if EH_HOST_FEAT_WIFI_EXT_ITWT_READY
    eh_host_feat_wifi_ext_itwt_deinit();
#endif
#if EH_HOST_FEAT_WIFI_EXT_ENT_READY
    eh_host_feat_wifi_ext_ent_deinit();
#endif
    return eh_host_feat_rpc_ext_v2_unregister_wifi_event_handlers();
}

#if EH_HOST_AUTO_FEAT_INIT
EH_HOST_FEAT_REGISTER(eh_host_feat_wifi_init, eh_host_feat_wifi_deinit, "wifi", 150);
#endif
