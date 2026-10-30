/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* The CP station: IDF's WIFI_EVENTs are facts, host RPCs and CP-local
 * esp_wifi_connect are requests. One mutex per dispatch; cells call IDF. */
#include "eh_cp_master_config.h"

#if EH_CP_FEAT_WIFI_READY
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "eh_cp.h"
#include "eh_cp_core.h"
#include "eh_check.h"
#include "eh_cp_event.h"
#include "eh_cp_feat_wifi.h"
#include "eh_cp_feat_wifi_event.h"
#include "eh_fsm.h"
#include "eh_cp_feat_wifi_sta_fsm.h"

static const char *TAG = "ehcp_event_client";

enum {
	ST_OFF,             /* radio stopped */
	ST_OFF_CONNECT,     /* radio stopped; a connect waits for the start */
	ST_IDLE,            /* started, not associated */
	ST_HELD,            /* as IDLE, but the host disconnected: CP-local connects wait */
	ST_CONNECTING,
	ST_CONNECTED,       /* entry links the RX to core */
	ST_DISCONNECTING,   /* the host disconnected; teardown -> HELD */
	ST_SWITCHING,       /* reconnecting for a new config; the teardown is a published drop */
	ST_COUNT
};
enum {
	EV_RADIO_STARTED, EV_RADIO_STOPPED, EV_ASSOCIATED, EV_DISASSOCIATED,
	EV_SET_CONFIG, EV_CONNECT, EV_LOCAL_CONNECT, EV_DISCONNECT,
	EV_START, EV_RESYNC, EV_SYNC, EV_DEINIT, EV_COUNT
};

typedef struct {
	wifi_interface_t     iface;
	const wifi_config_t *cfg;
} set_config_req_t;

/* SYNC: the driver's state when the feature starts after the app started Wi-Fi. */
typedef struct {
	bool                       linked;
	wifi_event_sta_connected_t conn;
} sync_req_t;

static eh_fsm_t s_fsm;
static bool s_auto_connect;         /* policy, set before or after init; survives deinit */

static SemaphoreHandle_t s_mtx;     /* created once, kept across deinit */

static struct {
	esp_err_t                  result;          /* of the request being dispatched */
	wifi_config_t              pending;         /* a config IDF refused while connecting */
	bool                       pending_valid;
	bool                       connect_after_drop; /* host connect while its disconnect ran */
	wifi_config_t              assoc_cfg;       /* the config the association used */
	wifi_event_sta_connected_t last_conn;
} s_cw;

esp_err_t __real_esp_wifi_connect(void);
esp_err_t __wrap_esp_wifi_connect(void);

static void publish(int32_t id, const void *data, size_t size)
{
	if (esp_event_post(EH_CP_FEAT_WIFI_EVENT, id, (void *)data, size,
			EH_CP_TIMEOUT_IN_MSEC(EH_CP_EVT_DFLT_TIMEOUT)) != ESP_OK) {
		ESP_LOGE(TAG, "station event %d lost", (int)id);
	}
}

static void apply_pending_config(void)
{
	if (s_cw.pending_valid && esp_wifi_set_config(WIFI_IF_STA, &s_cw.pending) == ESP_OK) {
		ESP_LOGI(TAG, "Applied cached STA config");
		s_cw.pending_valid = false;
	}
}

/* An attempt: CONNECTING, OFF_CONNECT (IDF connects only once started), or STAY
 * with the result set. */
static int start_connect(void)
{
	wifi_config_t cfg = {0};

	apply_pending_config();
	if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) {
		return ST_OFF_CONNECT;      /* Wi-Fi not initialised (being re-initialised) */
	}
	if (strnlen((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid)) == 0) {
		ESP_LOGE(TAG, "No SSID configured, cannot connect");
		s_cw.result = ESP_ERR_WIFI_SSID;
		return EH_FSM_STAY;
	}
	esp_err_t ret = __real_esp_wifi_connect();

	if (ret == ESP_ERR_WIFI_NOT_INIT || ret == ESP_ERR_WIFI_NOT_STARTED) {
		return ST_OFF_CONNECT;
	}
	if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN) {
		ESP_LOGE(TAG, "esp_wifi_connect failed: 0x%x", ret);
		s_cw.result = ret;
		return EH_FSM_STAY;
	}
	return ST_CONNECTING;
}

/* ── entry / exit ─────────────────────────────────────────────────────────── */
static void off_entry(eh_fsm_t *m)
{
	(void)m;
	s_cw.connect_after_drop = false;
	apply_pending_config();         /* IDF takes a config while stopped */
}

static void connected_entry(eh_fsm_t *m)
{
	(void)m;
	(void)esp_wifi_get_config(WIFI_IF_STA, &s_cw.assoc_cfg);
	publish(EH_CP_FEAT_WIFI_EVT_STA_CONNECTED_TO_AP, &s_cw.last_conn, sizeof(s_cw.last_conn));
	eh_cp_feat_wifi_link(WIFI_IF_STA, true);
	size_t cl = s_cw.last_conn.ssid_len;
	if (cl > sizeof(s_cw.last_conn.ssid)) cl = sizeof(s_cw.last_conn.ssid);
	ESP_LOGI(TAG, "--- Station connected %.*s ---", (int)cl, s_cw.last_conn.ssid);
}

static void connected_exit(eh_fsm_t *m)
{
	(void)m;
	eh_cp_feat_wifi_link(WIFI_IF_STA, false);
	memset(&s_cw.last_conn, 0, sizeof(s_cw.last_conn));
}

static void switching_entry(eh_fsm_t *m)
{
	(void)m;
	ESP_LOGW(TAG, "STA config changed; reconnecting");
	esp_wifi_disconnect();
}

/* ── handlers ─────────────────────────────────────────────────────────────── */
#define GO(st) static int go_##st(eh_fsm_t *m, const void *d) \
	{ (void)m; (void)d; return ST_##st; }

GO(OFF) GO(DISCONNECTING)

/* The radio started: IDLE, or an attempt when the auto-connect policy is on. */
static int started_off(eh_fsm_t *m, const void *d)
{
	int next;

	(void)m;
	(void)d;
	publish(EH_CP_FEAT_WIFI_EVT_STA_STARTED, NULL, 0);
	if (!s_auto_connect) {
		return ST_IDLE;
	}
	ESP_LOGI(TAG, "auto-connect on start");
	next = start_connect();
	return next == EH_FSM_STAY ? ST_IDLE : next;
}

/* A start already seen (a duplicate IDF event): published, nothing else. */
static int started_stay(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	publish(EH_CP_FEAT_WIFI_EVT_STA_STARTED, NULL, 0);
	return EH_FSM_STAY;
}

static int started_connect(eh_fsm_t *m, const void *d)
{
	int next;

	(void)m;
	(void)d;
	publish(EH_CP_FEAT_WIFI_EVT_STA_STARTED, NULL, 0);
	next = start_connect();
	return next == EH_FSM_STAY ? ST_IDLE : next;
}

static int stopped(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	publish(EH_CP_FEAT_WIFI_EVT_STA_STOPPED, NULL, 0);
	return ST_OFF;
}

/* A connect waits for the start: the stop does not cancel it. */
static int stopped_keep_connect(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	publish(EH_CP_FEAT_WIFI_EVT_STA_STOPPED, NULL, 0);
	return EH_FSM_STAY;
}

/* IDF reports an association: it is the fact, whatever was expected. */
static int associated(eh_fsm_t *m, const void *d)
{
	(void)m;
	s_cw.last_conn = *(const wifi_event_sta_connected_t *)d;
	return s_cw.pending_valid ? ST_SWITCHING : ST_CONNECTED;
}

/* HELD: the host disconnected; no switch reconnects on its behalf. */
static int held_associated(eh_fsm_t *m, const void *d)
{
	(void)m;
	s_cw.last_conn = *(const wifi_event_sta_connected_t *)d;
	return ST_CONNECTED;
}

static int associated_again(eh_fsm_t *m, const void *d)
{
	(void)m;
	s_cw.last_conn = *(const wifi_event_sta_connected_t *)d;
	publish(EH_CP_FEAT_WIFI_EVT_STA_CONNECTED_TO_AP, &s_cw.last_conn, sizeof(s_cw.last_conn));
	return EH_FSM_STAY;
}

/* An attempt completed after the host disconnected, or while switching: down again. */
static int associated_unwanted(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	apply_pending_config();
	esp_wifi_disconnect();
	return EH_FSM_STAY;
}

static int publish_disc(eh_fsm_t *m, const void *d)
{
	const wifi_event_sta_disconnected_t *e = d;

	(void)m;
	publish(EH_CP_FEAT_WIFI_EVT_STA_DISCONNECTED_FROM_AP, e, sizeof(*e));
	ESP_LOGV(TAG, "Station disconnected, reason[%u]", e->reason);
	return EH_FSM_STAY;
}

static int dropped(eh_fsm_t *m, const void *d)
{
	publish_disc(m, d);
	return ST_IDLE;
}

/* The host's disconnect completed; a host connect sent meanwhile runs now. */
static int dropped_held(eh_fsm_t *m, const void *d)
{
	int next;

	publish_disc(m, d);
	if (!s_cw.connect_after_drop) {
		return ST_HELD;
	}
	s_cw.connect_after_drop = false;
	next = start_connect();
	return next == EH_FSM_STAY ? ST_IDLE : next;
}

static int connect_after_drop(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	s_cw.connect_after_drop = true;
	return EH_FSM_STAY;
}

static int disconnect_again(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	s_cw.connect_after_drop = false;
	return EH_FSM_STAY;
}

/* Teardown for a new config: a real drop, so the app that asked for it gets its
 * DISCONNECTED and then the new link. */
static int switched(eh_fsm_t *m, const void *d)
{
	int next;

	publish_disc(m, d);
	next = start_connect();
	return next == EH_FSM_STAY ? ST_IDLE : next;
}

/* IDF refuses a config while connecting (ESP_ERR_WIFI_STATE): keep it, apply it
 * on the teardown. Returns whether it was kept. */
/* The fields IDF associates with and stores unchanged; it rewrites the auth threshold
 * and PMF itself, so those would never compare equal. */
static bool sta_cfg_same(const wifi_config_t *a, const wifi_config_t *b)
{
	const wifi_sta_config_t *x = &a->sta, *y = &b->sta;

	return strncmp((const char *)x->ssid, (const char *)y->ssid, sizeof(x->ssid)) == 0 &&
	       strncmp((const char *)x->password, (const char *)y->password, sizeof(x->password)) == 0 &&
	       x->bssid_set == y->bssid_set &&
	       (!x->bssid_set || memcmp(x->bssid, y->bssid, sizeof(x->bssid)) == 0) &&
	       x->channel == y->channel;
}

static bool apply_config(const set_config_req_t *r)
{
	wifi_config_t applied = *r->cfg;

	s_cw.result = esp_wifi_set_config(r->iface, &applied);
	if (r->iface != WIFI_IF_STA || s_cw.result != ESP_ERR_WIFI_STATE) {
		if (r->iface == WIFI_IF_STA && s_cw.result == ESP_OK) {
			s_cw.pending_valid = false;
			ESP_LOGW(TAG, "Applied WiFi config SSID: %.*s",
					(int)sizeof(r->cfg->sta.ssid), (const char *)r->cfg->sta.ssid);
		}
		return false;
	}
	ESP_LOGW(TAG, "Caching new WiFi config SSID: %.*s",
			(int)sizeof(r->cfg->sta.ssid), (const char *)r->cfg->sta.ssid);
	s_cw.pending = *r->cfg;
	s_cw.pending_valid = true;
	s_cw.result = ESP_OK;
	return true;
}

static int set_config(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)apply_config(d);
	return EH_FSM_STAY;
}

/* The same config during an attempt: already in place, the attempt goes on. */
static int set_config_connecting(eh_fsm_t *m, const void *d)
{
	const set_config_req_t *r = d;
	wifi_config_t cur = {0};

	(void)m;
	if (r->iface == WIFI_IF_STA && esp_wifi_get_config(WIFI_IF_STA, &cur) == ESP_OK &&
	    sta_cfg_same(s_cw.pending_valid ? &s_cw.pending : &cur, r->cfg)) {
		s_cw.result = ESP_OK;
		return EH_FSM_STAY;
	}
	return apply_config(d) ? ST_SWITCHING : EH_FSM_STAY;
}

/* A host connect, as IDF: none before init. Inited but stopped waits for the start. */
static int host_connect(eh_fsm_t *m, const void *d)
{
	wifi_mode_t mode;

	(void)m;
	(void)d;
	if (esp_wifi_get_mode(&mode) == ESP_ERR_WIFI_NOT_INIT) {
		s_cw.result = ESP_ERR_WIFI_NOT_INIT;
		return EH_FSM_STAY;
	}
	return start_connect();
}

/* A host connect while the station is off: IDF answers (not inited, not started, AP-only
 * mode); nothing is kept for a later start. */
static int host_connect_stopped(eh_fsm_t *m, const void *d)
{
	esp_err_t ret = __real_esp_wifi_connect();

	(void)m;
	(void)d;
	if (ret == ESP_OK || ret == ESP_ERR_WIFI_CONN) {
		return ST_CONNECTING;           /* the radio was already up: an attempt runs */
	}
	s_cw.result = ret;
	return EH_FSM_STAY;
}

/* A host connect during an attempt. Plain mode: only the host starts attempts, so it
 * is the app's own: IDF's ESP_ERR_WIFI_CONN. Network split: it joins the CP's attempt. */
static int connecting_host_connect(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	if (!EH_CP_FEAT_NW_SPLIT_READY) {
		s_cw.result = ESP_ERR_WIFI_CONN;
	}
	return EH_FSM_STAY;
}

/* CP code connecting: a Wi-Fi re-init in progress defers it to the start. In
 * CONNECTING it is the app retrying from a STA_DISCONNECTED this FSM has not seen yet. */
static int local_connect(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	return start_connect();
}

/* A host connect lifts the hold even when the attempt cannot start. */
static int held_connect(eh_fsm_t *m, const void *d)
{
	int next = host_connect(m, d);

	return next == EH_FSM_STAY ? ST_IDLE : next;
}

static int held_local_connect(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	ESP_LOGI(TAG, "Local connect ignored: host disconnected");
	return EH_FSM_STAY;
}

static bool config_moved(void)
{
	wifi_config_t cur = {0};

	if (s_cw.pending_valid) {
		return !sta_cfg_same(&s_cw.pending, &s_cw.assoc_cfg);
	}
	return esp_wifi_get_config(WIFI_IF_STA, &cur) == ESP_OK && !sta_cfg_same(&cur, &s_cw.assoc_cfg);
}

/* Connected: reconnect only when the config moved since the association. The
 * host asked, the CP holds the link: replay the association to it. */
static int connected_host_connect(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	if (config_moved()) {
		return ST_SWITCHING;
	}
	ESP_LOGI(TAG, "Station already connected");
	publish(EH_CP_FEAT_WIFI_EVT_STA_CONNECTED_TO_AP, &s_cw.last_conn, sizeof(s_cw.last_conn));
	publish(EH_CP_FEAT_WIFI_EVT_STA_REPLAYED, NULL, 0);
	return EH_FSM_STAY;
}

static int connected_local_connect(eh_fsm_t *m, const void *d)
{
	wifi_ap_record_t ap;

	if (config_moved()) {
		return ST_SWITCHING;
	}
	/* IDF reports a drop to the app before this FSM: its retry is real. */
	if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
		return local_connect(m, d);
	}
	ESP_LOGI(TAG, "Station already connected");
	return EH_FSM_STAY;
}

static int disconnect_stay(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	s_cw.result = esp_wifi_disconnect();
	return EH_FSM_STAY;
}

static int disconnect_to_off(eh_fsm_t *m, const void *d)
{
	(void)disconnect_stay(m, d);
	return ST_OFF;
}

static int disconnect_to_held(eh_fsm_t *m, const void *d)
{
	(void)disconnect_stay(m, d);
	return ST_HELD;
}

static int disconnect_to_disconnecting(eh_fsm_t *m, const void *d)
{
	(void)disconnect_stay(m, d);
	return ST_DISCONNECTING;
}

/* Host start, radio off: start it; the RADIO_STARTED fact does the rest. */
static int start_radio(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	s_cw.result = esp_wifi_start();
	return EH_FSM_STAY;
}

/* Host start, radio already up (a host that woke): no IDF start event will come,
 * so tell the host. */
static int start_replay(eh_fsm_t *m, const void *d)
{
	int32_t id = WIFI_EVENT_STA_START;

	(void)m;
	(void)d;
	s_cw.result = esp_wifi_start();
	publish(EH_CP_FEAT_WIFI_EVT_WITH_NO_ARGS, &id, sizeof(id));
	return EH_FSM_STAY;
}

/* The host's view of the association the CP holds; network split follows. */
static int resync_connected(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	publish(EH_CP_FEAT_WIFI_EVT_STA_CONNECTED_TO_AP, &s_cw.last_conn, sizeof(s_cw.last_conn));
	publish(EH_CP_FEAT_WIFI_EVT_STA_REPLAYED, NULL, 0);
	return EH_FSM_STAY;
}

static int start_replay_connected(eh_fsm_t *m, const void *d)
{
	(void)start_replay(m, d);
	return resync_connected(m, d);
}

/* The feature started after the app: take the driver's state. */
static int sync_off(eh_fsm_t *m, const void *d)
{
	const sync_req_t *r = d;

	if (!r->linked) {
		return started_off(m, d);
	}
	publish(EH_CP_FEAT_WIFI_EVT_STA_STARTED, NULL, 0);
	s_cw.last_conn = r->conn;
	return ST_CONNECTED;
}

/* ── the table ────────────────────────────────────────────────────────────── */
#define I eh_fsm_ignore

_Static_assert(EV_COUNT == 12, "one column per event");

static const eh_fsm_fn_t s_table[ST_COUNT][EV_COUNT] = {
[ST_OFF] = {
	[EV_RADIO_STARTED] = started_off,      [EV_RADIO_STOPPED] = stopped,
	[EV_ASSOCIATED]    = associated,       [EV_DISASSOCIATED] = publish_disc,
	[EV_SET_CONFIG]    = set_config,       [EV_CONNECT]       = host_connect_stopped,
	[EV_LOCAL_CONNECT] = local_connect,    [EV_DISCONNECT]    = disconnect_stay,
	[EV_START]         = start_radio,      [EV_RESYNC]        = I,
	[EV_SYNC]          = sync_off,         [EV_DEINIT]        = I },
[ST_OFF_CONNECT] = {
	[EV_RADIO_STARTED] = started_connect,  [EV_RADIO_STOPPED] = stopped_keep_connect,
	[EV_ASSOCIATED]    = associated,       [EV_DISASSOCIATED] = publish_disc,
	[EV_SET_CONFIG]    = set_config,       [EV_CONNECT]       = I,
	[EV_LOCAL_CONNECT] = I,                [EV_DISCONNECT]    = disconnect_to_off,
	[EV_START]         = start_radio,      [EV_RESYNC]        = I,
	[EV_SYNC]          = I,                [EV_DEINIT]        = go_OFF },
[ST_IDLE] = {
	[EV_RADIO_STARTED] = started_stay,     [EV_RADIO_STOPPED] = stopped,
	[EV_ASSOCIATED]    = associated,       [EV_DISASSOCIATED] = publish_disc,
	[EV_SET_CONFIG]    = set_config,       [EV_CONNECT]       = host_connect,
	[EV_LOCAL_CONNECT] = local_connect,    [EV_DISCONNECT]    = disconnect_to_held,
	[EV_START]         = start_replay,     [EV_RESYNC]        = I,
	[EV_SYNC]          = I,                [EV_DEINIT]        = go_OFF },
[ST_HELD] = {
	[EV_RADIO_STARTED] = started_stay,     [EV_RADIO_STOPPED] = stopped,
	[EV_ASSOCIATED]    = held_associated,  [EV_DISASSOCIATED] = publish_disc,
	[EV_SET_CONFIG]    = set_config,       [EV_CONNECT]       = held_connect,
	[EV_LOCAL_CONNECT] = held_local_connect, [EV_DISCONNECT]  = disconnect_stay,
	[EV_START]         = start_replay,     [EV_RESYNC]        = I,
	[EV_SYNC]          = I,                [EV_DEINIT]        = go_OFF },
[ST_CONNECTING] = {
	[EV_RADIO_STARTED] = started_stay,     [EV_RADIO_STOPPED] = stopped,
	[EV_ASSOCIATED]    = associated,       [EV_DISASSOCIATED] = dropped,
	[EV_SET_CONFIG]    = set_config_connecting, [EV_CONNECT]  = connecting_host_connect,
	[EV_LOCAL_CONNECT] = local_connect,    [EV_DISCONNECT]    = disconnect_to_disconnecting,
	[EV_START]         = start_replay,     [EV_RESYNC]        = I,
	[EV_SYNC]          = I,                [EV_DEINIT]        = go_OFF },
[ST_CONNECTED] = {
	[EV_RADIO_STARTED] = started_stay,     [EV_RADIO_STOPPED] = stopped,
	[EV_ASSOCIATED]    = associated_again, [EV_DISASSOCIATED] = dropped,
	[EV_SET_CONFIG]    = set_config,       [EV_CONNECT]       = connected_host_connect,
	[EV_LOCAL_CONNECT] = connected_local_connect, [EV_DISCONNECT] = disconnect_to_disconnecting,
	[EV_START]         = start_replay_connected, [EV_RESYNC]  = resync_connected,
	[EV_SYNC]          = I,                [EV_DEINIT]        = go_OFF },
[ST_DISCONNECTING] = {
	[EV_RADIO_STARTED] = started_stay,     [EV_RADIO_STOPPED] = stopped,
	[EV_ASSOCIATED]    = associated_unwanted, [EV_DISASSOCIATED] = dropped_held,
	[EV_SET_CONFIG]    = set_config,       [EV_CONNECT]       = connect_after_drop,
	[EV_LOCAL_CONNECT] = held_local_connect, [EV_DISCONNECT]  = disconnect_again,
	[EV_START]         = start_replay,     [EV_RESYNC]        = I,
	[EV_SYNC]          = I,                [EV_DEINIT]        = go_OFF },
[ST_SWITCHING] = {
	[EV_RADIO_STARTED] = started_stay,     [EV_RADIO_STOPPED] = stopped,
	[EV_ASSOCIATED]    = associated_unwanted, [EV_DISASSOCIATED] = switched,
	[EV_SET_CONFIG]    = set_config,       [EV_CONNECT]       = I,
	[EV_LOCAL_CONNECT] = I,                [EV_DISCONNECT]    = go_DISCONNECTING,
	[EV_START]         = start_replay,     [EV_RESYNC]        = I,
	[EV_SYNC]          = I,                [EV_DEINIT]        = go_OFF },
};

#undef I

static const eh_fsm_state_t s_states[ST_COUNT] = {
	[ST_OFF]           = { "OFF", off_entry },
	[ST_OFF_CONNECT]   = { "OFF_CONNECT", off_entry },
	[ST_IDLE]          = { "IDLE" },
	[ST_HELD]          = { "HELD" },
	[ST_CONNECTING]    = { "CONNECTING" },
	[ST_CONNECTED]     = { "CONNECTED", connected_entry, connected_exit },
	[ST_DISCONNECTING] = { "DISCONNECTING" },
	[ST_SWITCHING]     = { "SWITCHING", switching_entry },
};

static const char *const s_ev_names[EV_COUNT] = {
	"RADIO_STARTED", "RADIO_STOPPED", "ASSOCIATED", "DISASSOCIATED",
	"SET_CONFIG", "CONNECT", "LOCAL_CONNECT", "DISCONNECT",
	"START", "RESYNC", "SYNC", "DEINIT",
};

static const eh_fsm_def_t s_def = {
	.name = "cp_sta", .n_states = ST_COUNT, .n_events = EV_COUNT,
	.states = s_states, .table = &s_table[0][0], .ev_names = s_ev_names,
};

/* false: the feature is not running (before init, after deinit). */
static bool run(int ev, const void *data, esp_err_t *rc)
{
	bool running;

	if (!s_mtx) {
		return false;
	}
	xSemaphoreTakeRecursive(s_mtx, portMAX_DELAY);
	running = s_fsm.def != NULL;
	if (running) {
		s_cw.result = ESP_OK;
		eh_fsm_dispatch(&s_fsm, ev, data);
		*rc = s_cw.result;
	}
	xSemaphoreGiveRecursive(s_mtx);
	return running;
}

static esp_err_t dispatch(int ev, const void *data)
{
	esp_err_t rc;

	return run(ev, data, &rc) ? rc : ESP_ERR_INVALID_STATE;
}

/* ── inputs ───────────────────────────────────────────────────────────────── */
void eh_cp_sta_fsm_wifi_event(int32_t id, void *data)
{
	switch (id) {
	case WIFI_EVENT_STA_START:        dispatch(EV_RADIO_STARTED, NULL); break;
	case WIFI_EVENT_STA_STOP:         dispatch(EV_RADIO_STOPPED, NULL); break;
	case WIFI_EVENT_STA_CONNECTED:    dispatch(EV_ASSOCIATED, data); break;
	case WIFI_EVENT_STA_DISCONNECTED: dispatch(EV_DISASSOCIATED, data); break;
	default: break;
	}
}

/* Every CP-local connect is a request: it waits while the host holds the link down. */
esp_err_t __wrap_esp_wifi_connect(void)
{
	esp_err_t rc;

	return run(EV_LOCAL_CONNECT, NULL, &rc) ? rc : __real_esp_wifi_connect();
}

esp_err_t eh_cp_feat_wifi_request_connect(void)
{
	return dispatch(EV_CONNECT, NULL);
}

/* esp_wifi_start for the host; a radio already up replays the station to it. */
esp_err_t eh_cp_feat_wifi_request_start(void)
{
	esp_err_t ret;

	if (!run(EV_START, NULL, &ret)) {
		ret = esp_wifi_start();
	}
	return ret;
}

void eh_cp_feat_wifi_resync_host(void)
{
	(void)dispatch(EV_RESYNC, NULL);
}

void eh_cp_feat_wifi_set_auto_connect(bool on)
{
	s_auto_connect = on;
}

esp_err_t eh_cp_feat_wifi_request_disconnect(void)
{
	return dispatch(EV_DISCONNECT, NULL);
}

esp_err_t eh_cp_feat_wifi_set_sta_config(wifi_interface_t iface, const wifi_config_t *cfg)
{
	if (!cfg) {
		return ESP_ERR_INVALID_ARG;
	}
	set_config_req_t r = { .iface = iface, .cfg = cfg };

	return dispatch(EV_SET_CONFIG, &r);
}

esp_err_t eh_cp_feat_wifi_request_connect_with_config(const wifi_config_t *cfg)
{
	wifi_mode_t mode = WIFI_MODE_NULL;

	if (!cfg) {
		return ESP_ERR_INVALID_ARG;
	}
	ESP_LOGI(TAG, "Apply STA config SSID: %.*s",
			(int)sizeof(cfg->sta.ssid), (const char *)cfg->sta.ssid);
	/* A station is needed; an AP the app runs stays. */
	if (esp_wifi_get_mode(&mode) == ESP_OK && !(mode & WIFI_MODE_STA)) {
		EH_CHECK_OK_WARN(esp_wifi_set_mode((mode & WIFI_MODE_AP) ? WIFI_MODE_APSTA : WIFI_MODE_STA));
	}
	esp_err_t ret = eh_cp_feat_wifi_set_sta_config(WIFI_IF_STA, cfg);

	return ret != ESP_OK ? ret : eh_cp_feat_wifi_request_connect();
}

/* ── queries: lock-free reads of the single-writer state ──────────────────── */
bool eh_cp_feat_wifi_is_started(void)
{
	return s_fsm.def && s_fsm.state != ST_OFF && s_fsm.state != ST_OFF_CONNECT;
}

bool eh_cp_feat_wifi_is_station_connected(void)
{
	return s_fsm.def && s_fsm.state == ST_CONNECTED;
}

/* ── lifecycle ────────────────────────────────────────────────────────────── */
/* The driver's station state: IDF has no is-started query; get_max_tx_power
 * returns NOT_STARTED until the radio starts. */
void eh_cp_feat_wifi_driver_state(bool *started, bool *linked, wifi_event_sta_connected_t *conn)
{
	wifi_mode_t mode;
	int8_t pwr;
	wifi_ap_record_t ap;

	*started = esp_wifi_get_mode(&mode) == ESP_OK && (mode & WIFI_MODE_STA) &&
	           esp_wifi_get_max_tx_power(&pwr) == ESP_OK;
	*linked = *started && esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
	if (*linked && conn) {
		memset(conn, 0, sizeof(*conn));
		conn->channel = ap.primary;
		conn->authmode = ap.authmode;
		conn->ssid_len = strnlen((const char *)ap.ssid, sizeof(conn->ssid));
		memcpy(conn->ssid, ap.ssid, conn->ssid_len);
		memcpy(conn->bssid, ap.bssid, sizeof(conn->bssid));
		esp_wifi_sta_get_aid(&conn->aid);
	}
}

esp_err_t eh_cp_sta_fsm_init(void)
{
	sync_req_t r = {0};
	bool started;

	if (!s_mtx && !(s_mtx = xSemaphoreCreateRecursiveMutex())) {
		return ESP_ERR_NO_MEM;
	}
	xSemaphoreTakeRecursive(s_mtx, portMAX_DELAY);
	if (!s_fsm.def) {
		memset(&s_cw, 0, sizeof(s_cw));
		(void)eh_fsm_init(&s_fsm, &s_def, ST_OFF);
	}
	xSemaphoreGiveRecursive(s_mtx);
	/* The app may start Wi-Fi before this feature: begin from the driver's state. */
	eh_cp_feat_wifi_driver_state(&started, &r.linked, &r.conn);
	if (started) {
		(void)dispatch(EV_SYNC, &r);
	}
	return s_fsm.def ? ESP_OK : ESP_FAIL;
}

void eh_cp_sta_fsm_deinit(void)
{
	if (!s_mtx) {
		return;
	}
	xSemaphoreTakeRecursive(s_mtx, portMAX_DELAY);
	if (s_fsm.def) {
		eh_fsm_dispatch(&s_fsm, EV_DEINIT, NULL);  /* CONNECTED's exit unlinks the RX */
		s_fsm.def = NULL;
	}
	xSemaphoreGiveRecursive(s_mtx);
}
#endif /* EH_CP_FEAT_WIFI_READY */
