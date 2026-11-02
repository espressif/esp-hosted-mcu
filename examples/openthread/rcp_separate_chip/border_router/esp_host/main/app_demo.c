/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Per-link demo traffic, so each chip's data path shows in the log:
 *   Wi-Fi (co-processor) : ping the gateway once the station has an IPv4 address
 *   Thread (RCP)         : RCP version at start, then a periodic status line
 */

#include <inttypes.h>

#include <string.h>

#include "esp_check.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip_addr.h"
#include "openthread/instance.h"
#include "openthread/thread.h"
#include "openthread/thread_ftd.h"
#include "openthread/link.h"
#include "ping/ping_sock.h"

#include "app_demo.h"
#if CONFIG_ESP_HOSTED_RAW_THROUGHPUT_TRANSPORT
#include "eh_host_raw_tp_stats.h"
#endif

#define TAG "demo"

#define WIFI_PING_COUNT         5
#define THREAD_STATUS_PERIOD_MS 30000

static void wifi_ping_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t sent = 0, recv = 0, ms = 0;

    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &sent, sizeof(sent));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &recv, sizeof(recv));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &ms, sizeof(ms));
    ESP_LOGI(TAG, "wifi: ping gateway %" PRIu32 "/%" PRIu32 " replies, %" PRIu32 " ms total",
             recv, sent, ms);
    esp_ping_delete_session(hdl);
}

static void wifi_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const ip_event_got_ip_t *ev = data;
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    esp_ping_callbacks_t cbs = { .on_ping_end = wifi_ping_end };
    esp_ping_handle_t ping;

    wifi_ap_record_t ap;
    ESP_LOGI(TAG, "wifi: station " IPSTR ", gateway " IPSTR,
             IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.gw));
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ESP_LOGI(TAG, "wifi: AP \"%s\" channel %u (%s), rssi %d, phy%s%s%s%s",
                 (const char *)ap.ssid, ap.primary, ap.primary > 14 ? "5 GHz" : "2.4 GHz", ap.rssi,
                 ap.phy_11b ? " b" : "", ap.phy_11g ? " g" : "", ap.phy_11n ? " n" : "",
                 ap.phy_11ax ? " ax" : "");
    }
    ip_addr_set_ip4_u32(&cfg.target_addr, ev->ip_info.gw.addr);
    cfg.count = WIFI_PING_COUNT;
    if (esp_ping_new_session(&cfg, &cbs, &ping) == ESP_OK) {
        esp_ping_start(ping);
    }
}

static void thread_status_task(void *arg)
{
    otInstance *ot = esp_openthread_get_instance();

    esp_openthread_lock_acquire(portMAX_DELAY);
    ESP_LOGI(TAG, "thread: RCP %s", otGetRadioVersionString(ot));
    esp_openthread_lock_release();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(THREAD_STATUS_PERIOD_MS));
        esp_openthread_lock_acquire(portMAX_DELAY);
        otDeviceRole role = otThreadGetDeviceRole(ot);
        uint8_t channel = otLinkGetChannel(ot);
        otPanId panid = otLinkGetPanId(ot);
        uint16_t rloc16 = otThreadGetRloc16(ot);
        uint16_t children = 0;
        otChildInfo child;
        for (uint16_t i = 0; i < otThreadGetMaxAllowedChildren(ot); i++) {
            if (otThreadGetChildInfoByIndex(ot, i, &child) == OT_ERROR_NONE) {
                children++;
            }
        }
        esp_openthread_lock_release();
        ESP_LOGI(TAG, "thread: role %s, channel %u, panid 0x%04x, rloc16 0x%04x, children %u",
                 otThreadDeviceRoleToString(role), channel, panid, rloc16, children);
    }
}

esp_err_t app_demo_start(void)
{
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_got_ip, NULL),
                        TAG, "register got-ip handler");
    ESP_RETURN_ON_FALSE(xTaskCreate(thread_status_task, "thread_status", 3072, NULL, 4, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "create thread_status task");
    return ESP_OK;
}

#if CONFIG_ESP_HOSTED_RAW_THROUGHPUT_TRANSPORT
/* Raw ESP-Hosted bus throughput, no Wi-Fi: raw_tp start tx|rx|both, raw_tp stop */
static int cmd_raw_tp(int argc, char **argv)
{
    int rc = -1;

    if (argc >= 2 && !strcmp(argv[1], "stop")) {
        rc = eh_host_raw_tp_stop();
    } else if (argc >= 3 && !strcmp(argv[1], "start")) {
        uint8_t dir = !strcmp(argv[2], "tx")   ? ESP_TEST_RAW_TP__HOST_TO_ESP :
                      !strcmp(argv[2], "rx")   ? ESP_TEST_RAW_TP__ESP_TO_HOST :
                      !strcmp(argv[2], "both") ? ESP_TEST_RAW_TP__BIDIRECTIONAL : 0;
        rc = dir ? eh_host_raw_tp_start(dir) : -1;
    }
    printf("raw_tp: %s\n", rc ? "usage: raw_tp start tx|rx|both | raw_tp stop" : "OK");
    return rc ? 1 : 0;
}
#endif

esp_err_t app_demo_register_commands(void)
{
#if CONFIG_ESP_HOSTED_RAW_THROUGHPUT_TRANSPORT
    const esp_console_cmd_t cmd = {
        .command = "raw_tp",
        .help = "ESP-Hosted bus throughput: raw_tp start tx|rx|both, raw_tp stop",
        .func = cmd_raw_tp,
    };
    return esp_console_cmd_register(&cmd);
#else
    return ESP_OK;
#endif
}
