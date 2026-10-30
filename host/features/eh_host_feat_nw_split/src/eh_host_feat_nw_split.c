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
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_netif.h"
#include "eh_host_feat_nw_split.h"
#include "eh_host_nw_split.h"
#include "eh_host_port_master_config.h"
#if EH_HOST_AUTO_FEAT_INIT
#include "eh_host_auto_init.h"
#endif
#include "eh_host_wifi.h"
#include "eh_host_nw_split_lease_fsm.h"
#include "eh_common_caps.h"
#include "eh_host_event.h"
#if EH_HOST_TYPE_MCU
#include "eh_host_mcu_transport_init_event.h"
#endif

#define NW_SPLIT_TAG "eh_nw_split"

/* WIFI_STA_DEF made by this feature (INTERNAL_*): destroyed at deinit. The app's
 * is used as is and never touched. */
typedef enum { NETIF_NONE, NETIF_DEFAULT, NETIF_STATIC } netif_origin_t;

static struct {
    bool           inited;
    esp_netif_t   *created;
    netif_origin_t origin;
} s_state;

#if CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_DHCP
#define NETIF_MODE_OWNER EH_HOST_NW_SPLIT_IP_OWNER_HOST
#else
#define NETIF_MODE_OWNER EH_HOST_NW_SPLIT_IP_OWNER_CP
#endif

#if CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_STATIC
/* The CP lease is the only address: no host DHCP client. */
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

    esp_err_t dhcpc_rc = esp_netif_dhcpc_stop(n);
    if (dhcpc_rc != ESP_OK && dhcpc_rc != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(NW_SPLIT_TAG, "dhcpc_stop rc=%d", (int)dhcpc_rc);
    }

    return n;
}
#endif

/* The app, its Wi-Fi layer (e.g. wifi-cmd) and this feature may each create the
 * default STA netif, in any order: the later callers get the existing one, and
 * the lease FSM gets the first one (APP_EXTERNAL waits for it). */
esp_netif_t *__real_esp_netif_create_default_wifi_sta(void);
esp_netif_t *__wrap_esp_netif_create_default_wifi_sta(void);
esp_netif_t *__wrap_esp_netif_create_default_wifi_sta(void)
{
    esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (n) {
        return n;
    }
    n = __real_esp_netif_create_default_wifi_sta();
    if (n && s_state.inited) {
        eh_host_nw_split_lease_fsm_netif(n);
    }
    return n;
}

/* An existing WIFI_STA_DEF is used as is in every mode; INTERNAL_* create one
 * when absent. NULL: APP_EXTERNAL and the app has none yet. */
static esp_netif_t *netif_take(void)
{
    esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (n) {
        ESP_LOGI(NW_SPLIT_TAG, "WIFI_STA_DEF from the app, used as is");
        return n;
    }
#if CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_STATIC
    n = create_static_sta_netif();
    s_state.origin = NETIF_STATIC;
#elif CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_DHCP
    n = __real_esp_netif_create_default_wifi_sta();
    s_state.origin = NETIF_DEFAULT;
#endif
    if (!n) {
        s_state.origin = NETIF_NONE;
        return NULL;
    }
    s_state.created = n;
    ESP_LOGI(NW_SPLIT_TAG, "WIFI_STA_DEF created by network split");
    return n;
}

static void netif_release(void)
{
    if (s_state.origin == NETIF_STATIC) {
        esp_wifi_clear_default_wifi_driver_and_handlers(s_state.created);
        esp_netif_destroy(s_state.created);
    } else if (s_state.origin == NETIF_DEFAULT) {
        esp_netif_destroy_default_wifi(s_state.created);
    }
    s_state.created = NULL;
    s_state.origin = NETIF_NONE;
}

/* The CP's network split build and Kconfig IP owner, from its INIT caps. A CP
 * that reports nothing (older) is taken as split, CP-owned. */
static eh_host_nw_split_ip_owner_t cp_boot_owner(bool *cp_split)
{
#if EH_HOST_TYPE_MCU
    uint32_t ext = eh_host_mcu_transport_get_ext_capabilities();
    uint32_t o = (ext & ESP_EXT_CAP_NW_SPLIT_DHCP_MASK) >> ESP_EXT_CAP_NW_SPLIT_DHCP_SHIFT;

    *cp_split = !(ext & ESP_EXT_CAP_NW_SPLIT_REPORTED) || (ext & ESP_EXT_CAP_NW_SPLIT);
    return o == ESP_EXT_CAP_NW_SPLIT_DHCP_HOST ? EH_HOST_NW_SPLIT_IP_OWNER_HOST
                                               : EH_HOST_NW_SPLIT_IP_OWNER_CP;
#else
    *cp_split = true;
    return EH_HOST_NW_SPLIT_IP_OWNER_CP;
#endif
}

int eh_host_feat_nw_split_init(void)
{
    bool cp_split;
    eh_host_nw_split_ip_owner_t boot;

    if (s_state.inited) {
        return 0;
    }
    s_state.inited = true;
    boot = cp_boot_owner(&cp_split);
    if (eh_host_nw_split_register_event_handlers() != ESP_OK) {
        ESP_LOGW(NW_SPLIT_TAG, "nw_split event handler register failed");
    }
    if (eh_host_nw_split_lease_fsm_start(netif_take(), boot, NETIF_MODE_OWNER, cp_split) != ESP_OK) {
        ESP_LOGE(NW_SPLIT_TAG, "lease FSM start failed");
    }
    return 0;
}

int eh_host_feat_nw_split_deinit(void)
{
    if (!s_state.inited) {
        return 0;
    }
    s_state.inited = false;
    eh_host_nw_split_lease_fsm_stop();
    eh_host_nw_split_unregister_event_handlers();
    netif_release();
    return 0;
}

esp_err_t eh_host_nw_split_bind_mac(const uint8_t mac[6], uint16_t mtu)
{
    esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    uint8_t mac_copy[6];

    (void)mtu;
    if (!n || !mac) {
        return ESP_ERR_INVALID_STATE;
    }
    /* esp_netif_set_mac takes non-const but only reads; copy to keep our const. */
    memcpy(mac_copy, mac, sizeof(mac_copy));
    return esp_netif_set_mac(n, mac_copy);
}

#if EH_HOST_AUTO_FEAT_INIT
EH_HOST_FEAT_REGISTER(eh_host_feat_nw_split_init,
                      eh_host_feat_nw_split_deinit,
                      "nw_split", 200);
#endif
