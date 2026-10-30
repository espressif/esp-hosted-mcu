/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Private: the station as the host app sees it, in ESP-IDF's event order.
 * Fed by the Wi-Fi feature (requests) and the RPC event handlers (CP reports). */

#ifndef EH_HOST_STA_FSM_H_
#define EH_HOST_STA_FSM_H_

#include <stdbool.h>
#include <stddef.h>

#include "esp_event.h"
#include "eh_fsm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EH_STA_EV_INIT_REQ,         /* the app's esp_wifi_* call, once the CP accepted it */
    EH_STA_EV_DEINIT_REQ,
    EH_STA_EV_START_REQ,
    EH_STA_EV_STOP_REQ,
    EH_STA_EV_RADIO_STARTED,    /* CP reports */
    EH_STA_EV_RADIO_STOPPED,
    EH_STA_EV_ASSOCIATED,       /* wifi_event_sta_connected_t */
    EH_STA_EV_DISASSOCIATED,    /* wifi_event_sta_disconnected_t */
    EH_STA_EV_LEASE_READY,      /* network split: CP link up with an address */
    EH_STA_EV_DISCONNECT_REQ,   /* the app's esp_wifi_disconnect, before the RPC */
    EH_STA_EV_HOSTED_DOWN,      /* hosted deinit: the link goes with it */
    EH_STA_EV_INIT_CLAIM,       /* the app's esp_wifi_init, before the RPC */
    EH_STA_EV_INIT_FAIL,        /* ...the CP refused it */
    EH_STA_EV_DISCONNECT_FAIL,  /* the CP refused the disconnect */
    EH_STA_EV_CP_UNLINKED,      /* the CP (re)connected without an association (it rebooted) */
    EH_STA_EV_CONNECT_FAIL,     /* the CP refused the app's or the policy's connect */
    EH_STA_EV_CONNECT_REQ,      /* esp_wifi_connect, before the RPC: the app wants a link */
    EH_STA_EV_AUTO_CONNECT_REQ, /* the policy's connect, before the RPC */
    EH_STA_EV_STA_ADDED,        /* the app's mode gained a station while its Wi-Fi runs */
    EH_STA_EV_COUNT
} eh_host_sta_ev_t;

typedef enum {
    EH_STA_ST_NOT_INITED,           /* app has not called esp_wifi_init */
    EH_STA_ST_INITING,          /* esp_wifi_init in flight */
    EH_STA_ST_INITED,            /* app inited, not started: nothing reaches it */
    EH_STA_ST_STARTED_NO_STA,         /* app started without a station; a mode change adds it */
    EH_STA_ST_STARTED_CP_OFF,        /* app started; the CP stopped its radio */
    EH_STA_ST_STARTED,             /* app started; radio on, not associated, no attempt of its own */
    EH_STA_ST_CONNECTING,       /* the app's attempt: its outcome is the app's */
    EH_STA_ST_AUTO_CONNECTING,  /* the policy's attempt, on the app's behalf */
    EH_STA_ST_CONNECTED,        /* app started; associated */
    EH_STA_ST_RECONNECTING,     /* associated; the app connected again: the CP replays or switches */
    EH_STA_ST_DISCONNECTING,          /* associated; the app disconnected, the CP's report pending */
    EH_STA_ST_DISCONNECTED,             /* app started; the app disconnected: no reconnect policy */
    EH_STA_ST_STOPPING,         /* app stopped; waiting for the CP's radio stop */
    EH_STA_ST_RESTARTING,       /* app started again while STOPPING; the CP stop+start reports pending */
    EH_STA_ST_RESTARTING_CONNECTING, /* RESTARTING, and the app connected */
    EH_STA_ST_COUNT
} eh_host_sta_st_t;

int  eh_host_sta_fsm_init(void);
void eh_host_sta_fsm_deinit(void);

/* *_REQ, from the caller's task. */
void eh_host_sta_fsm_request(eh_host_sta_ev_t ev);
/* A mode change removing the station, before its RPC (its CP reports can land
 * before the answer); give_sta_back undoes it if the RPC fails. */
bool eh_host_sta_fsm_take_sta(void);
void eh_host_sta_fsm_give_sta_back(void);
/* CP reports, from the RX task in arrival order (before the reply that follows them). */
void eh_host_sta_fsm_report(eh_host_sta_ev_t ev, const void *data, size_t size);

/* The app's Wi-Fi mode, once the CP accepted it: which interfaces its start and stop move. */
void eh_host_sta_fsm_set_mode(bool sta, bool ap);

/* Connect policy (Kconfig): connect when the app's station starts; reconnect when
 * the link drops without the app asking. The FSM decides; the connect itself runs
 * on the event loop (EH_HOST_STA_ACTION_EVENT). */
void eh_host_sta_fsm_set_connect_policy(bool on_start, bool on_drop);
ESP_EVENT_DECLARE_BASE(EH_HOST_STA_ACTION_EVENT);
enum { EH_HOST_STA_ACTION_CONNECT };

/* Lock-free reads of the single-writer state (datapath safe). */
extern eh_fsm_t g_eh_host_sta_fsm;

static inline bool eh_host_sta_fsm_inited(void)
{
    return g_eh_host_sta_fsm.def && g_eh_host_sta_fsm.state != EH_STA_ST_NOT_INITED &&
           g_eh_host_sta_fsm.state != EH_STA_ST_INITING;
}

/* esp_wifi_init: true when this caller owns the init (it was NOT_INITED); the RPC
 * result then follows as INIT_REQ or INIT_FAIL. */
bool eh_host_sta_fsm_claim_init(void);

/* The soft-AP as the app sees it: AP_START / AP_STOP once each, AP_STOP on hosted
 * deinit; the AP RX is admitted while it is on. */
typedef enum {
    EH_AP_EV_STARTED, EH_AP_EV_STOPPED,     /* CP reports */
    EH_AP_EV_HOSTED_DOWN,
    EH_AP_EV_APP_START, EH_AP_EV_APP_STOP,  /* the app's esp_wifi_start / stop / deinit */
    EH_AP_EV_COUNT
} eh_host_ap_ev_t;
void eh_host_ap_fsm_report(eh_host_ap_ev_t ev);
bool eh_host_ap_fsm_on(void);

/* ESP_OK once the app started its station; else the rc IDF gives that app (not inited /
 * not started), even while the CP radio runs for CP code. */
esp_err_t eh_host_sta_fsm_app_rc(void);

/* The app's connect, after eh_host_sta_fsm_app_rc: ESP_ERR_WIFI_CONN while its own
 * attempt runs, as IDF; else the attempt is the app's from now. */
esp_err_t eh_host_sta_fsm_connect_begin(void);

/* A queued policy connect still wanted (the app started, has no attempt, has not left):
 * the attempt is the policy's from now. */
bool eh_host_sta_fsm_auto_connect_begin(void);

static inline bool eh_host_sta_fsm_connected(void)
{
    return g_eh_host_sta_fsm.def && (g_eh_host_sta_fsm.state == EH_STA_ST_CONNECTED ||
                                     g_eh_host_sta_fsm.state == EH_STA_ST_RECONNECTING ||
                                     g_eh_host_sta_fsm.state == EH_STA_ST_DISCONNECTING);
}

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_STA_FSM_H_ */
