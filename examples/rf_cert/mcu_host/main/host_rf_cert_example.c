/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * RF certification test — host side.
 *
 * The host owns the whole test. It brings up the hosted link, then hands
 * you a console: every step is a command you type, so a lab engineer
 * drives the sweep and reads each result before moving on. The
 * co-processor only answers RPCs.
 *
 * The console matches the ESP-IDF phy/cert_test example: same prompt, same
 * command names, same banner shape, so commands and scripts carry over.
 *
 * Cert mode is entered at start-up, as cert_test does from app_main.  That
 * only initialises the PHY test library and powers the Wi-Fi domain on;
 * nothing transmits until a test command is given.
 *
 * The commands live in the host CLI feature
 * (host/features/eh_host_feat_cli), not in this file, so any application
 * that enables CONFIG_ESP_HOSTED_HOST_FEAT_CLI gets them.
 *
 * For conducted RF measurement only.
 */

#include <stdio.h>

#include "esp_console.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_hosted.h"
#include "eh_host_cli.h"
#include "esp_phy_remote.h"

#include "rf_cert_ota_cmd.h"

static const char *TAG = "rf_cert_example";

static void banner(void)
{
    printf("\n  RF certification test — conducted measurement only\n");
    eh_host_feat_cli_rf_cert_overview();
    rf_cert_ota_print_hint();
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_hosted_init());
    ESP_LOGI(TAG, "ESP-Hosted initialized successfully");

    /* Ask the co-processor whether it can do this at all, so a wrong
     * co-processor build is obvious now and not ten commands later. */
    if (esp_phy_remote_query(ESP_PHY_REMOTE_QUERY_CONFIGURED) != ESP_OK) {
        ESP_LOGE(TAG, "co-processor has no RF cert support");
        ESP_LOGE(TAG, "rebuild it with CONFIG_ESP_HOSTED_CP_FEAT_RF_CERT=y "
                      "and CONFIG_ESP_PHY_ENABLE_CERT_TEST=y");
        ESP_LOGW(TAG, "the console still starts, so phy_cert_query works");
    } else {
        ESP_LOGI(TAG, "co-processor RF cert support: present");
    }

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "phy>";
    repl_cfg.max_cmdline_length = 256;

#if CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t jtag_cfg =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&jtag_cfg, &repl_cfg, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t cdc_cfg = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&cdc_cfg, &repl_cfg, &repl));
#else
#error "no console device selected — set CONFIG_ESP_CONSOLE_UART or a USB console"
#endif

    ESP_ERROR_CHECK(esp_console_register_help_command());
    ESP_ERROR_CHECK(eh_host_feat_cli_register_commands());
    rf_cert_ota_register();

    /* cert_test enters cert mode from app_main, so its console has no init
     * command; do the same, or a first test command fails INVALID_STATE. */
    if (esp_phy_remote_rftest_init() != ESP_OK) {
        ESP_LOGE(TAG, "could not enter RF cert mode on the coprocessor");
    }

    banner();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
