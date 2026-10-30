/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Who acquires the shared IPv4, the CP's DHCP client or the host; the CP decides.
 * A switch while associated waits for the disconnect: stopping a bound client
 * would DHCPRELEASE an address the other side uses. A CP boot starts from Kconfig. */
#include "eh_cp_master_config.h"

#if EH_CP_FEAT_NW_SPLIT_READY
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "eh_fsm.h"
#include "eh_cp_feat_wifi.h"
#include "eh_common_caps.h"
#include "eh_cp_feat_nw_split_apis.h"
#include "eh_cp_feat_nw_split_events.h"
#include "eh_cp_nw_split_ip_owner_fsm.h"

static const char *TAG = "ehcp_nw_split";

enum {
    ST_STOPPED, ST_CP_DOWN, ST_CP_UP, ST_CP_UP_TO_HOST, ST_HOST_DOWN, ST_HOST_UP,
    ST_HOST_UP_TO_CP, ST_COUNT
};
enum {
    EV_START, EV_STOP, EV_WANT_CP, EV_WANT_HOST, EV_LINK_UP, EV_LINK_DOWN, EV_HOST_LEASE,
    EV_RESYNC, EV_COUNT
};

typedef struct {
    esp_err_t                   rc;
    const esp_netif_ip_info_t  *ip;     /* HOST_LEASE */
    const esp_netif_dns_info_t *dns;
    uint8_t                     dns_type;
    esp_netif_t                *netif;  /* START */
    uint8_t                     owner;
    bool                        owned, host_allowed;
} req_t;

static eh_fsm_t s_fsm;
static SemaphoreHandle_t s_lock;     /* created once, kept across deinit */
static esp_netif_t *s_netif;
static bool s_host_allowed;

static void client_off(void)
{
    esp_err_t rc = esp_netif_dhcpc_stop(s_netif);

    if (rc != ESP_OK && rc != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGE(TAG, "dhcpc_stop: %s", esp_err_to_name(rc));
    }
}

/* The netif is down here: esp_netif only re-arms it, the client starts on connect. */
static void client_on(void)
{
    esp_err_t rc = esp_netif_dhcpc_start(s_netif);

    if (rc != ESP_OK && rc != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        ESP_LOGE(TAG, "dhcpc_start: %s", esp_err_to_name(rc));
    }
}

/* A CP app that stopped the client and set an address keeps it: the host cannot own it. */
static bool cp_static(void)
{
    esp_netif_dhcp_status_t st = ESP_NETIF_DHCP_INIT;
    esp_netif_ip_info_t ip = {0};

    if (!(esp_netif_get_flags(s_netif) & ESP_NETIF_DHCP_CLIENT)) {
        return true;
    }
    return esp_netif_dhcpc_get_status(s_netif, &st) == ESP_OK && st == ESP_NETIF_DHCP_STOPPED &&
           esp_netif_get_ip_info(s_netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

static bool may_go_host(eh_fsm_t *m, req_t *r)
{
    if (s_host_allowed && !cp_static()) {
        return true;
    }
    ESP_LOGW(TAG, "%s: host may not own the IP (%s)", eh_fsm_state_name(m, m->state),
             s_host_allowed ? "CP app static IP" : "DHCP owner COPROCESSOR");
    r->rc = ESP_ERR_NOT_ALLOWED;
    return false;
}

static int grant(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    return EH_FSM_STAY;
}

static int cp_down_want_host(eh_fsm_t *m, const void *d)
{
    if (!may_go_host(m, (req_t *)d)) {
        return EH_FSM_STAY;
    }
    client_off();
    return ST_HOST_DOWN;
}

static int cp_up_want_host(eh_fsm_t *m, const void *d)
{
    return may_go_host(m, (req_t *)d) ? ST_CP_UP_TO_HOST : EH_FSM_STAY;
}

static int host_down_want_cp(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    client_on();
    return ST_CP_DOWN;
}

static int to_cp_up(eh_fsm_t *m, const void *d)     { (void)m; (void)d; return ST_CP_UP; }
static int to_cp_down(eh_fsm_t *m, const void *d)   { (void)m; (void)d; return ST_CP_DOWN; }
static int to_host_up(eh_fsm_t *m, const void *d)   { (void)m; (void)d; return ST_HOST_UP; }
static int to_host_down(eh_fsm_t *m, const void *d) { (void)m; (void)d; return ST_HOST_DOWN; }
static int to_host_up_to_cp(eh_fsm_t *m, const void *d) { (void)m; (void)d; return ST_HOST_UP_TO_CP; }

/* The lease went with the disconnect: switch now. */
static int down_to_host(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    client_off();
    return ST_HOST_DOWN;
}

static int down_to_cp(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    client_on();
    return ST_CP_DOWN;
}

static int refuse_lease(eh_fsm_t *m, const void *d)
{
    req_t *r = (req_t *)d;

    ESP_LOGW(TAG, "%s: host lease " IPSTR " refused, the CP owns the IP",
             eh_fsm_state_name(m, m->state), IP2STR(&r->ip->ip));
    r->rc = ESP_ERR_NOT_ALLOWED;
    return EH_FSM_STAY;
}

static int take_lease(eh_fsm_t *m, const void *d)
{
    req_t *r = (req_t *)d;

    (void)m;
    r->rc = esp_netif_set_ip_info(s_netif, r->ip);
    if (r->rc != ESP_OK) {
        ESP_LOGE(TAG, "set_ip_info " IPSTR ": %s", IP2STR(&r->ip->ip), esp_err_to_name(r->rc));
        return EH_FSM_STAY;
    }
    if (r->dns && r->dns->ip.u_addr.ip4.addr != 0) {
        r->rc = esp_netif_set_dns_info(s_netif, (esp_netif_dns_type_t)r->dns_type,
                                       (esp_netif_dns_info_t *)r->dns);
    }
    ESP_LOGI(TAG, "host lease on the CP netif: " IPSTR, IP2STR(&r->ip->ip));
    return EH_FSM_STAY;
}

/* With the client the owner wants. A netif network split created after the app
 * started Wi-Fi missed IDF's events: bring it to the driver's state. The app's
 * own netif had them. */
static int start(eh_fsm_t *m, const void *d)
{
    const req_t *r = d;
    bool host = r->owner == ESP_EXT_CAP_NW_SPLIT_DHCP_HOST;
    esp_netif_dhcp_status_t st = ESP_NETIF_DHCP_INIT;
    bool started, linked;

    (void)m;
    s_netif = r->netif;
    s_host_allowed = r->host_allowed;
    /* Read here, under the lock: no link change falls between it and the state. */
    eh_cp_feat_wifi_driver_state(&started, &linked, NULL);
    if (host) {
        client_off();
    }
    if (r->owned && started) {
        esp_netif_action_start(s_netif, WIFI_EVENT, WIFI_EVENT_STA_START, NULL);
    }
    if (r->owned && linked && esp_netif_dhcpc_get_status(s_netif, &st) == ESP_OK &&
        st != ESP_NETIF_DHCP_STARTED) {
        esp_netif_action_connected(s_netif, WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, NULL);
    }
    ESP_LOGI(TAG, "ip owner %s (host %s)", host ? "HOST" : "CP",
             s_host_allowed ? "may own it" : "may not own it");
    return host ? (linked ? ST_HOST_UP : ST_HOST_DOWN) : (linked ? ST_CP_UP : ST_CP_DOWN);
}

static int stop(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    s_netif = NULL;
    return ST_STOPPED;
}

static int invalid_state(eh_fsm_t *m, const void *d)
{
    (void)m;
    ((req_t *)d)->rc = ESP_ERR_INVALID_STATE;
    return EH_FSM_STAY;
}

/* The host woke or reconnected: give it the lease the CP side holds. */
static int resync(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    eh_cp_nw_split_replay_status();
    return EH_FSM_STAY;
}

#define I eh_fsm_ignore
#define NR invalid_state

_Static_assert(EV_COUNT == 8, "one column per event");

static const eh_fsm_fn_t s_table[ST_COUNT][EV_COUNT] = {
/*                      START  STOP  WANT_CP            WANT_HOST          LINK_UP     LINK_DOWN     HOST_LEASE    RESYNC */
[ST_STOPPED]       = {  start, I,    NR,                NR,                I,          I,            NR,           I      },
[ST_CP_DOWN]       = {  NR,    stop, grant,             cp_down_want_host, to_cp_up,   I,            refuse_lease, I      },
[ST_CP_UP]         = {  NR,    stop, grant,             cp_up_want_host,   I,          to_cp_down,   refuse_lease, resync },
[ST_CP_UP_TO_HOST] = {  NR,    stop, to_cp_up,          grant,             I,          down_to_host, refuse_lease, resync },
[ST_HOST_DOWN]     = {  NR,    stop, host_down_want_cp, grant,             to_host_up, I,            take_lease,   I      },
[ST_HOST_UP]       = {  NR,    stop, to_host_up_to_cp,  grant,             I,          to_host_down, take_lease,   resync },
[ST_HOST_UP_TO_CP] = {  NR,    stop, grant,             to_host_up,        I,          down_to_cp,   take_lease,   resync },
};

#undef I
#undef NR

static const eh_fsm_state_t s_states[ST_COUNT] = {
    [ST_STOPPED] = { "STOPPED" },
    [ST_CP_DOWN] = { "CP_DOWN" }, [ST_CP_UP] = { "CP_UP" }, [ST_CP_UP_TO_HOST] = { "CP_UP_TO_HOST" },
    [ST_HOST_DOWN] = { "HOST_DOWN" }, [ST_HOST_UP] = { "HOST_UP" },
    [ST_HOST_UP_TO_CP] = { "HOST_UP_TO_CP" },
};

static const char *const s_ev_names[EV_COUNT] = {
    "START", "STOP", "WANT_CP", "WANT_HOST", "LINK_UP", "LINK_DOWN", "HOST_LEASE", "RESYNC",
};

static bool is_host(int st)
{
    return st == ST_HOST_DOWN || st == ST_HOST_UP || st == ST_HOST_UP_TO_CP;
}

static bool is_pending(int st)
{
    return st == ST_CP_UP_TO_HOST || st == ST_HOST_UP_TO_CP;
}

/* Every switch reaches the host, and every start (the split restarted from its
 * Kconfig owner): one asked while associated lands at a disconnect. */
static void on_transit(eh_fsm_t *m, int from, int to)
{
    uint8_t owner;

    (void)m;
    if (to == ST_STOPPED || (from != ST_STOPPED && is_host(from) == is_host(to))) {
        return;
    }
    owner = is_host(to) ? ESP_EXT_CAP_NW_SPLIT_DHCP_HOST : ESP_EXT_CAP_NW_SPLIT_DHCP_CP;
    ESP_LOGI(TAG, "ip owner %s -> %s", from == ST_STOPPED ? "(start)" : is_host(from) ? "HOST" : "CP",
             is_host(to) ? "HOST" : "CP");
    if (esp_event_post(ESP_HOSTED_CP_FEAT_NW_SPLIT_EVENT, ESP_HOSTED_CP_FEAT_NW_SPLIT_EVT_IP_OWNER,
                       &owner, sizeof(owner), 0) != ESP_OK) {
        ESP_LOGE(TAG, "ip owner event lost");
    }
}

static const eh_fsm_def_t s_def = {
    .name = "ip_owner", .n_states = ST_COUNT, .n_events = EV_COUNT,
    .states = s_states, .table = &s_table[0][0], .ev_names = s_ev_names,
    .on_transit = on_transit,
};

/* The mutex and the FSM are created once and live across stop/start. */
static esp_err_t dispatch(int ev, req_t *r, int *state)
{
    static portMUX_TYPE once = portMUX_INITIALIZER_UNLOCKED;

    if (!s_lock) {
        SemaphoreHandle_t l = xSemaphoreCreateMutex();

        if (!l) {
            return ESP_ERR_NO_MEM;
        }
        taskENTER_CRITICAL(&once);
        if (!s_lock) {
            s_lock = l;
            l = NULL;
        }
        taskEXIT_CRITICAL(&once);
        if (l) {
            vSemaphoreDelete(l);
        }
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_fsm.def && eh_fsm_init(&s_fsm, &s_def, ST_STOPPED) != 0) {
        r->rc = ESP_FAIL;
    } else {
        eh_fsm_dispatch(&s_fsm, ev, r);
    }
    if (state) {
        *state = s_fsm.state;
    }
    xSemaphoreGive(s_lock);
    return r->rc;
}

esp_err_t eh_cp_nw_split_ip_owner_fsm_start(esp_netif_t *netif, bool owned, uint8_t owner,
                                            bool host_allowed)
{
    req_t r = { .rc = ESP_OK, .netif = netif, .owned = owned, .owner = owner,
                .host_allowed = host_allowed };

    return dispatch(EV_START, &r, NULL);
}

void eh_cp_nw_split_ip_owner_fsm_stop(void)
{
    req_t r = { .rc = ESP_OK };

    (void)dispatch(EV_STOP, &r, NULL);
}

void eh_cp_nw_split_ip_owner_link(bool up)
{
    req_t r = { .rc = ESP_OK };

    (void)dispatch(up ? EV_LINK_UP : EV_LINK_DOWN, &r, NULL);
}

void eh_cp_nw_split_ip_owner_resync(void)
{
    req_t r = { .rc = ESP_OK };

    (void)dispatch(EV_RESYNC, &r, NULL);
}

uint8_t eh_cp_nw_split_ip_owner(void)
{
    return is_host(s_fsm.state) ? ESP_EXT_CAP_NW_SPLIT_DHCP_HOST : ESP_EXT_CAP_NW_SPLIT_DHCP_CP;
}

esp_err_t eh_cp_nw_split_ip_owner_host_lease(const esp_netif_ip_info_t *ip,
                                             const esp_netif_dns_info_t *dns, uint8_t dns_type)
{
    req_t r = { .rc = ESP_OK, .ip = ip, .dns = dns, .dns_type = dns_type };

    return dispatch(EV_HOST_LEASE, &r, NULL);
}

esp_err_t eh_cp_feat_nw_split_set_ip_owner(uint8_t want, uint8_t *owner, bool *pending)
{
    req_t r = { .rc = ESP_OK };
    esp_err_t rc;
    int st = ST_STOPPED;

    if (want != ESP_EXT_CAP_NW_SPLIT_DHCP_CP && want != ESP_EXT_CAP_NW_SPLIT_DHCP_HOST) {
        return ESP_ERR_INVALID_ARG;
    }
    rc = dispatch(want == ESP_EXT_CAP_NW_SPLIT_DHCP_HOST ? EV_WANT_HOST : EV_WANT_CP, &r, &st);
    if (owner) {
        *owner = is_host(st) ? ESP_EXT_CAP_NW_SPLIT_DHCP_HOST : ESP_EXT_CAP_NW_SPLIT_DHCP_CP;
    }
    if (pending) {
        *pending = is_pending(st);
    }
    ESP_LOGI(TAG, "ip owner request %s: %s, owner %s%s",
             want == ESP_EXT_CAP_NW_SPLIT_DHCP_HOST ? "HOST" : "CP", esp_err_to_name(rc),
             is_host(st) ? "HOST" : "CP", is_pending(st) ? " (switch at next disconnect)" : "");
    return rc;
}
#endif /* EH_CP_FEAT_NW_SPLIT_READY */
