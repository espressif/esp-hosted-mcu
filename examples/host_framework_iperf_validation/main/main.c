/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_console.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_transport_config.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "cmd_system.h"
#include "iperf.h"
#include "iperf_cmd.h"
#include "iperf_remote_control.h"
#include "ping_cmd.h"
#include "wifi_cmd.h"

#if CONFIG_ESP_WIFI_ENABLE_WIFI_RX_STATS || CONFIG_ESP_WIFI_ENABLE_WIFI_TX_STATS
#include "esp_wifi_he.h"
#endif

#if CONFIG_ESP_WIFI_ENABLE_WIFI_TX_STATS
extern int wifi_cmd_get_tx_statistics(int argc, char **argv);
extern int wifi_cmd_clr_tx_statistics(int argc, char **argv);
#endif

#if CONFIG_ESP_WIFI_ENABLE_WIFI_RX_STATS
extern int wifi_cmd_get_rx_statistics(int argc, char **argv);
extern int wifi_cmd_clr_rx_statistics(int argc, char **argv);
#endif

#define HOSTED_CONNECT_TIMEOUT_MS CONFIG_EXAMPLE_HOSTED_CONNECT_TIMEOUT_MS
#define SOFTAP_START_TIMEOUT_MS   CONFIG_EXAMPLE_SOFTAP_START_TIMEOUT_MS

#define BIT_CP_INIT        BIT0
#define BIT_TRANSPORT_UP   BIT1
#define BIT_TRANSPORT_DOWN BIT2
#define BIT_TRANSPORT_FAIL BIT3
#define BIT_SOFTAP_STARTED BIT4

static const char *TAG = "host_fw_iperf";

static EventGroupHandle_t s_app_events;
static esp_event_handler_instance_t s_hosted_handler;
static esp_event_handler_instance_t s_wifi_handler;

static uint32_t s_cp_init_count;
static uint32_t s_transport_up_count;
static uint32_t s_transport_down_count;
static uint32_t s_transport_failure_count;

void iperf_hook_show_wifi_stats(iperf_traffic_type_t type, iperf_status_t status)
{
    if (status == IPERF_STARTED) {
#if CONFIG_ESP_WIFI_ENABLE_WIFI_TX_STATS
        if (type != IPERF_UDP_SERVER) {
            wifi_cmd_clr_tx_statistics(0, NULL);
        }
#endif
#if CONFIG_ESP_WIFI_ENABLE_WIFI_RX_STATS
        if (type != IPERF_UDP_CLIENT) {
            wifi_cmd_clr_rx_statistics(0, NULL);
        }
#endif
    }

    if (status == IPERF_STOPPED) {
#if CONFIG_ESP_WIFI_ENABLE_WIFI_TX_STATS
        if (type != IPERF_UDP_SERVER) {
            wifi_cmd_get_tx_statistics(0, NULL);
        }
#endif
#if CONFIG_ESP_WIFI_ENABLE_WIFI_RX_STATS
        if (type != IPERF_UDP_CLIENT) {
            wifi_cmd_get_rx_statistics(0, NULL);
        }
#endif
    }
}

static esp_err_t init_nvs_flash(void)
{
    esp_err_t ret = nvs_flash_init();

    if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    return ret;
}

static esp_err_t init_default_event_loop(void)
{
    esp_err_t ret = esp_event_loop_create_default();

    if ((ret == ESP_OK) || (ret == ESP_ERR_INVALID_STATE)) {
        return ESP_OK;
    }

    return ret;
}

static void log_reset_pin_config(void)
{
    gpio_pin_t reset_pin = { 0 };

    if (esp_hosted_transport_get_reset_config(&reset_pin) == ESP_TRANSPORT_OK) {
        ESP_LOGI(TAG, "configured slave reset GPIO: pin=%d", reset_pin.pin);
        return;
    }

    ESP_LOGW(TAG, "failed to read configured slave reset GPIO");
}

static void log_softap_ip_info(void)
{
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip_info;

    if (!ap_netif) {
        ESP_LOGW(TAG, "failed to get default AP netif handle");
        return;
    }

    if (esp_netif_get_ip_info(ap_netif, &ip_info) != ESP_OK) {
        ESP_LOGW(TAG, "failed to read SoftAP IP information");
        return;
    }

    ESP_LOGI(TAG, "SoftAP IP: " IPSTR ", netmask: " IPSTR ", gateway: " IPSTR,
            IP2STR(&ip_info.ip),
            IP2STR(&ip_info.netmask),
            IP2STR(&ip_info.gw));
}

static void hosted_event_handler(void *arg, esp_event_base_t event_base,
        int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id == ESP_HOSTED_EVENT_CP_INIT) {
        esp_hosted_event_init_t *event = (esp_hosted_event_init_t *)event_data;

        s_cp_init_count++;
        xEventGroupSetBits(s_app_events, BIT_CP_INIT);
        ESP_LOGI(TAG, "got CP_INIT event, reset reason=%" PRIu16, event ? event->reason : 0);
        return;
    }

    if (event_id == ESP_HOSTED_EVENT_TRANSPORT_UP) {
        s_transport_up_count++;
        xEventGroupSetBits(s_app_events, BIT_TRANSPORT_UP);
        ESP_LOGI(TAG, "got TRANSPORT_UP event");
        return;
    }

    if (event_id == ESP_HOSTED_EVENT_TRANSPORT_DOWN) {
        s_transport_down_count++;
        xEventGroupSetBits(s_app_events, BIT_TRANSPORT_DOWN);
        ESP_LOGI(TAG, "got TRANSPORT_DOWN event");
        return;
    }

    if (event_id == ESP_HOSTED_EVENT_TRANSPORT_FAILURE) {
        s_transport_failure_count++;
        xEventGroupSetBits(s_app_events, BIT_TRANSPORT_FAIL);
        ESP_LOGW(TAG, "got TRANSPORT_FAILURE event");
        return;
    }

    ESP_LOGI(TAG, "got ESP_HOSTED event id=%" PRIi32, event_id);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
        int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;

    if (event_id == WIFI_EVENT_AP_START) {
        xEventGroupSetBits(s_app_events, BIT_SOFTAP_STARTED);
        ESP_LOGI(TAG, "got WIFI_EVENT_AP_START");
    }
}

static esp_err_t register_event_handlers(void)
{
    esp_err_t ret;

    s_hosted_handler = NULL;
    s_wifi_handler = NULL;

    ret = esp_event_handler_instance_register(ESP_HOSTED_EVENT,
            ESP_EVENT_ANY_ID,
            hosted_event_handler,
            NULL,
            &s_hosted_handler);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT,
            WIFI_EVENT_AP_START,
            wifi_event_handler,
            NULL,
            &s_wifi_handler);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

static esp_err_t bring_up_hosted_transport(void)
{
    EventBits_t bits;
    const EventBits_t required_bits = BIT_CP_INIT | BIT_TRANSPORT_UP;
    esp_err_t ret;

    xEventGroupClearBits(s_app_events,
            BIT_CP_INIT | BIT_TRANSPORT_UP | BIT_TRANSPORT_DOWN | BIT_TRANSPORT_FAIL);

    log_reset_pin_config();

    ret = esp_hosted_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hosted_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_hosted_connect_to_slave();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hosted_connect_to_slave failed: %s", esp_err_to_name(ret));
        return ret;
    }

    bits = xEventGroupWaitBits(s_app_events,
            required_bits,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(HOSTED_CONNECT_TIMEOUT_MS));
    if ((bits & required_bits) != required_bits) {
        ESP_LOGE(TAG,
                "timed out waiting for CP_INIT + TRANSPORT_UP, bits=0x%02" PRIx32,
                (uint32_t)bits);
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG,
            "hosted transport ready: cp_init=%" PRIu32 ", transport_up=%" PRIu32,
            s_cp_init_count,
            s_transport_up_count);
    return ESP_OK;
}

static esp_err_t configure_and_start_softap(void)
{
    EventBits_t bits;
    wifi_config_t ap_config = { 0 };
    size_t password_len = strlen(CONFIG_EXAMPLE_SOFTAP_PASSWORD);

    if (password_len > 0 && password_len < 8) {
        ESP_LOGE(TAG, "SoftAP password must be empty or at least 8 characters");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(wifi_cmd_wifi_init(NULL));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "%s", CONFIG_EXAMPLE_SOFTAP_SSID);
    snprintf((char *)ap_config.ap.password, sizeof(ap_config.ap.password), "%s", CONFIG_EXAMPLE_SOFTAP_PASSWORD);
    ap_config.ap.ssid_len = strlen(CONFIG_EXAMPLE_SOFTAP_SSID);
    ap_config.ap.channel = CONFIG_EXAMPLE_SOFTAP_CHANNEL;
    ap_config.ap.max_connection = CONFIG_EXAMPLE_SOFTAP_MAX_CONN;
    ap_config.ap.authmode = password_len == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    ESP_LOGI(TAG,
            "starting SoftAP: ssid=%s channel=%d max_conn=%d auth=%s",
            CONFIG_EXAMPLE_SOFTAP_SSID,
            CONFIG_EXAMPLE_SOFTAP_CHANNEL,
            CONFIG_EXAMPLE_SOFTAP_MAX_CONN,
            password_len == 0 ? "open" : "wpa2-psk");

    xEventGroupClearBits(s_app_events, BIT_SOFTAP_STARTED);
    ESP_ERROR_CHECK(wifi_cmd_wifi_start());

    bits = xEventGroupWaitBits(s_app_events,
            BIT_SOFTAP_STARTED,
            pdFALSE,
            pdTRUE,
            pdMS_TO_TICKS(SOFTAP_START_TIMEOUT_MS));
    if ((bits & BIT_SOFTAP_STARTED) == 0) {
        ESP_LOGE(TAG, "timed out waiting for WIFI_EVENT_AP_START");
        return ESP_ERR_TIMEOUT;
    }

    log_softap_ip_info();
    return ESP_OK;
}

static esp_err_t init_console_repl(esp_console_repl_t **repl)
{
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();

    repl_config.prompt = "iperf>";

#if CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    return esp_console_new_repl_uart(&uart_config, &repl_config, repl);
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t cdc_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    return esp_console_new_repl_usb_cdc(&cdc_config, &repl_config, repl);
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t usbjtag_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    return esp_console_new_repl_usb_serial_jtag(&usbjtag_config, &repl_config, repl);
#else
    *repl = NULL;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static void register_console_commands(void)
{
    register_system();
    ESP_ERROR_CHECK(wifi_cmd_register_all());
    app_register_iperf_commands();
    app_register_iperf_hook_func(iperf_hook_show_wifi_stats);
    ping_cmd_register_ping();
}

static void enable_wifi_statistics(void)
{
#if CONFIG_ESP_WIFI_ENABLE_WIFI_RX_STATS
#if CONFIG_ESP_WIFI_ENABLE_WIFI_RX_MU_STATS
    esp_wifi_enable_rx_statistics(true, true);
#else
    esp_wifi_enable_rx_statistics(true, false);
#endif
#endif
#if CONFIG_ESP_WIFI_ENABLE_WIFI_TX_STATS
    esp_wifi_enable_tx_statistics(ESP_WIFI_ACI_BE, true);
#endif
}

static void print_banner(void)
{
    printf("\n ==================================================\n");
    printf(" |   Hosted iPerf SoftAP validation is ready       |\n");
    printf(" |                                                 |\n");
    printf(" |  1. Host boots slave through esp_hosted         |\n");
    printf(" |  2. Wait for CP_INIT and TRANSPORT_UP           |\n");
    printf(" |  3. SoftAP starts on the slave                  |\n");
    printf(" |  4. Run iperf from console or remote control    |\n");
    printf(" |                                                 |\n");
    printf(" ==================================================\n\n");
}

void app_main(void)
{
    esp_console_repl_t *repl = NULL;

    ESP_LOGI(TAG, "ESP-Hosted host framework iperf validation example");

    ESP_ERROR_CHECK(init_nvs_flash());
    ESP_ERROR_CHECK(init_default_event_loop());

    s_app_events = xEventGroupCreate();
    assert(s_app_events);

    ESP_ERROR_CHECK(register_event_handlers());
    ESP_ERROR_CHECK(bring_up_hosted_transport());
    ESP_ERROR_CHECK(configure_and_start_softap());

    enable_wifi_statistics();
    ESP_ERROR_CHECK(init_console_repl(&repl));
    register_console_commands();

#if CONFIG_EXAMPLE_ENABLE_IPERF_REMOTE_CONTROL
    ESP_ERROR_CHECK(iperf_remote_control_start());
#endif

    print_banner();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}