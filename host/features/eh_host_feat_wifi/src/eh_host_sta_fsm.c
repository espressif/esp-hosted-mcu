/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* The station as the host app sees it, in ESP-IDF's event order. App requests and
 * CP reports share one mutex; on_transit posts STA_START/STOP/CONNECTED/DISCONNECTED
 * on the edges of the radio-on and associated state sets. */
#include <stdbool.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi_types.h"
#include "esp_wifi.h"
#include "eh_fsm.h"
#include "eh_host_port_sync.h"
#include "eh_host_sta_fsm.h"

#define TAG "eh_sta_fsm"

ESP_EVENT_DEFINE_BASE(EH_HOST_STA_ACTION_EVENT);

eh_fsm_t g_eh_host_sta_fsm;

static struct {
    wifi_event_sta_connected_t     conn;        /* the association the app was told about */
    wifi_event_sta_disconnected_t  disc;        /* what the next STA_DISCONNECTED carries */
    bool                           sta_mode;    /* the app's mode includes a station */
    bool                           ap_mode;     /* the app's mode includes a soft-AP */
    eh_host_port_mutex_t          *mtx;
} s_sta;

static struct {
    bool on_start, on_drop;
} s_policy;                         /* set once from Kconfig; survives deinit */

/* The CP's station as the CP reported it, whatever the app did: read when the
 * app's station begins, to adopt a link the CP holds. */
enum { CP_RADIO_OFF, CP_RADIO_ON, CP_CONNECTED, CP_ST_COUNT };
static eh_fsm_t s_cp;

static void post_wifi(int32_t id, const void *data, size_t size)
{
    esp_err_t rc = esp_event_post(WIFI_EVENT, id, data, size, 0);

    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "WIFI_EVENT %d lost: 0x%x", (int)id, rc);
    }
}

static void set_disc_reason(uint8_t reason)
{
    memset(&s_sta.disc, 0, sizeof(s_sta.disc));
    memcpy(s_sta.disc.ssid, s_sta.conn.ssid, sizeof(s_sta.disc.ssid));
    s_sta.disc.ssid_len = s_sta.conn.ssid_len;
    memcpy(s_sta.disc.bssid, s_sta.conn.bssid, sizeof(s_sta.disc.bssid));
    s_sta.disc.reason = reason;
}

/* ── app-visible edges ────────────────────────────────────────────────────── */
static const bool s_radio_on[EH_STA_ST_COUNT] = {
    [EH_STA_ST_STARTED] = true, [EH_STA_ST_CONNECTING] = true, [EH_STA_ST_AUTO_CONNECTING] = true,
    [EH_STA_ST_CONNECTED] = true, [EH_STA_ST_RECONNECTING] = true, [EH_STA_ST_DISCONNECTING] = true,
    [EH_STA_ST_DISCONNECTED] = true, [EH_STA_ST_RESTARTING] = true, [EH_STA_ST_RESTARTING_CONNECTING] = true,
};

static const bool s_assoc[EH_STA_ST_COUNT] = {
    [EH_STA_ST_CONNECTED] = true, [EH_STA_ST_RECONNECTING] = true, [EH_STA_ST_DISCONNECTING] = true,
};

/* DISCONNECTED before STOP, CONNECTED after START, as IDF orders them. */
static void edges(eh_fsm_t *m, int from, int to)
{
    (void)m;
    if (s_assoc[from] && !s_assoc[to]) {
        post_wifi(WIFI_EVENT_STA_DISCONNECTED, &s_sta.disc, sizeof(s_sta.disc));
    }
    if (s_radio_on[from] != s_radio_on[to]) {
        post_wifi(s_radio_on[to] ? WIFI_EVENT_STA_START : WIFI_EVENT_STA_STOP, NULL, 0);
    }
    if (!s_assoc[from] && s_assoc[to]) {
        post_wifi(WIFI_EVENT_STA_CONNECTED, &s_sta.conn, sizeof(s_sta.conn));
    }
}

/* No station in the app's mode (a mode change took it away): no policy connect. */
static void kick_connect(void)
{
    if (!s_sta.sta_mode) {
        return;
    }
    if (esp_event_post(EH_HOST_STA_ACTION_EVENT, EH_HOST_STA_ACTION_CONNECT, NULL, 0, 0) != ESP_OK) {
        ESP_LOGE(TAG, "connect action lost");
    }
}

/* ── handlers ─────────────────────────────────────────────────────────────── */
#define GO(st) static int go_##st(eh_fsm_t *m, const void *d) \
    { (void)m; (void)d; return EH_STA_ST_##st; }

GO(NOT_INITED) GO(INITING) GO(INITED) GO(STARTED_CP_OFF)
GO(STARTED) GO(CONNECTING) GO(AUTO_CONNECTING) GO(CONNECTED) GO(RECONNECTING) GO(DISCONNECTING)
GO(DISCONNECTED) GO(STOPPING) GO(RESTARTING) GO(RESTARTING_CONNECTING)

static int init_req(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    s_sta.sta_mode = true;          /* esp_wifi_init: station mode by default */
    s_sta.ap_mode = false;
    return EH_STA_ST_INITED;
}

/* As in IDF: DISCONNECTED (if connected) and STOP now; the CP's own reports of
 * that stop are the tail absorbed in STOPPING. */
static int stop_req(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    return s_sta.sta_mode ? EH_STA_ST_STOPPING : EH_STA_ST_INITED;
}

static int stop_req_associated(eh_fsm_t *m, const void *d)
{
    set_disc_reason(WIFI_REASON_ASSOC_LEAVE);
    return stop_req(m, d);
}

/* The CP stopped its radio by itself (mode change, CP re-init). */
static int radio_stopped_associated(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    set_disc_reason(WIFI_REASON_ASSOC_LEAVE);
    return EH_STA_ST_STARTED_CP_OFF;
}

/* The app's station is up: connect if the policy says so. */
static int radio_up(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    if (s_policy.on_start) {
        kick_connect();
    }
    return EH_STA_ST_STARTED;
}

/* IDF posts STA_START before esp_wifi_start returns: the CP's report of it is the
 * tail. Without a station the start waits for a mode change to add one. */
static int start_req(eh_fsm_t *m, const void *d)
{
    return s_sta.sta_mode ? radio_up(m, d) : EH_STA_ST_STARTED_NO_STA;
}

/* The app's station starts (its start, or a mode change adding it): a link the CP
 * already holds is the app's at once, as on a wake. */
static int sta_begins(eh_fsm_t *m, const void *d)
{
    if (!s_sta.sta_mode) {
        return EH_STA_ST_STARTED_NO_STA;
    }
    if (s_cp.state == CP_CONNECTED) {
        return EH_STA_ST_CONNECTED;     /* s_sta.conn holds the CP's last association */
    }
    return radio_up(m, d);
}

/* The app stops before it started: the CP's radio, if CP code runs it, stops with it. */
static int stop_ready(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    return s_cp.state == CP_RADIO_OFF ? EH_FSM_STAY : EH_STA_ST_STOPPING;
}

/* The app has no station in its mode: CP station reports stay in the CP copy. */
static int radio_back(eh_fsm_t *m, const void *d)
{
    return s_sta.sta_mode ? radio_up(m, d) : EH_FSM_STAY;
}

static int assoc_back(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    return s_sta.sta_mode ? EH_STA_ST_CONNECTED : EH_FSM_STAY;
}

/* Started again before the CP reported the stop: that stop and this start are the
 * tail; the policy acts once the stop is in (RESTARTING, RADIO_STOPPED). */
static int restart_req(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    return EH_STA_ST_RESTARTING;
}

/* The app's or the policy's attempt failed: IDF posts STA_DISCONNECTED for each one. */
static int failed_attempt(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    post_wifi(WIFI_EVENT_STA_DISCONNECTED, &s_sta.disc, sizeof(s_sta.disc));
    if (s_policy.on_drop) {
        kick_connect();
    }
    return EH_STA_ST_STARTED;
}

/* The CP rebooted under the attempt: it ends as failed. */
static int attempt_unlinked(eh_fsm_t *m, const void *d)
{
    set_disc_reason(WIFI_REASON_ASSOC_LEAVE);
    return failed_attempt(m, d);
}

/* The app's disconnect ends the attempt: STA_DISCONNECTED before it returns, as IDF;
 * the CP's own report of it is absorbed in DISCONNECTED. */
static int leave_attempt(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    set_disc_reason(WIFI_REASON_ASSOC_LEAVE);
    post_wifi(WIFI_EVENT_STA_DISCONNECTED, &s_sta.disc, sizeof(s_sta.disc));
    return EH_STA_ST_DISCONNECTED;
}

static int leave_restart_attempt(eh_fsm_t *m, const void *d)
{
    (void)leave_attempt(m, d);
    return EH_STA_ST_RESTARTING;
}

/* The link dropped without the app asking. */
static int dropped(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    if (s_policy.on_drop) {
        kick_connect();
    }
    return EH_STA_ST_STARTED;
}

/* d: the previous association. Same AP: a replay. Another AP: a roam. */
static int reassociated(eh_fsm_t *m, const void *d)
{
    static const uint8_t none[6];
    const wifi_event_sta_connected_t *prev = d;

    (void)m;
    if (!prev || memcmp(prev->bssid, s_sta.conn.bssid, sizeof(prev->bssid)) == 0 ||
        memcmp(prev->bssid, none, sizeof(none)) == 0) {
        return EH_FSM_STAY;
    }
    set_disc_reason(WIFI_REASON_ASSOC_LEAVE);
    memcpy(s_sta.disc.bssid, prev->bssid, sizeof(s_sta.disc.bssid));
    post_wifi(WIFI_EVENT_STA_DISCONNECTED, &s_sta.disc, sizeof(s_sta.disc));
    post_wifi(WIFI_EVENT_STA_CONNECTED, &s_sta.conn, sizeof(s_sta.conn));
    return EH_FSM_STAY;
}

/* The CP's answer to the app's connect while associated: a replay (same config, no
 * event, as IDF) or a roam to another AP. */
static int reconnected(eh_fsm_t *m, const void *d)
{
    (void)reassociated(m, d);
    return EH_STA_ST_CONNECTED;
}

/* The CP came back without the association (it rebooted under a running host). */
static int cp_unlinked(eh_fsm_t *m, const void *d)
{
    set_disc_reason(WIFI_REASON_ASSOC_LEAVE);
    return dropped(m, d);
}

/* The CP refused the connect: no attempt runs. No retry: nothing new would make it pass. */
static int connect_failed(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    return EH_STA_ST_STARTED;
}

static int leaving_unlinked(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    set_disc_reason(WIFI_REASON_ASSOC_LEAVE);
    return EH_STA_ST_DISCONNECTED;          /* the app asked to leave: no reconnect */
}

static int hosted_down_associated(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    set_disc_reason(WIFI_REASON_ASSOC_LEAVE);
    return EH_STA_ST_NOT_INITED;
}

/* ── the table ────────────────────────────────────────────────────────────── */
#define I   eh_fsm_ignore

_Static_assert(EH_STA_EV_COUNT == 19, "one column per event");

/* INIT_CLAIM / INIT_FAIL act in NOT_INITED / INITING only, DISCONNECT_FAIL in DISCONNECTING / DISCONNECTED.
 * CONNECT_FAIL acts where an attempt runs: elsewhere the app moved on first.
 * A failed attempt (DISASSOCIATED while unassociated) reaches the app only in its own
 * attempt (*CONNECTING): one it did not make (CP app, CP auto-connect) stays hidden.
 * INIT_REQ in NOT_INITED: the app's esp_wifi_init claimed before this FSM ran. */
#define E(ev) [EH_STA_EV_##ev]

static const eh_fsm_fn_t s_table[EH_STA_ST_COUNT][EH_STA_EV_COUNT] = {
[EH_STA_ST_NOT_INITED] = {
    E(INIT_REQ) = init_req, E(DEINIT_REQ) = I, E(START_REQ) = I,
    E(STOP_REQ) = I, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = I,
    E(ASSOCIATED) = I, E(DISASSOCIATED) = I, E(LEASE_READY) = I,
    E(DISCONNECT_REQ) = I, E(HOSTED_DOWN) = I, E(INIT_CLAIM) = go_INITING,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = I, E(CP_UNLINKED) = I,
    E(CONNECT_FAIL) = I, E(CONNECT_REQ) = I, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = I
},
[EH_STA_ST_INITING] = {
    E(INIT_REQ) = init_req, E(DEINIT_REQ) = I, E(START_REQ) = I,
    E(STOP_REQ) = I, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = I,
    E(ASSOCIATED) = I, E(DISASSOCIATED) = I, E(LEASE_READY) = I,
    E(DISCONNECT_REQ) = I, E(HOSTED_DOWN) = go_NOT_INITED, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = go_NOT_INITED, E(DISCONNECT_FAIL) = I, E(CP_UNLINKED) = I,
    E(CONNECT_FAIL) = I, E(CONNECT_REQ) = I, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = I
},
[EH_STA_ST_INITED] = {
    E(INIT_REQ) = I, E(DEINIT_REQ) = go_NOT_INITED, E(START_REQ) = sta_begins,
    E(STOP_REQ) = stop_ready, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = I,
    E(ASSOCIATED) = I, E(DISASSOCIATED) = I, E(LEASE_READY) = I,
    E(DISCONNECT_REQ) = I, E(HOSTED_DOWN) = go_NOT_INITED, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = I, E(CP_UNLINKED) = I,
    E(CONNECT_FAIL) = I, E(CONNECT_REQ) = I, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = I
},
[EH_STA_ST_STARTED_NO_STA] = {
    E(INIT_REQ) = I, E(DEINIT_REQ) = go_NOT_INITED, E(START_REQ) = I,
    E(STOP_REQ) = stop_req, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = I,
    E(ASSOCIATED) = I, E(DISASSOCIATED) = I, E(LEASE_READY) = I,
    E(DISCONNECT_REQ) = I, E(HOSTED_DOWN) = go_NOT_INITED, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = I, E(CP_UNLINKED) = I,
    E(CONNECT_FAIL) = I, E(CONNECT_REQ) = I, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = sta_begins
},
[EH_STA_ST_STARTED_CP_OFF] = {
    E(INIT_REQ) = I, E(DEINIT_REQ) = go_NOT_INITED, E(START_REQ) = I,
    E(STOP_REQ) = go_INITED, E(RADIO_STARTED) = radio_back, E(RADIO_STOPPED) = I,
    E(ASSOCIATED) = assoc_back, E(DISASSOCIATED) = I, E(LEASE_READY) = assoc_back,
    E(DISCONNECT_REQ) = I, E(HOSTED_DOWN) = go_NOT_INITED, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = I, E(CP_UNLINKED) = I,
    E(CONNECT_FAIL) = I, E(CONNECT_REQ) = I, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = sta_begins
},
[EH_STA_ST_STARTED] = {
    E(INIT_REQ) = I, E(DEINIT_REQ) = go_NOT_INITED, E(START_REQ) = I,
    E(STOP_REQ) = stop_req, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = go_STARTED_CP_OFF,
    E(ASSOCIATED) = go_CONNECTED, E(DISASSOCIATED) = I, E(LEASE_READY) = go_CONNECTED,
    E(DISCONNECT_REQ) = go_DISCONNECTED, E(HOSTED_DOWN) = go_NOT_INITED, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = I, E(CP_UNLINKED) = I,
    E(CONNECT_FAIL) = I, E(CONNECT_REQ) = go_CONNECTING, E(AUTO_CONNECT_REQ) = go_AUTO_CONNECTING,
    E(STA_ADDED) = I
},
[EH_STA_ST_CONNECTING] = {
    E(INIT_REQ) = I, E(DEINIT_REQ) = go_NOT_INITED, E(START_REQ) = I,
    E(STOP_REQ) = stop_req, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = go_STARTED_CP_OFF,
    E(ASSOCIATED) = go_CONNECTED, E(DISASSOCIATED) = failed_attempt, E(LEASE_READY) = I,
    E(DISCONNECT_REQ) = leave_attempt, E(HOSTED_DOWN) = go_NOT_INITED, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = I, E(CP_UNLINKED) = attempt_unlinked,
    E(CONNECT_FAIL) = connect_failed, E(CONNECT_REQ) = I, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = I
},
[EH_STA_ST_AUTO_CONNECTING] = {
    E(INIT_REQ) = I, E(DEINIT_REQ) = go_NOT_INITED, E(START_REQ) = I,
    E(STOP_REQ) = stop_req, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = go_STARTED_CP_OFF,
    E(ASSOCIATED) = go_CONNECTED, E(DISASSOCIATED) = failed_attempt, E(LEASE_READY) = I,
    E(DISCONNECT_REQ) = leave_attempt, E(HOSTED_DOWN) = go_NOT_INITED, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = I, E(CP_UNLINKED) = attempt_unlinked,
    E(CONNECT_FAIL) = connect_failed, E(CONNECT_REQ) = go_CONNECTING, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = I
},
[EH_STA_ST_CONNECTED] = {
    E(INIT_REQ) = I, E(DEINIT_REQ) = go_NOT_INITED, E(START_REQ) = I,
    E(STOP_REQ) = stop_req_associated, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = radio_stopped_associated,
    E(ASSOCIATED) = reassociated, E(DISASSOCIATED) = dropped, E(LEASE_READY) = I,
    E(DISCONNECT_REQ) = go_DISCONNECTING, E(HOSTED_DOWN) = hosted_down_associated, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = I, E(CP_UNLINKED) = cp_unlinked,
    E(CONNECT_FAIL) = I, E(CONNECT_REQ) = go_RECONNECTING, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = I
},
[EH_STA_ST_RECONNECTING] = {
    E(INIT_REQ) = I, E(DEINIT_REQ) = go_NOT_INITED, E(START_REQ) = I,
    E(STOP_REQ) = stop_req_associated, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = radio_stopped_associated,
    E(ASSOCIATED) = reconnected, E(DISASSOCIATED) = go_CONNECTING, E(LEASE_READY) = I,
    E(DISCONNECT_REQ) = go_DISCONNECTING, E(HOSTED_DOWN) = hosted_down_associated, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = I, E(CP_UNLINKED) = cp_unlinked,
    E(CONNECT_FAIL) = go_CONNECTED, E(CONNECT_REQ) = I, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = I
},
[EH_STA_ST_DISCONNECTING] = {
    E(INIT_REQ) = I, E(DEINIT_REQ) = go_NOT_INITED, E(START_REQ) = I,
    E(STOP_REQ) = stop_req_associated, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = radio_stopped_associated,
    E(ASSOCIATED) = I, E(DISASSOCIATED) = go_DISCONNECTED, E(LEASE_READY) = I,
    E(DISCONNECT_REQ) = I, E(HOSTED_DOWN) = hosted_down_associated, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = go_CONNECTED, E(CP_UNLINKED) = leaving_unlinked,
    E(CONNECT_FAIL) = I, E(CONNECT_REQ) = go_RECONNECTING, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = I
},
[EH_STA_ST_DISCONNECTED] = {
    E(INIT_REQ) = I, E(DEINIT_REQ) = go_NOT_INITED, E(START_REQ) = I,
    E(STOP_REQ) = stop_req, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = go_STARTED_CP_OFF,
    E(ASSOCIATED) = go_CONNECTED, E(DISASSOCIATED) = I, E(LEASE_READY) = I,
    E(DISCONNECT_REQ) = I, E(HOSTED_DOWN) = go_NOT_INITED, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = go_STARTED, E(CP_UNLINKED) = I,
    E(CONNECT_FAIL) = I, E(CONNECT_REQ) = go_CONNECTING, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = I
},
[EH_STA_ST_STOPPING] = {
    E(INIT_REQ) = I, E(DEINIT_REQ) = go_NOT_INITED, E(START_REQ) = restart_req,
    E(STOP_REQ) = I, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = go_INITED,
    E(ASSOCIATED) = I, E(DISASSOCIATED) = I, E(LEASE_READY) = I,
    E(DISCONNECT_REQ) = I, E(HOSTED_DOWN) = go_NOT_INITED, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = I, E(CP_UNLINKED) = I,
    E(CONNECT_FAIL) = I, E(CONNECT_REQ) = I, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = I
},
[EH_STA_ST_RESTARTING] = {
    E(INIT_REQ) = I, E(DEINIT_REQ) = go_NOT_INITED, E(START_REQ) = I,
    E(STOP_REQ) = go_STOPPING, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = radio_up,
    E(ASSOCIATED) = I, E(DISASSOCIATED) = I, E(LEASE_READY) = I,
    E(DISCONNECT_REQ) = I, E(HOSTED_DOWN) = go_NOT_INITED, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = I, E(CP_UNLINKED) = I,
    E(CONNECT_FAIL) = I, E(CONNECT_REQ) = go_RESTARTING_CONNECTING, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = I
},
[EH_STA_ST_RESTARTING_CONNECTING] = {
    E(INIT_REQ) = I, E(DEINIT_REQ) = go_NOT_INITED, E(START_REQ) = I,
    E(STOP_REQ) = go_STOPPING, E(RADIO_STARTED) = I, E(RADIO_STOPPED) = go_CONNECTING,
    E(ASSOCIATED) = I, E(DISASSOCIATED) = I, E(LEASE_READY) = I,
    E(DISCONNECT_REQ) = leave_restart_attempt, E(HOSTED_DOWN) = go_NOT_INITED, E(INIT_CLAIM) = I,
    E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = I, E(CP_UNLINKED) = I,
    E(CONNECT_FAIL) = go_RESTARTING, E(CONNECT_REQ) = I, E(AUTO_CONNECT_REQ) = I,
    E(STA_ADDED) = I
},
};

#undef E
#undef I

static const eh_fsm_state_t s_states[EH_STA_ST_COUNT] = {
    [EH_STA_ST_NOT_INITED]        = { "NOT_INITED" },
    [EH_STA_ST_INITING]       = { "INITING" },
    [EH_STA_ST_INITED]         = { "INITED" },
    [EH_STA_ST_STARTED_NO_STA]      = { "STARTED_NO_STA" },
    [EH_STA_ST_STARTED_CP_OFF]     = { "STARTED_CP_OFF" },
    [EH_STA_ST_STARTED]          = { "STARTED" },
    [EH_STA_ST_CONNECTING]    = { "CONNECTING" },
    [EH_STA_ST_AUTO_CONNECTING] = { "AUTO_CONNECTING" },
    [EH_STA_ST_CONNECTED]     = { "CONNECTED" },
    [EH_STA_ST_RECONNECTING]  = { "RECONNECTING" },
    [EH_STA_ST_DISCONNECTING]       = { "DISCONNECTING" },
    [EH_STA_ST_DISCONNECTED]          = { "DISCONNECTED" },
    [EH_STA_ST_STOPPING]      = { "STOPPING" },
    [EH_STA_ST_RESTARTING]    = { "RESTARTING" },
    [EH_STA_ST_RESTARTING_CONNECTING] = { "RESTARTING_CONNECTING" },
};

static const char *const s_ev_names[EH_STA_EV_COUNT] = {
    "INIT_REQ", "DEINIT_REQ", "START_REQ", "STOP_REQ", "RADIO_STARTED",
    "RADIO_STOPPED", "ASSOCIATED", "DISASSOCIATED", "LEASE_READY", "DISCONNECT_REQ",
    "HOSTED_DOWN", "INIT_CLAIM", "INIT_FAIL", "DISCONNECT_FAIL", "CP_UNLINKED",
    "CONNECT_FAIL", "CONNECT_REQ", "AUTO_CONNECT_REQ", "STA_ADDED",
};

/* ── CP copy ──────────────────────────────────────────────────────────────── */
static int cp_go_off(eh_fsm_t *m, const void *d)   { (void)m; (void)d; return CP_RADIO_OFF; }
static int cp_go_on(eh_fsm_t *m, const void *d)    { (void)m; (void)d; return CP_RADIO_ON; }
static int cp_go_assoc(eh_fsm_t *m, const void *d) { (void)m; (void)d; return CP_CONNECTED; }

#define I eh_fsm_ignore
#define E(ev) [EH_STA_EV_##ev]
#define CP_ROW(radio_started, radio_stopped, associated, disassociated) {          \
    E(INIT_REQ) = I, E(DEINIT_REQ) = I, E(START_REQ) = I, E(STOP_REQ) = I,          \
    E(RADIO_STARTED) = radio_started, E(RADIO_STOPPED) = radio_stopped,              \
    E(ASSOCIATED) = associated, E(DISASSOCIATED) = disassociated,                    \
    E(LEASE_READY) = I, E(DISCONNECT_REQ) = I, E(HOSTED_DOWN) = cp_go_off,           \
    E(INIT_CLAIM) = I, E(INIT_FAIL) = I, E(DISCONNECT_FAIL) = I,                     \
    E(CP_UNLINKED) = cp_go_off, E(CONNECT_FAIL) = I, E(CONNECT_REQ) = I,             \
    E(AUTO_CONNECT_REQ) = I, E(STA_ADDED) = I }

static const eh_fsm_fn_t s_cp_table[CP_ST_COUNT][EH_STA_EV_COUNT] = {
    [CP_RADIO_OFF]   = CP_ROW(cp_go_on, I,         cp_go_assoc, I),
    [CP_RADIO_ON]    = CP_ROW(I,        cp_go_off, cp_go_assoc, I),
    [CP_CONNECTED] = CP_ROW(I,        cp_go_off, I,           cp_go_on),
};

#undef CP_ROW
#undef E
#undef I

static const eh_fsm_state_t s_cp_states[CP_ST_COUNT] = {
    [CP_RADIO_OFF] = { "OFF" }, [CP_RADIO_ON] = { "ON" }, [CP_CONNECTED] = { "ASSOC" },
};

static const eh_fsm_def_t s_cp_def = {
    .name = "sta_cp", .n_states = CP_ST_COUNT, .n_events = EH_STA_EV_COUNT,
    .states = s_cp_states, .table = &s_cp_table[0][0], .ev_names = s_ev_names,
};

static const eh_fsm_def_t s_def = {
    .name = "sta", .n_states = EH_STA_ST_COUNT, .n_events = EH_STA_EV_COUNT,
    .states = s_states, .table = &s_table[0][0], .ev_names = s_ev_names,
    .on_transit = edges,
};

/* ── input ────────────────────────────────────────────────────────────────── */
/* Applied in the RX task, in arrival order: a report sent before an RPC reply is
 * in the FSM before the request that reply completes. */
void eh_host_sta_fsm_report(eh_host_sta_ev_t id, const void *data, size_t size)
{
    wifi_event_sta_connected_t prev;

    (void)size;
    if (!s_sta.mtx || ((id == EH_STA_EV_ASSOCIATED || id == EH_STA_EV_DISASSOCIATED) && !data)) {
        return;
    }
    eh_host_port_mutex_lock(s_sta.mtx);
    prev = s_sta.conn;
    if (id == EH_STA_EV_ASSOCIATED) {
        s_sta.conn = *(const wifi_event_sta_connected_t *)data;
    } else if (id == EH_STA_EV_DISASSOCIATED) {
        s_sta.disc = *(const wifi_event_sta_disconnected_t *)data;
    }
    if (s_cp.def) {
        eh_fsm_dispatch(&s_cp, (int)id, NULL);
    }
    if (g_eh_host_sta_fsm.def) {
        eh_fsm_dispatch(&g_eh_host_sta_fsm, (int)id, id == EH_STA_EV_ASSOCIATED ? &prev : data);
    }
    eh_host_port_mutex_unlock(s_sta.mtx);
}

static void ap_dispatch(eh_host_ap_ev_t ev);

void eh_host_sta_fsm_request(eh_host_sta_ev_t ev)
{
    if (!s_sta.mtx) {
        return;
    }
    eh_host_port_mutex_lock(s_sta.mtx);
    if (g_eh_host_sta_fsm.def) {
        eh_fsm_dispatch(&g_eh_host_sta_fsm, ev, NULL);
    }
    if (ev == EH_STA_EV_START_REQ) {
        ap_dispatch(EH_AP_EV_APP_START);
    } else if (ev == EH_STA_EV_STOP_REQ || ev == EH_STA_EV_DEINIT_REQ) {
        ap_dispatch(EH_AP_EV_APP_STOP);
    }
    eh_host_port_mutex_unlock(s_sta.mtx);
}

/* Before the Wi-Fi feature runs there is nothing to own: the call goes through. */
bool eh_host_sta_fsm_claim_init(void)
{
    bool claimed = true;

    if (!s_sta.mtx) {
        return true;
    }
    eh_host_port_mutex_lock(s_sta.mtx);
    if (g_eh_host_sta_fsm.def) {
        claimed = g_eh_host_sta_fsm.state == EH_STA_ST_NOT_INITED;
        eh_fsm_dispatch(&g_eh_host_sta_fsm, EH_STA_EV_INIT_CLAIM, NULL);
    }
    eh_host_port_mutex_unlock(s_sta.mtx);
    return claimed;
}

/* ── the soft-AP ──────────────────────────────────────────────────────────── */
/* As the station: AP_START / AP_STOP when the app's call returns, as IDF posts them;
 * the CP's reports of those calls are the tail. A report the app did not cause (a
 * mode change, an AP config change) moves the app's AP. */
enum {
    AP_ST_OFF_IDLE,     /* app not started; CP AP off */
    AP_ST_OFF_APP,      /* app started; no AP in its mode, or the CP stopped it */
    AP_ST_ON_IDLE,      /* app not started; CP AP on */
    AP_ST_ON,           /* app started; CP AP on */
    AP_ST_STARTING,     /* app started; the CP's start report pending */
    AP_ST_STOPPING,     /* app stopped; the CP's stop report pending */
    AP_ST_RESTARTING,   /* app started again while STOPPING */
    AP_ST_COUNT
};

static eh_fsm_t s_ap_fsm;

static const bool s_ap_on[AP_ST_COUNT] = {
    [AP_ST_ON] = true, [AP_ST_STARTING] = true, [AP_ST_RESTARTING] = true,
};

#define AP_GO(st) static int ap_go_##st(eh_fsm_t *m, const void *d) { (void)m; (void)d; return AP_ST_##st; }
AP_GO(OFF_IDLE) AP_GO(OFF_APP) AP_GO(ON_IDLE) AP_GO(ON) AP_GO(STARTING) AP_GO(STOPPING)
AP_GO(RESTARTING)
#undef AP_GO

static int ap_start_req(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    return s_sta.ap_mode ? AP_ST_STARTING : AP_ST_OFF_APP;
}

#define I eh_fsm_ignore
_Static_assert(EH_AP_EV_COUNT == 5, "one column per event");

static const eh_fsm_fn_t s_ap_table[AP_ST_COUNT][EH_AP_EV_COUNT] = {
/*                      STARTED        STOPPED           HOSTED_DOWN     APP_START          APP_STOP */
[AP_ST_OFF_IDLE]   = {  ap_go_ON_IDLE, I,                I,              ap_start_req,      I              },
[AP_ST_OFF_APP]    = {  ap_go_ON,      I,                ap_go_OFF_IDLE, I,                 ap_go_OFF_IDLE },
[AP_ST_ON_IDLE]    = {  I,             ap_go_OFF_IDLE,   ap_go_OFF_IDLE, ap_go_ON,          I              },
[AP_ST_ON]         = {  I,             ap_go_OFF_APP,    ap_go_OFF_IDLE, I,                 ap_go_STOPPING },
[AP_ST_STARTING]   = {  ap_go_ON,      ap_go_OFF_APP,    ap_go_OFF_IDLE, I,                 ap_go_STOPPING },
[AP_ST_STOPPING]   = {  I,             ap_go_OFF_IDLE,   ap_go_OFF_IDLE, ap_go_RESTARTING,  I              },
[AP_ST_RESTARTING] = {  I,             ap_go_STARTING,   ap_go_OFF_IDLE, I,                 ap_go_STOPPING },
};
#undef I

static const eh_fsm_state_t s_ap_states[AP_ST_COUNT] = {
    [AP_ST_OFF_IDLE] = { "OFF_IDLE" }, [AP_ST_OFF_APP] = { "OFF_APP" },
    [AP_ST_ON_IDLE] = { "ON_IDLE" }, [AP_ST_ON] = { "ON" }, [AP_ST_STARTING] = { "STARTING" },
    [AP_ST_STOPPING] = { "STOPPING" }, [AP_ST_RESTARTING] = { "RESTARTING" },
};
static const char *const s_ap_ev_names[EH_AP_EV_COUNT] = {
    "STARTED", "STOPPED", "HOSTED_DOWN", "APP_START", "APP_STOP",
};
static void ap_edges(eh_fsm_t *m, int from, int to)
{
    (void)m;
    if (s_ap_on[from] != s_ap_on[to]) {
        post_wifi(s_ap_on[to] ? WIFI_EVENT_AP_START : WIFI_EVENT_AP_STOP, NULL, 0);
    }
}

static const eh_fsm_def_t s_ap_def = {
    .name = "ap", .n_states = AP_ST_COUNT, .n_events = EH_AP_EV_COUNT,
    .states = s_ap_states, .table = &s_ap_table[0][0], .ev_names = s_ap_ev_names,
    .on_transit = ap_edges,
};

/* Under s_sta.mtx. */
static void ap_dispatch(eh_host_ap_ev_t ev)
{
    if (s_ap_fsm.def) {
        eh_fsm_dispatch(&s_ap_fsm, ev, NULL);
    }
}

void eh_host_ap_fsm_report(eh_host_ap_ev_t ev)
{
    if (!s_sta.mtx) {
        return;
    }
    eh_host_port_mutex_lock(s_sta.mtx);
    ap_dispatch(ev);
    eh_host_port_mutex_unlock(s_sta.mtx);
}

bool eh_host_ap_fsm_on(void)
{
    return s_ap_fsm.def && s_ap_on[s_ap_fsm.state];
}

/* Under the lock: the action can run before the dispatch that queued it returns. */
/* The app's own Wi-Fi state, whatever the CP's radio does for CP code. */
esp_err_t eh_host_sta_fsm_app_rc(void)
{
    esp_err_t rc = ESP_OK;

    if (!s_sta.mtx) {
        return ESP_OK;
    }
    eh_host_port_mutex_lock(s_sta.mtx);
    if (g_eh_host_sta_fsm.def) {
        switch (g_eh_host_sta_fsm.state) {
        case EH_STA_ST_NOT_INITED:
        case EH_STA_ST_INITING:
            rc = ESP_ERR_WIFI_NOT_INIT;
            break;
        case EH_STA_ST_INITED:
        case EH_STA_ST_STOPPING:
            rc = ESP_ERR_WIFI_NOT_STARTED;
            break;
        default:
            break;
        }
    }
    eh_host_port_mutex_unlock(s_sta.mtx);
    return rc;
}

esp_err_t eh_host_sta_fsm_connect_begin(void)
{
    esp_err_t rc = ESP_OK;

    if (!s_sta.mtx) {
        return ESP_OK;
    }
    eh_host_port_mutex_lock(s_sta.mtx);
    if (g_eh_host_sta_fsm.def) {
        if (g_eh_host_sta_fsm.state == EH_STA_ST_CONNECTING ||
            g_eh_host_sta_fsm.state == EH_STA_ST_RESTARTING_CONNECTING) {
            rc = ESP_ERR_WIFI_CONN;
        } else {
            eh_fsm_dispatch(&g_eh_host_sta_fsm, EH_STA_EV_CONNECT_REQ, NULL);
        }
    }
    eh_host_port_mutex_unlock(s_sta.mtx);
    return rc;
}

bool eh_host_sta_fsm_auto_connect_begin(void)
{
    bool begun;

    if (!s_sta.mtx) {
        return false;
    }
    eh_host_port_mutex_lock(s_sta.mtx);
    begun = g_eh_host_sta_fsm.def && g_eh_host_sta_fsm.state == EH_STA_ST_STARTED;
    if (begun) {
        eh_fsm_dispatch(&g_eh_host_sta_fsm, EH_STA_EV_AUTO_CONNECT_REQ, NULL);
    }
    eh_host_port_mutex_unlock(s_sta.mtx);
    return begun;
}

void eh_host_sta_fsm_set_connect_policy(bool on_start, bool on_drop)
{
    s_policy.on_start = on_start;
    s_policy.on_drop = on_drop;
}

bool eh_host_sta_fsm_take_sta(void)
{
    bool had;

    if (!s_sta.mtx) {
        return false;
    }
    eh_host_port_mutex_lock(s_sta.mtx);
    had = s_sta.sta_mode;
    s_sta.sta_mode = false;
    eh_host_port_mutex_unlock(s_sta.mtx);
    return had;
}

void eh_host_sta_fsm_give_sta_back(void)
{
    if (!s_sta.mtx) {
        return;
    }
    eh_host_port_mutex_lock(s_sta.mtx);
    s_sta.sta_mode = true;
    eh_host_port_mutex_unlock(s_sta.mtx);
}

void eh_host_sta_fsm_set_mode(bool sta, bool ap)
{
    if (!s_sta.mtx) {
        return;
    }
    eh_host_port_mutex_lock(s_sta.mtx);
    bool added = sta && !s_sta.sta_mode;

    s_sta.sta_mode = sta;
    s_sta.ap_mode = ap;
    if (added && g_eh_host_sta_fsm.def) {
        eh_fsm_dispatch(&g_eh_host_sta_fsm, EH_STA_EV_STA_ADDED, NULL);
    }
    eh_host_port_mutex_unlock(s_sta.mtx);
}

int eh_host_sta_fsm_init(void)
{
    /* The mutex outlives a deinit: a late report may still hold it. */
    eh_host_port_mutex_t *mtx = s_sta.mtx ? s_sta.mtx : eh_host_port_mutex_create();

    if (!mtx) {
        ESP_LOGE(TAG, "mutex create failed");
        return -1;
    }
    if (g_eh_host_sta_fsm.def) {
        return 0;
    }
    memset(&s_sta, 0, sizeof(s_sta));
    s_sta.mtx = mtx;
    if (eh_fsm_init(&g_eh_host_sta_fsm, &s_def, EH_STA_ST_NOT_INITED) != 0 ||
        eh_fsm_init(&s_cp, &s_cp_def, CP_RADIO_OFF) != 0 ||
        eh_fsm_init(&s_ap_fsm, &s_ap_def, AP_ST_OFF_IDLE) != 0) {
        ESP_LOGE(TAG, "init failed");
        g_eh_host_sta_fsm.def = NULL;
        return -1;
    }
    return 0;
}

/* Hosted is going away: the link goes with it, so a started radio ends as in
 * IDF, DISCONNECTED (if connected) then STOP. */
void eh_host_sta_fsm_deinit(void)
{
    if (!g_eh_host_sta_fsm.def) {
        return;
    }
    eh_host_port_mutex_lock(s_sta.mtx);
    eh_fsm_dispatch(&g_eh_host_sta_fsm, EH_STA_EV_HOSTED_DOWN, NULL);
    g_eh_host_sta_fsm.def = NULL;
    eh_fsm_dispatch(&s_cp, EH_STA_EV_HOSTED_DOWN, NULL);
    s_cp.def = NULL;
    ap_dispatch(EH_AP_EV_HOSTED_DOWN);
    s_ap_fsm.def = NULL;
    eh_host_port_mutex_unlock(s_sta.mtx);
}
