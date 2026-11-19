/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* esp_wifi_remote_* strong defs forwarding to eh_host_wifi_* / eh_host_sys_*. */

#include <stdint.h>
#include <stdlib.h>

#include "esp_err.h"

#if __has_include(<esp_wifi_remote.h>)

#include "esp_wifi.h"
#include "esp_wifi_types.h"

#include "eh_host_sys.h"
#include "eh_host_wifi.h"
#include "eh_host_wifi_priv.h"
#if EH_HOST_FEAT_WIFI_EXT_ITWT_READY
#include "eh_host_wifi_itwt.h"
#endif

#include "esp_wifi_remote.h"

#if EH_HOST_TYPE_MCU && EH_HOST_FEAT_WIFI_READY
#include "eh_common_interface.h"
#include "esp_hosted_wifi_remote_glue.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_event.h"
#include "eh_host_port.h"
#ifdef ESP_PLATFORM
#include "lwip/netif.h"   /* netif->input peek — IDF/lwip only */
#endif

static esp_remote_channel_t s_sta_ch;
static esp_remote_channel_t s_ap_ch;

/* Default netifs, cached: they outlive every association; cleared on Wi-Fi deinit. */
static esp_netif_t *s_netif[2];

/* lwIP input attached: netif_add runs on the event loop after STA_START is posted,
 * and a frame before it faults in wlanif_input. */
static inline bool hosted_netif_input_ready(bool is_ap, bool *has_ip)
{
#ifdef ESP_PLATFORM
    int i = is_ap ? 1 : 0;
    esp_netif_t *n = s_netif[i];
    if (__builtin_expect(n == NULL, 0)) {
        n = esp_netif_get_handle_from_ifkey(is_ap ? "WIFI_AP_DEF" : "WIFI_STA_DEF");
        if (!n) {
            return false;
        }
        s_netif[i] = n;
    }
    struct netif *impl = (struct netif *)esp_netif_get_netif_impl(n);
    if (!impl || !impl->input) {
        return false;
    }
    *has_ip = !ip4_addr_isany_val(*netif_ip4_addr(impl));
    return true;
#else
    (void)is_ap;
    *has_ip = true;
    return true;
#endif
}

/* Frames the path cannot take yet, in arrival order. Full: the new frame is
 * refused, so the first ones (the frame that woke the host) are kept. */
#define HOLD_MAX 10

typedef struct {
    void   *buf;
    void   *to_free;
    size_t  len;
} held_frame_t;

static struct {
    held_frame_t q[HOLD_MAX];
    uint8_t      head, n;
} s_hold[2];
static eh_host_port_mutex_t *s_hold_mtx;

static esp_remote_channel_t channel_of(bool is_ap)
{
    return is_ap ? s_ap_ch : s_sta_ch;
}

/* Open: the FSM admits RX, lwIP takes input and the netif has its address. */
static bool path_open(bool is_ap)
{
    bool has_ip = false;

    return eh_host_wifi_rx_admitted(is_ap) && hosted_netif_input_ready(is_ap, &has_ip) && has_ip;
}

static bool hold_pop(bool is_ap, held_frame_t *f)
{
    bool got = false;

    eh_host_port_mutex_lock(s_hold_mtx);
    if (s_hold[is_ap].n) {
        *f = s_hold[is_ap].q[s_hold[is_ap].head];
        s_hold[is_ap].head = (s_hold[is_ap].head + 1) % HOLD_MAX;
        s_hold[is_ap].n--;
        got = true;
    }
    eh_host_port_mutex_unlock(s_hold_mtx);
    return got;
}

static void hold_push(bool is_ap, void *buf, void *to_free, size_t len)
{
    bool full;

    eh_host_port_mutex_lock(s_hold_mtx);
    full = s_hold[is_ap].n == HOLD_MAX;
    if (!full) {
        s_hold[is_ap].q[(s_hold[is_ap].head + s_hold[is_ap].n) % HOLD_MAX] =
            (held_frame_t){ .buf = buf, .to_free = to_free, .len = len };
        s_hold[is_ap].n++;
    }
    eh_host_port_mutex_unlock(s_hold_mtx);
    if (full) {
        ESP_LOGW("eh_rx_guard", "%s: path not open, %d held, %u bytes refused",
                 is_ap ? "ap" : "sta", HOLD_MAX, (unsigned int)len);
        free(to_free);
    }
}

static void hold_deliver(bool is_ap)
{
    held_frame_t f;
    int n = 0;

    while (path_open(is_ap) && hold_pop(is_ap, &f)) {
        (void)esp_wifi_remote_channel_rx(channel_of(is_ap), f.buf, f.to_free, f.len);
        n++;
    }
    if (n) {
        ESP_LOGI("eh_rx_guard", "%s: %d held frames delivered", is_ap ? "ap" : "sta", n);
    }
}

static void hold_drop(bool is_ap)
{
    held_frame_t f;
    int n = 0;

    while (hold_pop(is_ap, &f)) {
        free(f.to_free);
        n++;
    }
    if (n) {
        ESP_LOGW("eh_rx_guard", "%s: link down, %d held frames dropped", is_ap ? "ap" : "sta", n);
    }
}

/* The FSM's app-facing edges: the path opened (GOT_IP / AP_START) or went away;
 * at a start the netif is looked up again (it may have been re-created). */
static void on_path_edge(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == IP_EVENT) {
        hold_deliver(false);
    } else if (id == WIFI_EVENT_STA_START) {
        s_netif[0] = NULL;
    } else if (id == WIFI_EVENT_AP_START) {
        s_netif[1] = NULL;
        hold_deliver(true);
    } else {
        hold_drop(id == WIFI_EVENT_AP_STOP);
    }
}

static const int32_t s_wifi_edges[] = {
    WIFI_EVENT_STA_START, WIFI_EVENT_AP_START, WIFI_EVENT_STA_DISCONNECTED, WIFI_EVENT_STA_STOP,
    WIFI_EVENT_AP_STOP,
};

static void path_edges_subscribe(bool on)
{
    for (size_t i = 0; i < sizeof(s_wifi_edges) / sizeof(s_wifi_edges[0]); i++) {
        if (on) {
            (void)esp_event_handler_register(WIFI_EVENT, s_wifi_edges[i], on_path_edge, NULL);
        } else {
            (void)esp_event_handler_unregister(WIFI_EVENT, s_wifi_edges[i], on_path_edge);
        }
    }
    if (on) {
        (void)esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_path_edge, NULL);
    } else {
        (void)esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, on_path_edge);
    }
}

/* Deliver once the FSM admits RX and lwIP takes input; hold until then. Before
 * the address only new frames pass (the host's DHCP); held ones wait for it. */
static esp_err_t hosted_channel_rx_guard(void *h, void *buffer,
                                         void *buff_to_free, size_t len)
{
    bool is_ap = (h == (void *)s_ap_ch);
    bool has_ip = false;

    if (__builtin_expect(eh_host_wifi_rx_admitted(is_ap) &&
                         hosted_netif_input_ready(is_ap, &has_ip), 1)) {
        if (__builtin_expect(s_hold[is_ap].n != 0, 0) && has_ip) {
            hold_deliver(is_ap);
        }
        return esp_wifi_remote_channel_rx(h, buffer, buff_to_free, len);
    }
    hold_push(is_ap, buffer, buff_to_free, len);
    return ESP_OK;
}

/* As upstream's esp_hosted_init: the channels exist before the transport starts
 * (constructors run before the scheduler), so no frame meets a missing channel. */
__attribute__((constructor)) static void hosted_channels_create(void)
{
    struct esp_remote_channel_config sta_cfg = { .if_type = ESP_STA_IF, .secure = false };
    struct esp_remote_channel_config ap_cfg  = { .if_type = ESP_AP_IF,  .secure = false };
    esp_remote_channel_tx_fn_t tx_fn = NULL;

    s_hold_mtx = eh_host_port_mutex_create();
    if (!s_hold_mtx) {
        return;
    }
    s_sta_ch = esp_hosted_add_channel(&sta_cfg, &tx_fn, hosted_channel_rx_guard);
    if (s_sta_ch) esp_wifi_remote_channel_set(WIFI_IF_STA, s_sta_ch, tx_fn);

    s_ap_ch = esp_hosted_add_channel(&ap_cfg, &tx_fn, hosted_channel_rx_guard);
    if (s_ap_ch) esp_wifi_remote_channel_set(WIFI_IF_AP, s_ap_ch, tx_fn);
}
#endif

const char eh_wifi_remote_glue_anchor = 0;

/* Path edges follow a successful init/deinit: a refused call changes nothing.
 * Subscribed here, after the app's netif handlers, so AP_START finds input attached. */
esp_err_t esp_wifi_remote_init(const wifi_init_config_t *config)
{
    esp_err_t rc = eh_host_wifi_init(config);
#if EH_HOST_TYPE_MCU && EH_HOST_FEAT_WIFI_READY
    if (rc == ESP_OK) {
        path_edges_subscribe(true);
    }
#endif
    return rc;
}

esp_err_t esp_wifi_remote_deinit(void)
{
    esp_err_t rc = eh_host_wifi_deinit();
#if EH_HOST_TYPE_MCU && EH_HOST_FEAT_WIFI_READY
    if (rc != ESP_OK) {
        return rc;
    }
    path_edges_subscribe(false);
    hold_drop(false);
    hold_drop(true);
    s_netif[0] = NULL;
    s_netif[1] = NULL;
#endif
    return rc;
}

esp_err_t esp_wifi_remote_start(void)
{
    return eh_host_wifi_start();
}

esp_err_t esp_wifi_remote_stop(void)
{
    return eh_host_wifi_stop();
}

esp_err_t esp_wifi_remote_connect(void)
{
    return eh_host_wifi_connect();
}

esp_err_t esp_wifi_remote_disconnect(void)
{
    return eh_host_wifi_disconnect();
}

esp_err_t esp_wifi_remote_set_mode(wifi_mode_t mode)
{
    return eh_host_wifi_set_mode(mode);
}

esp_err_t esp_wifi_remote_get_mode(wifi_mode_t *mode)
{
    return eh_host_wifi_get_mode(mode);
}

esp_err_t esp_wifi_remote_set_config(wifi_interface_t interface,
                                     wifi_config_t *conf)
{
    return eh_host_wifi_set_config(interface, conf);
}

esp_err_t esp_wifi_remote_get_config(wifi_interface_t interface,
                                     wifi_config_t *conf)
{
    return eh_host_wifi_get_config(interface, conf);
}

/* CP owns its NVS; host accepts WIFI_STORAGE_* silently. */
esp_err_t esp_wifi_remote_set_storage(wifi_storage_t storage)
{
    (void)storage;
    return ESP_OK;
}

/* MAC lives in eh_host_sys, not eh_host_wifi. */
esp_err_t esp_wifi_remote_get_mac(wifi_interface_t ifx, uint8_t mac[6])
{
    return eh_host_sys_get_mac(ifx, mac);
}

esp_err_t esp_wifi_remote_set_mac(wifi_interface_t ifx, const uint8_t mac[6])
{
    return eh_host_sys_set_mac(ifx, mac);
}

#if EH_HOST_FEAT_WIFI_EXT_ITWT_READY
#include "esp_wifi_types_generic.h"

esp_err_t esp_wifi_sta_itwt_setup(wifi_itwt_setup_config_t *setup_config)
{ return eh_host_wifi_itwt_setup(setup_config); }

esp_err_t esp_wifi_sta_itwt_teardown(int flow_id)
{ return eh_host_wifi_itwt_teardown(flow_id); }

esp_err_t esp_wifi_sta_itwt_suspend(int flow_id, int suspend_time_ms)
{ return eh_host_wifi_itwt_suspend(flow_id, suspend_time_ms); }

esp_err_t esp_wifi_sta_itwt_get_flow_id_status(int *flow_id_bitmap)
{ return eh_host_wifi_itwt_get_flow_id_status(flow_id_bitmap); }

esp_err_t esp_wifi_sta_itwt_send_probe_req(int timeout_ms)
{ return eh_host_wifi_itwt_send_probe_req(timeout_ms); }

esp_err_t esp_wifi_sta_itwt_set_target_wake_time_offset(int offset_us)
{ return eh_host_wifi_itwt_set_target_wake_time_offset(offset_us); }

esp_err_t esp_wifi_sta_twt_config(wifi_twt_config_t *config)
{ return eh_host_wifi_sta_twt_config(config); }
#endif /* EH_HOST_FEAT_WIFI_EXT_ITWT_READY */

#endif /* __has_include(<esp_wifi_remote.h>) */
