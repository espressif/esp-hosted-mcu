/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Liveness monitor for the manual bring-up. Drives the CP heartbeat, watches the
 * hosted lifecycle events, and recovers the link by calling an injected
 * restart callback (main.c: deinit -> init -> connect). Recovery runs in its own task —
 * never call esp_hosted_deinit()/init() from an esp_event or esp_timer callback.
 *
 * Recovery triggers (per the hosted event contract):
 *   - TRANSPORT_FAILURE       bus driver saw a read/write error (link broke)
 *   - CP_INIT (unexpected)    the coprocessor rebooted out from under us
 *   - heartbeat watchdog      no CP_HEARTBEAT within the window (peer stalled)
 */
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_hosted_event.h"
#include "eh_host_feat_heartbeat.h"
#include "eh_host_heartbeat.h"
#include "example_private.h"

/* The CP clamps the heartbeat interval to a 10s floor (MIN_HEARTBEAT_INTERVAL),
 * so request 10s. The watchdog window must exceed the CP beat period (which the
 * clamp + first-beat startup latency push above the requested interval), so floor
 * it at 3x the interval — matching the hosted_events reference monitor. */
#define HB_INTERVAL_SEC   10
#define HB_WATCHDOG_MS    (HB_INTERVAL_SEC * 3 * 1000)   /* 30s */

static const char *TAG = EH_EX_TAG;

static esp_err_t (*s_reinit_fn)(void);
static SemaphoreHandle_t s_recover;         /* given by triggers, taken by the task */
static SemaphoreHandle_t s_exited;          /* given by the task on its way out */
static esp_timer_handle_t s_watchdog;
static TaskHandle_t s_task;
static esp_event_handler_instance_t s_evt;
static volatile bool s_stop;

static void rearm_watchdog(void)
{
    if (s_watchdog) {
        esp_timer_stop(s_watchdog);                       /* one-shot; restart the window */
        esp_timer_start_once(s_watchdog, (uint64_t)HB_WATCHDOG_MS * 1000);
    }
}

static void watchdog_cb(void *arg)
{
    ESP_LOGW(TAG, "EH stage=monitor heartbeat-loss (no beat in %d ms)", HB_WATCHDOG_MS);
    xSemaphoreGive(s_recover);
}

static void monitor_cp_reboot(void)
{
    ESP_LOGW(TAG, "EH stage=monitor cp-reboot -> recover");
    xSemaphoreGive(s_recover);
}

static void monitor_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != ESP_HOSTED_EVENT) {
        return;
    }
    switch (id) {
    case ESP_HOSTED_EVENT_CP_HEARTBEAT:
        ESP_LOGI(TAG, "EH stage=monitor heartbeat");
        rearm_watchdog();
        break;
    case ESP_HOSTED_EVENT_TRANSPORT_FAILURE:
        ESP_LOGW(TAG, "EH stage=monitor transport-failure -> recover");
        xSemaphoreGive(s_recover);
        break;
    default:
        break;
    }
}

static void arm_heartbeat(void)
{
    eh_host_feat_heartbeat_init();                         /* register CP_HEARTBEAT handler (idempotent) */
    eh_host_heartbeat_configure(true, HB_INTERVAL_SEC);    /* tell the CP to beat */
    /* Arm the watchdog now, at transport-up — NOT on the first beat: a CP wedged
     * from boot never beats at all and must still be detected. The 3x-interval
     * window absorbs first-beat startup latency (see the hosted_events monitor). */
    rearm_watchdog();
}

static void monitor_task(void *arg)
{
    while (1) {
        xSemaphoreTake(s_recover, portMAX_DELAY);
        if (s_stop) {
            break;
        }
        if (s_reinit_fn) {
            esp_timer_stop(s_watchdog);
            if (s_reinit_fn() == ESP_OK) {                 /* hosted link back up */
                arm_heartbeat();                           /* CP was reset — re-issue heartbeat */
            }
        }
    }
    xSemaphoreGive(s_exited);
    vTaskDelete(NULL);
}

static void monitor_release(void)
{
    example_on_cp_reboot(NULL);
    if (s_evt) {
        esp_event_handler_instance_unregister(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID, s_evt);
        s_evt = NULL;
    }
    if (s_watchdog) {
        esp_timer_stop(s_watchdog);
        esp_timer_delete(s_watchdog);
        s_watchdog = NULL;
    }
    if (s_recover) {
        vSemaphoreDelete(s_recover);
        s_recover = NULL;
    }
    if (s_exited) {
        vSemaphoreDelete(s_exited);
        s_exited = NULL;
    }
}

esp_err_t example_bus_monitor_start(esp_err_t (*reinit_fn)(void))
{
    s_reinit_fn = reinit_fn;
    s_stop = false;

    esp_err_t err = ESP_ERR_NO_MEM;
    s_recover = xSemaphoreCreateBinary();
    s_exited = xSemaphoreCreateBinary();
    if (!s_recover || !s_exited) {
        goto fail;
    }
    const esp_timer_create_args_t targs = { .callback = watchdog_cb, .name = "eh_hb_wd" };
    err = esp_timer_create(&targs, &s_watchdog);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_event_handler_instance_register(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID,
                                              monitor_evt, NULL, &s_evt);
    if (err != ESP_OK) {
        goto fail;
    }
    if (xTaskCreate(monitor_task, "eh_monitor", 4096, NULL, 5, &s_task) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    example_on_cp_reboot(monitor_cp_reboot);
    arm_heartbeat();
    ESP_LOGI(TAG, "EH stage=monitor started (heartbeat=%ds watchdog=%dms)",
             HB_INTERVAL_SEC, HB_WATCHDOG_MS);
    return ESP_OK;

fail:
    monitor_release();
    return err;
}

void example_bus_monitor_stop(void)
{
    if (!s_task) {
        return;
    }
    example_on_cp_reboot(NULL);                            /* no new triggers */
    if (s_evt) {                                           /* no new triggers */
        esp_event_handler_instance_unregister(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID, s_evt);
        s_evt = NULL;
    }
    esp_timer_stop(s_watchdog);
    s_stop = true;
    xSemaphoreGive(s_recover);                             /* wake the task so it exits */
    xSemaphoreTake(s_exited, portMAX_DELAY);               /* waits out an in-flight recovery */
    s_task = NULL;
    eh_host_heartbeat_configure(false, 0);
    monitor_release();
}
