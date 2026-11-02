/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Three-chip OpenThread Border Router Example
 *
 *   ESP-Hosted co-processor (cp/)  : Wi-Fi backhaul + BLE, over SDIO / SPI
 *   802.15.4 RCP (cp_rcp/)         : Thread radio, spinel over a dedicated UART
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_spinel.h"
#include "esp_openthread_types.h"
#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
#include "esp_ot_cli_extension.h"
#endif // CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "ot_examples_br.h"
#include "ot_examples_common.h"

#include "esp_hosted.h"
#include "app_demo.h"
#include "bleprph.h"

#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
#include "ot_led_strip.h"
#endif

#define TAG "esp_ot_br_3chip"

#if CONFIG_EXAMPLE_RCP_RESET_PIN >= 0
#define PIN_TO_RCP_RESET CONFIG_EXAMPLE_RCP_RESET_PIN
static void rcp_failure_hardware_reset_handler(void)
{
    ESP_LOGW(TAG, "RCP failure: resetting RCP via GPIO %d", PIN_TO_RCP_RESET);
    gpio_config_t reset_pin_config;
    memset(&reset_pin_config, 0, sizeof(reset_pin_config));
    reset_pin_config.intr_type = GPIO_INTR_DISABLE;
    reset_pin_config.pin_bit_mask = BIT(PIN_TO_RCP_RESET);
    reset_pin_config.mode = GPIO_MODE_OUTPUT;
    reset_pin_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    reset_pin_config.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&reset_pin_config);
    gpio_set_level(PIN_TO_RCP_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TO_RCP_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_reset_pin(PIN_TO_RCP_RESET);
}
#endif

static esp_err_t app_init_esp_hosted(void)
{
    ESP_RETURN_ON_ERROR(esp_hosted_init(), TAG, "esp_hosted_init failed");
    ESP_RETURN_ON_ERROR(esp_hosted_connect_to_slave(), TAG, "esp_hosted_connect_to_slave failed");
    return ESP_OK;
}

void app_main(void)
{
    if (app_init_esp_hosted() != ESP_OK) {
        return;
    }

    // Used eventfds:
    // * netif
    // * task queue
    // * border router
    size_t max_eventfd = 3;

#if CONFIG_OPENTHREAD_RADIO_TREL
    // * TREL reception (The Thread Radio Encapsulation Link needs a eventfd for reception.)
    max_eventfd++;
#endif
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = max_eventfd,
    };
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp-ot-br"));
#if CONFIG_EXAMPLE_RCP_RESET_PIN >= 0
    esp_openthread_register_rcp_failure_handler(rcp_failure_hardware_reset_handler);
    esp_openthread_set_coprocessor_reset_failure_callback(rcp_failure_hardware_reset_handler);
#else
    ESP_LOGW(TAG, "RCP hardware reset off: EXAMPLE_RCP_RESET_PIN is -1");
#endif

    ESP_ERROR_CHECK(ble_prph_start());

#if CONFIG_OPENTHREAD_CLI
    ot_console_start();
    ot_register_external_commands();
    ESP_ERROR_CHECK(app_demo_register_commands());
#endif

    static esp_openthread_config_t config = {
        .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
        .platform_config = {
            .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
            .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
            .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
        },
    };

    ESP_ERROR_CHECK(esp_openthread_start(&config));
    ESP_ERROR_CHECK(app_demo_start());
#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
    esp_cli_custom_command_init();
#endif
#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
    ESP_ERROR_CHECK(esp_openthread_state_indicator_init(esp_openthread_get_instance()));
#endif
#if CONFIG_OPENTHREAD_BORDER_ROUTER && CONFIG_OPENTHREAD_NETWORK_AUTO_START
    /* Connects Wi-Fi, then brings up border routing on that backbone. */
    ESP_ERROR_CHECK(esp_openthread_border_router_start());
    /* Joins the stored dataset, or forms one from the Kconfig dataset. */
    ot_network_auto_start();
#endif
}
