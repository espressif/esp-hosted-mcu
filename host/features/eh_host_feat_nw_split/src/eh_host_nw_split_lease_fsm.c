/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* The IP lease the host and CP share on one STA netif, and who acquires it.
 *   OFF -> WAIT_NETIF -> {CP|HOST}_DOWN -LINK_UP-> *_NO_IP -lease-> *_IP
 * CP_* rows apply the CP's lease; HOST_* rows push the host's to the CP. An owner
 * change waits in *_IP_TO_* while an address is in use. */
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "eh_fsm.h"
#include "eh_host_event.h"
#include "eh_host_feat_nw_split.h"
#include "eh_host_nw_split.h"
#include "eh_host_port_master_config.h"
#include "eh_host_port_sync.h"
#include "eh_host_sta_fsm.h"
#include "eh_host_nw_split_lease_fsm.h"

#define TAG "eh_nw_split"

ESP_EVENT_DEFINE_BASE(EH_HOST_LEASE_FSM_EVENT);

enum {
    ST_OFF, ST_WAIT_NETIF, ST_SPLIT_OFF,
    ST_CP_DOWN, ST_CP_NO_IP, ST_CP_IP, ST_CP_ROAMING, ST_CP_IP_TO_HOST,
    ST_HOST_DOWN, ST_HOST_NO_IP, ST_HOST_IP, ST_HOST_IP_TO_CP,
    ST_COUNT
};
enum {
    EV_START, EV_NETIF, EV_STA_START, EV_STOP, EV_STA_CONNECTED, EV_LINK_UP, EV_LINK_DOWN,
    EV_CP_LEASE, EV_APPLY, EV_HOST_LEASE, EV_PUSH_DONE, EV_OWNER, EV_CP_INIT,
    EV_STA_DISCONNECTED, EV_COUNT
};

typedef struct {
    esp_netif_t                 *netif;
    eh_host_nw_split_ip_owner_t  boot;
    bool                         cp_split;
} start_req_t;

static eh_fsm_t s_fsm;
static eh_host_port_mutex_t *s_mtx;   /* inputs come from the event loop and the app (STOP) */

static struct {
    esp_netif_t         *netif;
    esp_netif_ip_info_t  lease;     /* CP rows: the CP's lease; HOST rows: the one pushed */
    esp_ip_addr_t        dns;       /* CP rows: the CP's main DNS */
    eh_host_nw_split_ip_owner_t boot;
} s_ls;

/* The owner to ask for: the app's (eh_host_nw_split_set_ip_owner), else the netif
 * mode's. UNKNOWN until either sets it. Kept across stop/start. */
static eh_host_nw_split_ip_owner_t s_want;

#define I eh_fsm_ignore
#define X eh_fsm_unsupported

static const char *owner_name(eh_host_nw_split_ip_owner_t o)
{
    return o == EH_HOST_NW_SPLIT_IP_OWNER_HOST ? "HOST" :
           o == EH_HOST_NW_SPLIT_IP_OWNER_CP ? "CP" : "?";
}

static void post(int ev, const void *data, size_t size)
{
    if (esp_event_post(EH_HOST_LEASE_FSM_EVENT, ev, data, size, 0) != ESP_OK) {
        ESP_LOGE(TAG, "lease input %d lost", ev);
    }
}

/* ── the netif's DHCP client ──────────────────────────────────────────────── */
static bool has_client(void)
{
    return esp_netif_get_flags(s_ls.netif) & ESP_NETIF_DHCP_CLIENT;
}

static esp_netif_dhcp_status_t client_status(void)
{
    esp_netif_dhcp_status_t st = ESP_NETIF_DHCP_INIT;

    (void)esp_netif_dhcpc_get_status(s_ls.netif, &st);
    return st;
}

/* esp_netif starts the host client on STA_CONNECTED: stop a started one so it
 * never REQUESTs nor RELEASEs the CP lease (the CP keeps what it sends off the
 * air). Stopping it before esp_netif's handler ran would turn the netif static. */
static void client_stop(void)
{
    if (has_client() && client_status() == ESP_NETIF_DHCP_STARTED) {
        (void)esp_netif_dhcpc_stop(s_ls.netif);
    }
}

/* Re-armed: a stopped client starts on the next connect, or now on an up netif. */
static void client_arm(void)
{
    if (has_client() && client_status() == ESP_NETIF_DHCP_STOPPED) {
        (void)esp_netif_dhcpc_start(s_ls.netif);
    }
}

/* ── asking the CP ────────────────────────────────────────────────────────── */
static void on_owner_answer(esp_err_t rc, uint8_t owner, bool pending)
{
    eh_host_nw_split_ip_owner_evt_t a = {
        .want = s_want, .owner = (eh_host_nw_split_ip_owner_t)owner,
        .pending = pending, .rc = rc,
    };

    post(EV_OWNER, &a, sizeof(a));
}

static void ask(void)
{
    if (eh_host_nw_split_rpc_set_ip_owner_async(s_want, on_owner_answer) != ESP_OK) {
        ESP_LOGE(TAG, "ip owner request not sent");
    }
}

/* The owner the answer settles on; UNKNOWN when it settles nothing yet. */
static eh_host_nw_split_ip_owner_t settled(const eh_host_nw_split_ip_owner_evt_t *a)
{
    if (a->rc == ESP_OK && a->pending) {
        return EH_HOST_NW_SPLIT_IP_OWNER_UNKNOWN;     /* the CP's owner event follows */
    }
    return (a->rc == ESP_OK || a->rc == ESP_ERR_NOT_ALLOWED) ? a->owner :
           EH_HOST_NW_SPLIT_IP_OWNER_UNKNOWN;
}

/* ── CP rows ──────────────────────────────────────────────────────────────── */
static bool lease_valid(void)
{
    return s_ls.lease.ip.addr != 0;
}

static void apply_lease(void)
{
    esp_netif_t *n = s_ls.netif;
    bool was_up = esp_netif_is_netif_up(n);
    esp_netif_ip_info_t old = {0};

    (void)esp_netif_get_old_ip_info(n, &old);
    client_stop();
    if (esp_netif_set_ip_info(n, &s_ls.lease) != ESP_OK) {
        ESP_LOGE(TAG, "set_ip_info " IPSTR " failed", IP2STR(&s_ls.lease.ip));
        return;
    }
    if (s_ls.dns.type == ESP_IPADDR_TYPE_V4 && s_ls.dns.u_addr.ip4.addr != 0) {
        esp_netif_dns_info_t dns = { .ip = s_ls.dns };

        (void)esp_netif_set_dns_info(n, ESP_NETIF_DNS_MAIN, &dns);
    }
    /* esp_netif posts GOT_IP itself for an up netif with EVENT_IP_MODIFIED. */
    if (!was_up || !(esp_netif_get_flags(n) & ESP_NETIF_FLAG_EVENT_IP_MODIFIED)) {
        ip_event_got_ip_t evt = {
            .esp_netif  = n,
            .ip_info    = s_ls.lease,
            .ip_changed = memcmp(&old, &s_ls.lease, sizeof(old)) != 0,
        };

        (void)esp_netif_set_old_ip_info(n, &s_ls.lease);
        if (esp_event_post(IP_EVENT, IP_EVENT_STA_GOT_IP, &evt, sizeof(evt), 0) != ESP_OK) {
            ESP_LOGE(TAG, "GOT_IP post failed");
        }
    }
    ESP_LOGD(TAG, "CP lease on host netif: " IPSTR, IP2STR(&s_ls.lease.ip));
}

/* The lease went away with the station still associated (the CP lost it). */
static void drop_lease(void)
{
    const esp_netif_ip_info_t none = {0};
    ip_event_got_ip_t evt = { .esp_netif = s_ls.netif };

    (void)esp_netif_set_ip_info(s_ls.netif, &none);
    (void)esp_netif_set_old_ip_info(s_ls.netif, &none);
    if (esp_event_post(IP_EVENT, IP_EVENT_STA_LOST_IP, &evt, sizeof(evt), 0) != ESP_OK) {
        ESP_LOGE(TAG, "LOST_IP post failed");
    }
}

/* Link down: back to the pre-association netif, so the next association is new
 * for esp_netif. A netif with a DHCP client gets it re-armed: left stopped, the
 * next STA_CONNECTED would take the netif as static. */
static void park_netif(void)
{
    esp_netif_ip_info_t cur = {0};

    if (esp_netif_is_netif_up(s_ls.netif)) {
        return;
    }
    if (has_client()) {
        client_arm();
        return;
    }
    if (esp_netif_get_ip_info(s_ls.netif, &cur) == ESP_OK && cur.ip.addr != 0) {
        const esp_netif_ip_info_t none = {0};

        (void)esp_netif_set_ip_info(s_ls.netif, &none);
    }
}

/* d: the CP status. Whether it carries a lease. */
static bool store_lease(const eh_host_nw_split_status_t *e)
{
    if (!e->link_up || !e->dhcp_up || !e->has_ipv4 || e->ipv4.ip.addr == 0) {
        memset(&s_ls.lease, 0, sizeof(s_ls.lease));
        return false;
    }
    s_ls.lease = e->ipv4;
    s_ls.dns = e->dns_main;
    return true;
}

/* A lease while not shown as associated: the station FSM learnt it from the RPC
 * handler (LEASE_READY); the address follows its STA_CONNECTED (APPLY). */
static int cp_lease_ready(void)
{
    post(EV_APPLY, NULL, 0);
    return EH_FSM_STAY;
}

static int cp_up(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    client_stop();
    if (!lease_valid()) {
        return ST_CP_NO_IP;
    }
    apply_lease();
    return ST_CP_IP;
}

static int cp_down(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    park_netif();
    return ST_CP_DOWN;
}

static int cp_ip_down(eh_fsm_t *m, const void *d)
{
    if (d && *(const uint8_t *)d == WIFI_REASON_ROAMING) {
        return ST_CP_ROAMING;       /* IDF keeps the address across a roam */
    }
    return cp_down(m, d);
}

/* In esp_netif's disconnect pass: a STA_CONNECTED queued before LINK_DOWN must
 * find the client armed, else it posts the old address as static. */
static int cp_rearm(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    park_netif();
    return EH_FSM_STAY;
}

static int cp_ip_rearm(eh_fsm_t *m, const void *d)
{
    if (d && *(const uint8_t *)d == WIFI_REASON_ROAMING) {
        return EH_FSM_STAY;
    }
    return cp_rearm(m, d);
}

static int cp_dhcp_off(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    client_stop();
    return EH_FSM_STAY;
}

static int cp_lease_stored(eh_fsm_t *m, const void *d)
{
    (void)m;
    return store_lease(d) ? cp_lease_ready() : EH_FSM_STAY;
}

static int cp_no_ip_apply(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    if (!lease_valid()) {
        return EH_FSM_STAY;
    }
    apply_lease();
    return ST_CP_IP;
}

static int cp_ip_lease(eh_fsm_t *m, const void *d)
{
    const eh_host_nw_split_status_t *e = d;
    esp_netif_ip_info_t cur = s_ls.lease;

    (void)m;
    if (!e->link_up) {              /* the CP's own link drop: the address goes with LINK_DOWN */
        memset(&s_ls.lease, 0, sizeof(s_ls.lease));
        return EH_FSM_STAY;
    }
    if (!store_lease(e)) {
        drop_lease();               /* lease lost, station still associated */
        return ST_CP_NO_IP;
    }
    if (memcmp(&cur, &s_ls.lease, sizeof(cur)) != 0) {
        apply_lease();              /* another address: GOT_IP, ip_changed */
    }
    return EH_FSM_STAY;
}

/* Roaming, link down: an address change still lands; a lost lease ends the roam. */
static int cp_roam_lease(eh_fsm_t *m, const void *d)
{
    const eh_host_nw_split_status_t *e = d;

    if (!e->link_up) {
        memset(&s_ls.lease, 0, sizeof(s_ls.lease));
        return EH_FSM_STAY;
    }
    if (!store_lease(e)) {
        return cp_down(m, d);
    }
    return cp_lease_ready();        /* CONNECTED first, the address after it (APPLY) */
}

/* The roam ended with a lease: a changed address lands as GOT_IP, ip_changed. */
static int cp_roam_apply(eh_fsm_t *m, const void *d)
{
    esp_netif_ip_info_t cur = {0};

    (void)m;
    (void)d;
    if (!lease_valid()) {
        return EH_FSM_STAY;
    }
    if (esp_netif_get_ip_info(s_ls.netif, &cur) != ESP_OK ||
        memcmp(&cur, &s_ls.lease, sizeof(cur)) != 0) {
        apply_lease();
    }
    return ST_CP_IP;
}

static int cp_roam_back(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    return ST_CP_IP;                /* back from a roam, the address kept */
}

/* ── HOST rows ────────────────────────────────────────────────────────────── */
static void on_push_done(esp_err_t rc)
{
    post(EV_PUSH_DONE, &rc, sizeof(rc));
}

/* Give the CP stack the host lease (none = 0.0.0.0), so it answers ARP for the
 * shared address. */
static void push(const esp_netif_ip_info_t *lease)
{
    eh_host_nw_split_status_t st = {
        .link_up = true, .dhcp_up = lease->ip.addr != 0,
        .has_ipv4 = lease->ip.addr != 0, .ipv4 = *lease,
    };
    esp_netif_dns_info_t d = {0};

    if (st.has_ipv4 &&
        esp_netif_get_dns_info(s_ls.netif, ESP_NETIF_DNS_MAIN, &d) == ESP_OK) {
        st.dns_main = d.ip;
    }
    if (eh_host_nw_split_set_status_async(&st, on_push_done) != ESP_OK) {
        ESP_LOGE(TAG, "host lease " IPSTR " not sent to the CP", IP2STR(&lease->ip));
        return;
    }
    s_ls.lease = *lease;
}

/* A new association: tell the CP "no lease" until the host has one. */
static int host_up(eh_fsm_t *m, const void *d)
{
    static const esp_netif_ip_info_t none;

    (void)m;
    (void)d;
    push(&none);
    return ST_HOST_NO_IP;
}

static int host_down(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    return ST_HOST_DOWN;
}

/* d: the host's address, all zero when lost. */
static int host_lease(eh_fsm_t *m, const void *d)
{
    const esp_netif_ip_info_t *ip = d;

    (void)m;
    if (memcmp(ip, &s_ls.lease, sizeof(*ip)) != 0) {
        push(ip);
    }
    return ip->ip.addr ? ST_HOST_IP : ST_HOST_NO_IP;
}

/* A static address set while down still goes to the CP. */
static int host_down_lease(eh_fsm_t *m, const void *d)
{
    (void)host_lease(m, d);
    return EH_FSM_STAY;
}

static int host_ip_to_cp_lease(eh_fsm_t *m, const void *d)
{
    const esp_netif_ip_info_t *ip = d;

    (void)m;
    if (memcmp(ip, &s_ls.lease, sizeof(*ip)) != 0) {
        push(ip);
    }
    return EH_FSM_STAY;
}

/* The CP refused the lease: it no longer has the host as owner (a CP boot). Ask. */
static int push_done(eh_fsm_t *m, const void *d)
{
    esp_err_t rc = *(const esp_err_t *)d;

    (void)m;
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "CP refused the host lease (0x%x): asking for ip owner %s again",
                 rc, owner_name(s_want));
        ask();
    }
    return EH_FSM_STAY;
}

/* ── owner answers / events, per row ──────────────────────────────────────── */
static int owner_cp_down(eh_fsm_t *m, const void *d)
{
    (void)m;
    if (settled(d) != EH_HOST_NW_SPLIT_IP_OWNER_HOST) {
        return EH_FSM_STAY;
    }
    memset(&s_ls.lease, 0, sizeof(s_ls.lease));
    client_arm();
    return ST_HOST_DOWN;
}

/* Associated, no address in use: the host client is unbound (stopped). */
static int owner_cp_no_ip(eh_fsm_t *m, const void *d)
{
    (void)m;
    if (settled(d) != EH_HOST_NW_SPLIT_IP_OWNER_HOST) {
        return EH_FSM_STAY;
    }
    memset(&s_ls.lease, 0, sizeof(s_ls.lease));
    client_arm();                   /* starts now: the netif is up */
    return host_up(m, d);
}

static int owner_cp_ip(eh_fsm_t *m, const void *d)
{
    (void)m;
    return settled(d) == EH_HOST_NW_SPLIT_IP_OWNER_HOST ? ST_CP_IP_TO_HOST : EH_FSM_STAY;
}

static int owner_cp_roaming(eh_fsm_t *m, const void *d)
{
    if (settled(d) != EH_HOST_NW_SPLIT_IP_OWNER_HOST) {
        return EH_FSM_STAY;
    }
    (void)cp_down(m, d);
    return owner_cp_down(m, d);
}

static int owner_cp_ip_to_host(eh_fsm_t *m, const void *d)
{
    (void)m;
    return settled(d) == EH_HOST_NW_SPLIT_IP_OWNER_CP ? ST_CP_IP : EH_FSM_STAY;
}

static int owner_host_down(eh_fsm_t *m, const void *d)
{
    (void)m;
    if (settled(d) != EH_HOST_NW_SPLIT_IP_OWNER_CP) {
        return EH_FSM_STAY;
    }
    memset(&s_ls.lease, 0, sizeof(s_ls.lease));
    return ST_CP_DOWN;
}

/* Associated, no address yet: the client is unbound (mid-DORA at most). */
static int owner_host_no_ip(eh_fsm_t *m, const void *d)
{
    (void)m;
    if (settled(d) != EH_HOST_NW_SPLIT_IP_OWNER_CP) {
        return EH_FSM_STAY;
    }
    client_stop();
    memset(&s_ls.lease, 0, sizeof(s_ls.lease));
    return ST_CP_NO_IP;
}

static int owner_host_ip(eh_fsm_t *m, const void *d)
{
    (void)m;
    return settled(d) == EH_HOST_NW_SPLIT_IP_OWNER_CP ? ST_HOST_IP_TO_CP : EH_FSM_STAY;
}

static int owner_host_ip_to_cp(eh_fsm_t *m, const void *d)
{
    (void)m;
    return settled(d) == EH_HOST_NW_SPLIT_IP_OWNER_HOST ? ST_HOST_IP : EH_FSM_STAY;
}

/* Switching to the host: a lost lease frees the address now, the host acquires it. */
static int cp_ip_to_host_lease(eh_fsm_t *m, const void *d)
{
    const eh_host_nw_split_status_t *e = d;

    if (!e->link_up || store_lease(e)) {
        return cp_ip_lease(m, d);
    }
    drop_lease();
    client_arm();
    return host_up(m, d);
}

/* The switch waited for the address to go with the link. */
static int to_host_down(eh_fsm_t *m, const void *d)
{
    (void)cp_down(m, d);
    memset(&s_ls.lease, 0, sizeof(s_ls.lease));
    client_arm();
    return ST_HOST_DOWN;
}

static int to_cp_down(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    memset(&s_ls.lease, 0, sizeof(s_ls.lease));
    return ST_CP_DOWN;
}

static int cp_init(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    ask();                          /* a CP connection starts from its Kconfig owner */
    return EH_FSM_STAY;
}

/* ── lifecycle ────────────────────────────────────────────────────────────── */
static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data);

/* After esp_netif's WIFI_EVENT handlers, which run in registration order. */
static const int32_t s_link_ids[] = {
    WIFI_EVENT_STA_CONNECTED, WIFI_EVENT_STA_DISCONNECTED, WIFI_EVENT_STA_STOP,
};

static int with_netif(esp_netif_t *n)
{
    for (size_t i = 0; i < sizeof(s_link_ids) / sizeof(s_link_ids[0]); i++) {
        if (esp_event_handler_register(WIFI_EVENT, s_link_ids[i], on_wifi, NULL) != ESP_OK) {
            ESP_LOGE(TAG, "WIFI_EVENT %d handler not registered", (int)s_link_ids[i]);
        }
    }
    s_ls.netif = n;
    memset(&s_ls.lease, 0, sizeof(s_ls.lease));
    ESP_LOGI(TAG, "ip owner %s (CP boot owner), asking for %s",
             owner_name(s_ls.boot), owner_name(s_want));
    ask();
    return s_ls.boot == EH_HOST_NW_SPLIT_IP_OWNER_HOST ? ST_HOST_DOWN : ST_CP_DOWN;
}

static int start(eh_fsm_t *m, const void *d)
{
    const start_req_t *r = d;
    esp_netif_t *n = NULL;

    (void)m;
    s_ls.boot = r->boot;
    if (!r->cp_split) {
        eh_host_nw_split_ip_owner_evt_t e = {
            .want = s_want, .owner = EH_HOST_NW_SPLIT_IP_OWNER_HOST, .rc = ESP_ERR_NOT_SUPPORTED,
        };

        s_ls.netif = r->netif;
        ESP_LOGE(TAG, "CP has no network split (CONFIG_ESP_HOSTED_CP_FEAT_NW_SPLIT=n): "
                 "the host netif runs its own DHCP client%s",
                 (r->netif && (esp_netif_get_flags(r->netif) & ESP_NETIF_DHCP_CLIENT)) ? "" :
                 ", and this netif has none: no IP");
        (void)esp_event_post(EH_HOST_EVENT, EH_HOST_EVENT_NW_SPLIT_IP_OWNER, &e, sizeof(e), 0);
        return ST_SPLIT_OFF;
    }
    if (!r->netif && !(n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"))) {
        ESP_LOGI(TAG, "APP_EXTERNAL: waiting for the app's WIFI_STA_DEF");
        return ST_WAIT_NETIF;
    }
    return with_netif(r->netif ? r->netif : n);
}

static int netif_ready(eh_fsm_t *m, const void *d)
{
    (void)m;
    ESP_LOGI(TAG, "WIFI_STA_DEF from the app, used as is");
    return with_netif(*(esp_netif_t *const *)d);
}

/* APP_EXTERNAL: the app's WIFI_STA_DEF must exist by Wi-Fi start. */
static int sta_start_waiting(eh_fsm_t *m, const void *d)
{
    esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    (void)m;
    (void)d;
    if (n) {
        ESP_LOGI(TAG, "WIFI_STA_DEF from the app, used as is");
        return with_netif(n);
    }
    ESP_LOGE(TAG, "APP_EXTERNAL: no WIFI_STA_DEF at Wi-Fi start: the app must "
             "create it (esp_netif_create_default_wifi_sta)");
    return EH_FSM_STAY;
}

static int stop(eh_fsm_t *m, const void *d)
{
    (void)m;
    (void)d;
    s_ls.netif = NULL;
    return ST_OFF;
}

/* ── the table ────────────────────────────────────────────────────────────── */
_Static_assert(EV_COUNT == 14, "one column per event");

static const eh_fsm_fn_t s_table[ST_COUNT][EV_COUNT] = {
[ST_OFF] = {
    [EV_START] = start,          [EV_NETIF] = I,             [EV_STA_START] = I,
    [EV_STOP] = I,               [EV_STA_CONNECTED] = I,     [EV_LINK_UP] = I,
    [EV_LINK_DOWN] = I,          [EV_CP_LEASE] = I,          [EV_APPLY] = I,
    [EV_HOST_LEASE] = I,         [EV_PUSH_DONE] = I,         [EV_OWNER] = I,
    [EV_CP_INIT] = I,
    [EV_STA_DISCONNECTED] = I },
[ST_WAIT_NETIF] = {
    [EV_START] = X,              [EV_NETIF] = netif_ready,   [EV_STA_START] = sta_start_waiting,
    [EV_STOP] = stop,            [EV_STA_CONNECTED] = I,     [EV_LINK_UP] = I,
    [EV_LINK_DOWN] = I,          [EV_CP_LEASE] = I,          [EV_APPLY] = I,
    [EV_HOST_LEASE] = I,         [EV_PUSH_DONE] = I,         [EV_OWNER] = I,
    [EV_CP_INIT] = I,
    [EV_STA_DISCONNECTED] = I },
[ST_SPLIT_OFF] = {
    [EV_START] = X,              [EV_NETIF] = I,             [EV_STA_START] = I,
    [EV_STOP] = stop,            [EV_STA_CONNECTED] = I,     [EV_LINK_UP] = I,
    [EV_LINK_DOWN] = I,          [EV_CP_LEASE] = I,          [EV_APPLY] = I,
    [EV_HOST_LEASE] = I,         [EV_PUSH_DONE] = I,         [EV_OWNER] = I,
    [EV_CP_INIT] = I,
    [EV_STA_DISCONNECTED] = I },
[ST_CP_DOWN] = {
    [EV_START] = X,              [EV_NETIF] = I,             [EV_STA_START] = I,
    [EV_STOP] = stop,            [EV_STA_CONNECTED] = cp_dhcp_off, [EV_LINK_UP] = cp_up,
    [EV_LINK_DOWN] = cp_down,    [EV_CP_LEASE] = cp_lease_stored, [EV_APPLY] = I,
    [EV_HOST_LEASE] = I,         [EV_PUSH_DONE] = I,         [EV_OWNER] = owner_cp_down,
    [EV_CP_INIT] = cp_init,
    [EV_STA_DISCONNECTED] = cp_rearm },
[ST_CP_NO_IP] = {
    [EV_START] = X,              [EV_NETIF] = I,             [EV_STA_START] = I,
    [EV_STOP] = stop,            [EV_STA_CONNECTED] = cp_dhcp_off, [EV_LINK_UP] = cp_dhcp_off,
    [EV_LINK_DOWN] = cp_down,    [EV_CP_LEASE] = cp_lease_stored, [EV_APPLY] = cp_no_ip_apply,
    [EV_HOST_LEASE] = I,         [EV_PUSH_DONE] = I,         [EV_OWNER] = owner_cp_no_ip,
    [EV_CP_INIT] = cp_init,
    [EV_STA_DISCONNECTED] = cp_rearm },
[ST_CP_IP] = {
    [EV_START] = X,              [EV_NETIF] = I,             [EV_STA_START] = I,
    [EV_STOP] = stop,            [EV_STA_CONNECTED] = cp_dhcp_off, [EV_LINK_UP] = I,
    [EV_LINK_DOWN] = cp_ip_down, [EV_CP_LEASE] = cp_ip_lease, [EV_APPLY] = I,
    [EV_HOST_LEASE] = I,         [EV_PUSH_DONE] = I,         [EV_OWNER] = owner_cp_ip,
    [EV_CP_INIT] = cp_init,
    [EV_STA_DISCONNECTED] = cp_ip_rearm },
[ST_CP_ROAMING] = {
    [EV_START] = X,              [EV_NETIF] = I,             [EV_STA_START] = I,
    [EV_STOP] = stop,            [EV_STA_CONNECTED] = cp_dhcp_off, [EV_LINK_UP] = cp_roam_back,
    [EV_LINK_DOWN] = cp_down,    [EV_CP_LEASE] = cp_roam_lease, [EV_APPLY] = cp_roam_apply,
    [EV_HOST_LEASE] = I,         [EV_PUSH_DONE] = I,         [EV_OWNER] = owner_cp_roaming,
    [EV_CP_INIT] = cp_init,
    [EV_STA_DISCONNECTED] = cp_rearm },
[ST_CP_IP_TO_HOST] = {
    [EV_START] = X,              [EV_NETIF] = I,             [EV_STA_START] = I,
    [EV_STOP] = stop,            [EV_STA_CONNECTED] = cp_dhcp_off, [EV_LINK_UP] = I,
    [EV_LINK_DOWN] = to_host_down, [EV_CP_LEASE] = cp_ip_to_host_lease, [EV_APPLY] = I,
    [EV_HOST_LEASE] = I,         [EV_PUSH_DONE] = I,         [EV_OWNER] = owner_cp_ip_to_host,
    [EV_CP_INIT] = cp_init,
    [EV_STA_DISCONNECTED] = cp_rearm },
[ST_HOST_DOWN] = {
    [EV_START] = X,              [EV_NETIF] = I,             [EV_STA_START] = I,
    [EV_STOP] = stop,            [EV_STA_CONNECTED] = I,     [EV_LINK_UP] = host_up,
    [EV_LINK_DOWN] = I,          [EV_CP_LEASE] = I,          [EV_APPLY] = I,
    [EV_HOST_LEASE] = host_down_lease, [EV_PUSH_DONE] = push_done, [EV_OWNER] = owner_host_down,
    [EV_CP_INIT] = cp_init,
    [EV_STA_DISCONNECTED] = I },
[ST_HOST_NO_IP] = {
    [EV_START] = X,              [EV_NETIF] = I,             [EV_STA_START] = I,
    [EV_STOP] = stop,            [EV_STA_CONNECTED] = I,     [EV_LINK_UP] = host_up,
    [EV_LINK_DOWN] = host_down,  [EV_CP_LEASE] = I,          [EV_APPLY] = I,
    [EV_HOST_LEASE] = host_lease, [EV_PUSH_DONE] = push_done, [EV_OWNER] = owner_host_no_ip,
    [EV_CP_INIT] = cp_init,
    [EV_STA_DISCONNECTED] = I },
[ST_HOST_IP] = {
    [EV_START] = X,              [EV_NETIF] = I,             [EV_STA_START] = I,
    [EV_STOP] = stop,            [EV_STA_CONNECTED] = I,     [EV_LINK_UP] = host_up,
    [EV_LINK_DOWN] = host_down,  [EV_CP_LEASE] = I,          [EV_APPLY] = I,
    [EV_HOST_LEASE] = host_lease, [EV_PUSH_DONE] = push_done, [EV_OWNER] = owner_host_ip,
    [EV_CP_INIT] = cp_init,
    [EV_STA_DISCONNECTED] = I },
[ST_HOST_IP_TO_CP] = {
    [EV_START] = X,              [EV_NETIF] = I,             [EV_STA_START] = I,
    [EV_STOP] = stop,            [EV_STA_CONNECTED] = cp_dhcp_off, [EV_LINK_UP] = I,
    [EV_LINK_DOWN] = to_cp_down, [EV_CP_LEASE] = I,          [EV_APPLY] = I,
    [EV_HOST_LEASE] = host_ip_to_cp_lease, [EV_PUSH_DONE] = push_done, [EV_OWNER] = owner_host_ip_to_cp,
    [EV_CP_INIT] = cp_init,
    [EV_STA_DISCONNECTED] = I },
};

#undef I
#undef X

static const eh_fsm_state_t s_states[ST_COUNT] = {
    [ST_OFF] = { "OFF" }, [ST_WAIT_NETIF] = { "WAIT_NETIF" }, [ST_SPLIT_OFF] = { "SPLIT_OFF" },
    [ST_CP_DOWN] = { "CP_DOWN" }, [ST_CP_NO_IP] = { "CP_NO_IP" }, [ST_CP_IP] = { "CP_IP" },
    [ST_CP_ROAMING] = { "CP_ROAMING" }, [ST_CP_IP_TO_HOST] = { "CP_IP_TO_HOST" },
    [ST_HOST_DOWN] = { "HOST_DOWN" }, [ST_HOST_NO_IP] = { "HOST_NO_IP" },
    [ST_HOST_IP] = { "HOST_IP" }, [ST_HOST_IP_TO_CP] = { "HOST_IP_TO_CP" },
};

static const char *const s_ev_names[EV_COUNT] = {
    "START", "NETIF", "STA_START", "STOP", "STA_CONNECTED", "LINK_UP", "LINK_DOWN",
    "CP_LEASE", "APPLY", "HOST_LEASE", "PUSH_DONE", "OWNER", "CP_INIT", "STA_DISCONNECTED",
};

static bool host_row(int st)
{
    return st >= ST_HOST_DOWN && st <= ST_HOST_IP_TO_CP;
}

static bool owner_row(int st)
{
    return st >= ST_CP_DOWN;
}

static void on_transit(eh_fsm_t *m, int from, int to)
{
    (void)m;
    if (owner_row(from) && owner_row(to) && host_row(from) != host_row(to)) {
        ESP_LOGI(TAG, "ip owner %s -> %s", host_row(from) ? "HOST" : "CP",
                 host_row(to) ? "HOST" : "CP");
    }
}

static const eh_fsm_def_t s_def = {
    .name = "lease", .n_states = ST_COUNT, .n_events = EV_COUNT,
    .states = s_states, .table = &s_table[0][0], .ev_names = s_ev_names,
    .on_transit = on_transit,
};

/* ── inputs ───────────────────────────────────────────────────────────────── */
static void dispatch(int ev, const void *data)
{
    eh_host_port_mutex_lock(s_mtx);
    if (s_fsm.def) {
        eh_fsm_dispatch(&s_fsm, ev, data);
    }
    eh_host_port_mutex_unlock(s_mtx);
}

static void on_input(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == EV_OWNER) {
        const eh_host_nw_split_ip_owner_evt_t *a = data;

        if (a->rc == ESP_ERR_NOT_ALLOWED) {
            ESP_LOGE(TAG, "CP refused ip owner %s: the IP stays with %s",
                     owner_name(a->want), owner_name(a->owner));
        } else if (a->rc == ESP_OK) {
            ESP_LOGI(TAG, "ip owner %s granted%s", owner_name(a->pending ? a->want : a->owner),
                     a->pending ? " at the next disconnect" : "");
        } else {
            ESP_LOGE(TAG, "ip owner request failed (0x%x)", a->rc);
        }
        (void)esp_event_post(EH_HOST_EVENT, EH_HOST_EVENT_NW_SPLIT_IP_OWNER, a, sizeof(*a), 0);
    }
    dispatch((int)id, data);
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    uint8_t reason = 0;

    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        post(EV_STA_START, NULL, 0);
        return;
    }
    if (id == WIFI_EVENT_STA_CONNECTED) {
        dispatch(EV_STA_CONNECTED, NULL);    /* now, after esp_netif's start */
        post(EV_LINK_UP, NULL, 0);
        return;
    }
    if (id == WIFI_EVENT_STA_DISCONNECTED && data) {
        reason = ((const wifi_event_sta_disconnected_t *)data)->reason;
    }
    dispatch(EV_STA_DISCONNECTED, &reason);    /* now, after esp_netif's own handler */
    post(EV_LINK_DOWN, &reason, sizeof(reason));
}

static void on_cp_status(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    if (data) {
        post(EV_CP_LEASE, data, sizeof(eh_host_nw_split_status_t));
    }
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const ip_event_got_ip_t *evt = data;
    esp_netif_ip_info_t ip = {0};

    (void)arg;
    (void)base;
    if (!evt || evt->esp_netif != s_ls.netif) {
        return;
    }
    if (id == IP_EVENT_STA_GOT_IP) {
        ip = evt->ip_info;
    }
    post(EV_HOST_LEASE, &ip, sizeof(ip));
}

static void on_cp_init(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    post(EV_CP_INIT, NULL, 0);
}


esp_err_t eh_host_nw_split_lease_fsm_start(esp_netif_t *netif, eh_host_nw_split_ip_owner_t boot_owner,
                                          eh_host_nw_split_ip_owner_t want, bool cp_split)
{
    esp_err_t rc = ESP_OK;
    start_req_t r = { .netif = netif, .boot = boot_owner, .cp_split = cp_split };

    if (s_want == EH_HOST_NW_SPLIT_IP_OWNER_UNKNOWN) {
        s_want = want;
    }
    if (!s_mtx && !(s_mtx = eh_host_port_mutex_create())) {
        return ESP_ERR_NO_MEM;
    }
    eh_host_port_mutex_lock(s_mtx);
    if (!s_fsm.def && eh_fsm_init(&s_fsm, &s_def, ST_OFF) != 0) {
        rc = ESP_FAIL;
    }
    eh_host_port_mutex_unlock(s_mtx);
    if (rc != ESP_OK) {
        return rc;
    }
    rc |= esp_event_handler_register(EH_HOST_LEASE_FSM_EVENT, ESP_EVENT_ANY_ID, on_input, NULL);
    rc |= esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, on_wifi, NULL);
    rc |= esp_event_handler_register(EH_HOST_EVENT, EH_HOST_EVENT_NW_SPLIT_STATUS, on_cp_status, NULL);
    rc |= esp_event_handler_register(EH_HOST_EVENT, EH_HOST_EVENT_CP_INIT, on_cp_init, NULL);
    rc |= esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL);
    rc |= esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_ip, NULL);
    post(EV_START, &r, sizeof(r));
    return rc;
}

void eh_host_nw_split_lease_fsm_netif(esp_netif_t *netif)
{
    post(EV_NETIF, &netif, sizeof(netif));
}

void eh_host_nw_split_lease_fsm_cp_owner(eh_host_nw_split_ip_owner_t owner)
{
    eh_host_nw_split_ip_owner_evt_t e = { .want = s_want, .owner = owner, .rc = ESP_OK };

    post(EV_OWNER, &e, sizeof(e));
    if (s_want != EH_HOST_NW_SPLIT_IP_OWNER_UNKNOWN && owner != s_want) {
        ask();                      /* the CP restarted its split from its Kconfig owner */
    }
}

void eh_host_nw_split_lease_fsm_stop(void)
{
    (void)esp_event_handler_unregister(EH_HOST_LEASE_FSM_EVENT, ESP_EVENT_ANY_ID, on_input);
    (void)esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_START, on_wifi);
    for (size_t i = 0; i < sizeof(s_link_ids) / sizeof(s_link_ids[0]); i++) {
        (void)esp_event_handler_unregister(WIFI_EVENT, s_link_ids[i], on_wifi);
    }
    (void)esp_event_handler_unregister(EH_HOST_EVENT, EH_HOST_EVENT_NW_SPLIT_STATUS, on_cp_status);
    (void)esp_event_handler_unregister(EH_HOST_EVENT, EH_HOST_EVENT_CP_INIT, on_cp_init);
    (void)esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip);
    (void)esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_LOST_IP, on_ip);
    if (s_mtx) {
        dispatch(EV_STOP, NULL);
    }
}

esp_err_t eh_host_nw_split_set_ip_owner(eh_host_nw_split_ip_owner_t want,
                                        eh_host_nw_split_ip_owner_t *owner, bool *pending)
{
    uint8_t got = 0;
    bool later = false;

    if (want != EH_HOST_NW_SPLIT_IP_OWNER_CP && want != EH_HOST_NW_SPLIT_IP_OWNER_HOST) {
        return ESP_ERR_INVALID_ARG;
    }
    s_want = want;
    if (s_fsm.def && s_fsm.state == ST_SPLIT_OFF) {
        if (owner) {
            *owner = EH_HOST_NW_SPLIT_IP_OWNER_HOST;
        }
        return ESP_ERR_NOT_SUPPORTED;   /* no split on the CP: the host owns it */
    }
    if (!s_fsm.def || s_fsm.state == ST_OFF || s_fsm.state == ST_WAIT_NETIF) {
        if (owner) {
            *owner = EH_HOST_NW_SPLIT_IP_OWNER_UNKNOWN;
        }
        return ESP_ERR_NOT_FINISHED;    /* asked once the lease FSM runs */
    }
    esp_err_t rc = eh_host_nw_split_rpc_set_ip_owner(want, &got, &later);

    on_owner_answer(rc, got, later);
    if (owner) {
        *owner = (eh_host_nw_split_ip_owner_t)got;
    }
    if (pending) {
        *pending = later;
    }
    return rc;
}

eh_host_nw_split_ip_owner_t eh_host_nw_split_get_ip_owner(void)
{
    int st = s_fsm.state;

    if (s_fsm.def && st == ST_SPLIT_OFF) {
        return EH_HOST_NW_SPLIT_IP_OWNER_HOST;
    }
    if (!s_fsm.def || !owner_row(st)) {
        return EH_HOST_NW_SPLIT_IP_OWNER_UNKNOWN;
    }
    return host_row(st) ? EH_HOST_NW_SPLIT_IP_OWNER_HOST : EH_HOST_NW_SPLIT_IP_OWNER_CP;
}
