/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Network-Split host feature: configurable STA netif ownership. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#ifdef ESP_PLATFORM
#include "lwip/netif.h"   /* netif->input peek — IDF/lwip only */
#endif
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_netif.h"
#include "eh_host_feat_nw_split.h"
#include "eh_host_nw_split.h"
#include "eh_host_port_master_config.h"
#if EH_HOST_AUTO_FEAT_INIT
#include "eh_host_auto_init.h"
#endif
#include "eh_host_event.h"
#include "eh_host_wifi.h"
#include "eh_host_wifi_priv.h"

#define NW_SPLIT_TAG "eh_nw_split"

static struct {
    bool         initialised;
    bool         owns_netif;
    bool         custom_netif;
    esp_netif_t *netif;
    bool         status_pending;   /* CP status arrived before the netif was re-added (wake) */
    eh_host_nw_split_status_t pending;
} s_state;

#if CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_CONNECT_ON_START
static void on_sta_start(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    esp_err_t rc = esp_wifi_connect();
    ESP_LOGD(NW_SPLIT_TAG, "auto-connect on STA_START rc=%d", (int)rc);
}
#endif

#if CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_RECONNECT_ON_DISCONNECT
static void on_sta_disconnected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    esp_err_t rc = esp_wifi_connect();
    ESP_LOGD(NW_SPLIT_TAG, "auto-reconnect on STA_DISCONNECTED rc=%d", (int)rc);
}
#endif

#if !CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_DHCP
static bool nw_split_netif_added(void)
{
#ifdef ESP_PLATFORM
    struct netif *impl = (struct netif *)esp_netif_get_netif_impl(s_state.netif);
    return impl && impl->input;
#else
    return esp_netif_get_netif_impl(s_state.netif) != NULL;
#endif
}

static void synthesize_sta_connected(void)
{
    if (eh_host_wifi_rx_admitted(false /*is_ap*/)) {
        return;   /* the CP already relayed a real StaConnected */
    }

    /* Restore the connected event when the CP only reports the IP status. */
    wifi_event_sta_connected_t conn_evt = {0};

    (void)esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                         &conn_evt, sizeof(conn_evt), 0);

    ESP_LOGD(NW_SPLIT_TAG,
             "synthesized STA_CONNECTED (CP gave IP without StaConnected)");

    eh_host_wifi_admit_rx(false /*is_ap*/, true);
}

static void apply_cp_ip_status(const eh_host_nw_split_status_t *e)
{
    bool was_up = esp_netif_is_netif_up(s_state.netif);
    esp_netif_ip_info_t old = {0};
    (void)esp_netif_get_old_ip_info(s_state.netif, &old);

    esp_err_t rc = esp_netif_dhcpc_stop(s_state.netif);

    if (rc != ESP_OK && rc != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(NW_SPLIT_TAG, "dhcpc_stop failed: 0x%x", rc);
    }

    if (esp_netif_set_ip_info(s_state.netif, &e->ipv4) != ESP_OK) {
        ESP_LOGE(NW_SPLIT_TAG, "set_ip_info failed");
        return;
    }

    if (e->dns_main.type == ESP_IPADDR_TYPE_V4 &&
        e->dns_main.u_addr.ip4.addr != 0) {
        esp_netif_dns_info_t dns = { .ip = e->dns_main };

        rc = esp_netif_set_dns_info(s_state.netif, ESP_NETIF_DNS_MAIN, &dns);
        if (rc != ESP_OK) {
            ESP_LOGW(NW_SPLIT_TAG, "set_dns_info failed: 0x%x", rc);
        }
    }

    /* esp_netif posts GOT_IP itself:
     *   netif up       -> set_ip_info posts it (EVENT_IP_MODIFIED)
     *   netif not up   -> action_connected posts it on the pending STA_CONNECTED,
     *                     for a netif with a stopped DHCP client
     * Post it here only when neither applies. */
    uint32_t flags = (uint32_t)esp_netif_get_flags(s_state.netif);
    bool netif_posts = was_up ? (flags & ESP_NETIF_FLAG_EVENT_IP_MODIFIED) != 0
                              : (flags & ESP_NETIF_DHCP_CLIENT) != 0;

    if (!netif_posts) {
        ip_event_got_ip_t evt = {
            .esp_netif  = s_state.netif,
            .ip_info    = e->ipv4,
            .ip_changed = memcmp(&old, &e->ipv4, sizeof(old)) != 0,
        };

        (void)esp_netif_set_old_ip_info(s_state.netif, &e->ipv4);
        rc = esp_event_post(IP_EVENT, IP_EVENT_STA_GOT_IP, &evt, sizeof(evt), 0);
        if (rc != ESP_OK) {
            ESP_LOGW(NW_SPLIT_TAG, "GOT_IP post failed: 0x%x", rc);
        }
    }

    ESP_LOGD(NW_SPLIT_TAG, "CP IP applied to host netif: " IPSTR,
             IP2STR(&e->ipv4.ip));
}

/* The CP lease is applied as a static address, and esp_netif keeps a static
 * address across a link drop, so a reconnect to the same lease would look like a
 * replay. Return the netif to its pre-association state instead:
 *   - DHCP client flag (APP_EXTERNAL): a client stopped by on_sta_connected is
 *     re-armed (dhcpc_start on a down netif clears the address and sends
 *     nothing), even when no lease came; on_sta_connected stops it again before
 *     it can bind. Left stopped, esp_netif's next connect logs "invalid static ip".
 *   - no DHCP client flag: clear the address.
 * While the netif is still up, wait for STA_DISCONNECTED to take it down first:
 * clearing earlier hides the address drop that arms esp_netif's LOST_IP timer. */
static void nw_split_link_down(void)
{
    if (esp_netif_is_netif_up(s_state.netif)) {
        return;
    }
    if (esp_netif_get_flags(s_state.netif) & ESP_NETIF_DHCP_CLIENT) {
        esp_netif_dhcp_status_t st = ESP_NETIF_DHCP_INIT;

        if (esp_netif_dhcpc_get_status(s_state.netif, &st) == ESP_OK &&
            st == ESP_NETIF_DHCP_STOPPED) {
            (void)esp_netif_dhcpc_start(s_state.netif);
        }
        return;
    }

    esp_netif_ip_info_t cur = {0};

    if (esp_netif_get_ip_info(s_state.netif, &cur) == ESP_OK && cur.ip.addr != 0) {
        esp_netif_ip_info_t none = {0};
        (void)esp_netif_set_ip_info(s_state.netif, &none);
    }
}

/* The CP runs the only DHCP client for the shared MAC. esp_netif starts the host
 * client on STA_CONNECTED (registered before this handler); stop it while it is
 * still unbound, so it never sends a REQUEST and never RELEASEs the CP lease. */
static void on_nw_split_sta_connected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    if (s_state.netif && (esp_netif_get_flags(s_state.netif) & ESP_NETIF_DHCP_CLIENT)) {
        (void)esp_netif_dhcpc_stop(s_state.netif);
    }
}

static void on_nw_split_link_down(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    if (s_state.netif) {
        nw_split_link_down();
    }
}

static void on_cp_dhcp_status(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;

    if (!data || !s_state.netif) {
        return;
    }

    const eh_host_nw_split_status_t *e =
        (const eh_host_nw_split_status_t *)data;

    if (!e->dhcp_up || !e->has_ipv4 || e->ipv4.ip.addr == 0) {
        nw_split_link_down();
        return;
    }

    /* The netif is added on STA_START, which the app sees only after its
     * esp_wifi_start(). A status that arrives earlier (CP replay on a host wake)
     * waits here; on_nw_split_sta_start applies it. */
    if (!nw_split_netif_added()) {
        s_state.pending = *e;
        s_state.status_pending = true;
        return;
    }

    /* A replay of the current lease (no link-down in between) changes nothing. */
    esp_netif_ip_info_t cur = {0};

    if (esp_netif_get_ip_info(s_state.netif, &cur) == ESP_OK &&
        cur.ip.addr != 0 && cur.ip.addr == e->ipv4.ip.addr) {
        ESP_LOGD(NW_SPLIT_TAG,
                 "CP status replay for unchanged IP " IPSTR " — skip",
                 IP2STR(&e->ipv4.ip));
        return;
    }

    synthesize_sta_connected();
    apply_cp_ip_status(e);
}

/* On a host-power-save wake the CP relays STA_START (which re-adds the netif)
 * only after it has already replayed the network status. STA_START's default
 * handler (registered before this one) has now added the netif, so re-drive any
 * status we stashed earlier — on_cp_dhcp_status will apply it in order. */
static void on_nw_split_sta_start(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    if (!s_state.status_pending || !nw_split_netif_added()) {
        return;
    }
    s_state.status_pending = false;
    (void)esp_event_post(EH_HOST_EVENT, EH_HOST_EVENT_NW_SPLIT_STATUS,
                         &s_state.pending, sizeof(s_state.pending), 0);
}

/* Gate to its only use site (INTERNAL_STATIC); the broader !INTERNAL_DHCP guard left it defined-but-unused in APP_EXTERNAL mode. */
#if CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_STATIC
static esp_netif_t *create_static_sta_netif(void)
{
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    base.flags &= ~(uint32_t)ESP_NETIF_DHCP_CLIENT;
    base.flags |=  (uint32_t)ESP_NETIF_FLAG_AUTOUP;

    esp_netif_config_t cfg = {
        .base = &base,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_WIFI_STA,
    };

    esp_netif_t *n = esp_netif_new(&cfg);
    if (!n) {
        return NULL;
    }

    if (esp_netif_attach_wifi_station(n) != ESP_OK ||
        esp_wifi_set_default_wifi_sta_handlers() != ESP_OK) {
        esp_netif_destroy(n);
        return NULL;
    }

    /* INTERNAL_STATIC must not run local DHCP; IP comes from CP event. */
    esp_err_t dhcpc_rc = esp_netif_dhcpc_stop(n);
    if (dhcpc_rc != ESP_OK && dhcpc_rc != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(NW_SPLIT_TAG, "dhcpc_stop rc=%d", (int)dhcpc_rc);
    }

    return n;
}
#endif /* NETIF_INTERNAL_STATIC */
#endif

#if CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_DHCP
/* The host runs the only DHCP client. Give the CP stack the same lease, so it
 * answers ARP for the shared address and serves the CP port range. The CP
 * accepts it only when its own DHCP client is off (DHCP owner = HOST), so a
 * wrong CP owner shows up here, at association, even before any lease. */
static void nw_split_push_lease(const esp_netif_ip_info_t *lease)
{
    char ip[16], nm[16], gw[16], dns[16] = "0.0.0.0";
    esp_netif_dns_info_t d = {0};

    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&lease->ip));
    snprintf(nm, sizeof(nm), IPSTR, IP2STR(&lease->netmask));
    snprintf(gw, sizeof(gw), IPSTR, IP2STR(&lease->gw));
    if (lease->ip.addr != 0 &&
        esp_netif_get_dns_info(s_state.netif, ESP_NETIF_DNS_MAIN, &d) == ESP_OK &&
        d.ip.type == ESP_IPADDR_TYPE_V4) {
        snprintf(dns, sizeof(dns), IPSTR, IP2STR(&d.ip.u_addr.ip4));
    }
    if (eh_host_nw_split_set_status(WIFI_IF_STA, 1, lease->ip.addr != 0, ip, nm, gw,
                                    lease->ip.addr != 0, dns, ESP_NETIF_DNS_MAIN) != ESP_OK) {
        ESP_LOGE(NW_SPLIT_TAG, "CP rejected the host lease %s: INTERNAL_DHCP needs the "
                 "CP built with CONFIG_ESP_HOSTED_DEFAULT_NETWORK_STACK_DHCP_HOST", ip);
    }
}

static void on_host_sta_connected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    const esp_netif_ip_info_t none = {0};

    nw_split_push_lease(&none);
}

static void on_host_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)data;

    if (evt && evt->esp_netif == s_state.netif) {
        nw_split_push_lease(&evt->ip_info);
    }
}
#endif

/* The app, its Wi-Fi layer (e.g. wifi-cmd) and this feature may each create the
 * default STA netif, in any order: the later callers get the existing one. */
esp_netif_t *__real_esp_netif_create_default_wifi_sta(void);
esp_netif_t *__wrap_esp_netif_create_default_wifi_sta(void);
esp_netif_t *__wrap_esp_netif_create_default_wifi_sta(void)
{
    esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    return n ? n : __real_esp_netif_create_default_wifi_sta();
}

int eh_host_feat_nw_split_init(void)
{
    if (s_state.initialised) {
        return 0;
    }
    memset(&s_state, 0, sizeof(s_state));

#if CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_APP_EXTERNAL
    s_state.netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!s_state.netif) {
        ESP_LOGE(NW_SPLIT_TAG,
                 "APP_EXTERNAL requires WIFI_STA_DEF before esp_hosted_init");
        return ESP_ERR_INVALID_STATE;
    }
    s_state.owns_netif = false;
    s_state.custom_netif = false;
#else
    /* A WIFI_STA_DEF the app (or its Wi-Fi layer) already created is used as is:
     * the DHCP-flag paths above keep its client unbound when the CP owns DHCP. */
    s_state.netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (s_state.netif) {
        s_state.owns_netif = false;
        s_state.custom_netif = false;
    } else {
#if CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_STATIC
        s_state.netif = create_static_sta_netif();
        s_state.custom_netif = true;
#else
        s_state.netif = esp_netif_create_default_wifi_sta();
        s_state.custom_netif = false;
#endif
        if (!s_state.netif) {
            ESP_LOGE(NW_SPLIT_TAG, "netif create failed");
            return ESP_ERR_NO_MEM;
        }
        s_state.owns_netif = true;
    }
#endif

#if CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_CONNECT_ON_START
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START,
                                     on_sta_start, NULL);
#endif
#if CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_RECONNECT_ON_DISCONNECT
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                     on_sta_disconnected, NULL);
#endif

#if !CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_DHCP
    esp_event_handler_register(EH_HOST_EVENT,
                                     EH_HOST_EVENT_NW_SPLIT_STATUS,
                                     on_cp_dhcp_status, NULL);
    /* Registered after the default STA_START handler so the netif is already
     * added when we re-drive a stashed wake-path status. */
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START,
                                     on_nw_split_sta_start, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                     on_nw_split_link_down, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_STOP,
                                     on_nw_split_link_down, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                                     on_nw_split_sta_connected, NULL);
#else
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                                     on_host_sta_connected, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     on_host_got_ip, NULL);
#endif

    if (eh_host_nw_split_register_event_handlers() != ESP_OK) {
        ESP_LOGW(NW_SPLIT_TAG, "nw_split event handler register failed");
    }

    s_state.initialised = true;
    return 0;
}

int eh_host_feat_nw_split_deinit(void)
{
    if (!s_state.initialised) {
        return 0;
    }

#if !CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_DHCP
    (void)esp_event_handler_unregister(EH_HOST_EVENT,
                                       EH_HOST_EVENT_NW_SPLIT_STATUS,
                                       on_cp_dhcp_status);
    (void)esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_START,
                                       on_nw_split_sta_start);
    (void)esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                       on_nw_split_link_down);
    (void)esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_STOP,
                                       on_nw_split_link_down);
    (void)esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                                       on_nw_split_sta_connected);
#else
    (void)esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                                       on_host_sta_connected);
    (void)esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                       on_host_got_ip);
#endif
#if CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_RECONNECT_ON_DISCONNECT
    (void)esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                       on_sta_disconnected);
#endif
#if CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_CONNECT_ON_START
    (void)esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_START,
                                       on_sta_start);
#endif

    eh_host_nw_split_unregister_event_handlers();

    if (s_state.owns_netif && s_state.netif) {
        if (s_state.custom_netif) {
            esp_wifi_clear_default_wifi_driver_and_handlers(s_state.netif);
            esp_netif_destroy(s_state.netif);
        } else {
            esp_netif_destroy_default_wifi(s_state.netif);
        }
    }

    s_state.netif = NULL;
    s_state.owns_netif = false;
    s_state.custom_netif = false;
    s_state.initialised = false;
    return 0;
}

esp_err_t eh_host_nw_split_bind_mac(const uint8_t mac[6], uint16_t mtu)
{
    (void)mtu;
    if (!s_state.initialised || !s_state.netif || !mac) {
        return ESP_ERR_INVALID_STATE;
    }

    /* esp_netif_set_mac takes non-const but only reads; copy to keep our const. */
    uint8_t mac_copy[6];
    memcpy(mac_copy, mac, sizeof(mac_copy));
    return esp_netif_set_mac(s_state.netif, mac_copy);
}

#if EH_HOST_AUTO_FEAT_INIT
EH_HOST_FEAT_REGISTER(eh_host_feat_nw_split_init,
                      eh_host_feat_nw_split_deinit,
                      "nw_split", 200);
#endif
