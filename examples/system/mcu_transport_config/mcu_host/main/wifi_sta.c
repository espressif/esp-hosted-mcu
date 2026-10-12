/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Stage 2: Wi-Fi STA over the hosted link. esp_wifi_* is routed to the
 * coprocessor via esp_wifi_remote on p4/h2. In manual mode the host Wi-Fi feature
 * is not auto-initialised, so we call eh_host_feat_wifi_init() before esp_wifi_init().
 * Connect is event-driven: STA_START -> esp_wifi_connect(); GOT_IP unblocks main.
 */
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "eh_host_feat_wifi.h"
#include "example_private.h"

static const char *TAG = EH_EX_TAG;

static esp_netif_t *s_netif;
static esp_event_handler_instance_t s_any, s_ip;

static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();                       /* simple reconnect for the demo */
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "EH stage=wifi ip=" IPSTR, IP2STR(&e->ip_info.ip));
        example_signal_got_ip();
    }
}

esp_err_t example_wifi_sta_start(void)
{
    if (eh_host_feat_wifi_init() != 0) {          /* manual feature init (auto-init off) */
        ESP_LOGE(TAG, "eh_host_feat_wifi_init failed");
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_evt, NULL, &s_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_evt, NULL, &s_ip));

    wifi_config_t wc = {
        .sta = {
            .ssid = CONFIG_EXAMPLE_WIFI_SSID,
            .password = CONFIG_EXAMPLE_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_err_t err = example_wait_got_ip(30000);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "EH stage=wifi no ip yet (rc=%d)", err);
    }
    return err;
}

void example_wifi_sta_stop(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_any);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip);
    esp_wifi_deinit();
    if (s_netif) {
        esp_netif_destroy_default_wifi(s_netif);
        s_netif = NULL;
    }
}
