/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Shared esp_event foundation. Owns the default event loop and a small event
 * group that the stages gate on: LINK_UP (set on the hosted TRANSPORT_UP event,
 * cleared on DOWN/FAILURE) and GOT_IP (set by wifi_sta.c). It also sorts CP_INIT:
 * each bring-up expects one; any other means the CP rebooted, reported to the
 * callback bus_monitor.c installs. Registered before the first bring-up, so no
 * CP_INIT is missed.
 */
#include <stdbool.h>
#include "esp_log.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_hosted_event.h"
#include "example_private.h"

static const char *TAG = EH_EX_TAG;

#define BIT_LINK_UP  (1 << 0)
#define BIT_GOT_IP   (1 << 1)

static EventGroupHandle_t s_eg;
static volatile bool s_cp_init_expected;
static void (*s_on_cp_reboot)(void);

static void hosted_observer(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != ESP_HOSTED_EVENT) {
        return;
    }
    switch (id) {
    case ESP_HOSTED_EVENT_TRANSPORT_UP:
        xEventGroupSetBits(s_eg, BIT_LINK_UP);
        ESP_LOGI(TAG, "hosted event: transport up");
        break;
    case ESP_HOSTED_EVENT_TRANSPORT_DOWN:
        xEventGroupClearBits(s_eg, BIT_LINK_UP);
        ESP_LOGW(TAG, "hosted event: transport down");
        break;
    case ESP_HOSTED_EVENT_TRANSPORT_FAILURE:
        xEventGroupClearBits(s_eg, BIT_LINK_UP);
        ESP_LOGW(TAG, "hosted event: transport failure");
        break;
    case ESP_HOSTED_EVENT_CP_INIT:
        if (s_cp_init_expected) {
            s_cp_init_expected = false;
            ESP_LOGI(TAG, "hosted event: cp init");
        } else if (s_on_cp_reboot) {
            s_on_cp_reboot();
        }
        break;
    default:
        break;
    }
}

esp_err_t example_events_init(void)
{
    s_eg = xEventGroupCreate();
    if (!s_eg) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {   /* loop may already exist */
        return err;
    }
    return esp_event_handler_instance_register(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID,
                                               hosted_observer, NULL, NULL);
}

esp_err_t example_wait_link_up(uint32_t timeout_ms)
{
    EventBits_t b = xEventGroupWaitBits(s_eg, BIT_LINK_UP, pdFALSE, pdTRUE,
                                        pdMS_TO_TICKS(timeout_ms));
    return (b & BIT_LINK_UP) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t example_wait_got_ip(uint32_t timeout_ms)
{
    EventBits_t b = xEventGroupWaitBits(s_eg, BIT_GOT_IP, pdFALSE, pdTRUE,
                                        pdMS_TO_TICKS(timeout_ms));
    return (b & BIT_GOT_IP) ? ESP_OK : ESP_ERR_TIMEOUT;
}

void example_signal_got_ip(void)
{
    if (s_eg) {
        xEventGroupSetBits(s_eg, BIT_GOT_IP);
    }
}

void example_expect_cp_init(void)
{
    s_cp_init_expected = true;
}

void example_on_cp_reboot(void (*cb)(void))
{
    s_on_cp_reboot = cb;
}

void example_clear_conn_bits(void)
{
    if (s_eg) {
        xEventGroupClearBits(s_eg, BIT_LINK_UP | BIT_GOT_IP);
    }
}
