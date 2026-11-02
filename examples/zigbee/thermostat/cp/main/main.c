/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "esp_check.h"
#include "esp_coexist.h"
#include "esp_openthread.h"
#include "esp_openthread_types.h"
#include "esp_vfs_eventfd.h"

static const char *TAG = "zb_rcp_cp";

#if !CONFIG_OPENTHREAD_RCP_UART
#error "The Zigbee host speaks spinel only on a UART: set CONFIG_OPENTHREAD_RCP_UART"
#endif

/* Standard OpenThread RCP on a dedicated UART, as ESP-IDF
 * examples/openthread/ot_rcp. ESP-Hosted does not carry spinel here. */
_Static_assert(CONFIG_EXAMPLE_RCP_UART_TX_PIN >= 0 && CONFIG_EXAMPLE_RCP_UART_RX_PIN >= 0,
               "Dedicated spinel UART: set EXAMPLE_RCP_UART_TX_PIN and EXAMPLE_RCP_UART_RX_PIN");
#if CONFIG_EH_TRANSPORT_CP_UART
_Static_assert(CONFIG_EXAMPLE_RCP_UART_PORT != CONFIG_EH_TRANSPORT_CP_UART_PORT,
               "Spinel UART and the ESP-Hosted UART transport use the same port");
#endif

static void ot_rcp_uart_start(void)
{
    /* ot task queue + radio driver */
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 2,
    };
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

    static esp_openthread_config_t config = {
        .netif_config = {0},
        .platform_config = {
            .radio_config = {
                .radio_mode = RADIO_MODE_NATIVE,
            },
            .host_config = {
                .host_connection_mode = HOST_CONNECTION_MODE_RCP_UART,
                .host_uart_config = {
                    .port = CONFIG_EXAMPLE_RCP_UART_PORT,
                    .uart_config = {
                        .baud_rate = CONFIG_EXAMPLE_RCP_UART_BAUDRATE,
                        .data_bits = UART_DATA_8_BITS,
                        .parity = UART_PARITY_DISABLE,
                        .stop_bits = UART_STOP_BITS_1,
                        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                        .rx_flow_ctrl_thresh = 0,
                        .source_clk = UART_SCLK_DEFAULT,
                    },
                    .rx_pin = CONFIG_EXAMPLE_RCP_UART_RX_PIN,
                    .tx_pin = CONFIG_EXAMPLE_RCP_UART_TX_PIN,
                },
            },
            .port_config = {
                .storage_partition_name = "nvs",
                .netif_queue_size = 10,
                .task_queue_size = 10,
            },
        },
    };

    ESP_LOGI(TAG, "OpenThread RCP on UART%d", CONFIG_EXAMPLE_RCP_UART_PORT);
    ESP_ERROR_CHECK(esp_openthread_start(&config));
#if CONFIG_ESP_COEX_SW_COEXIST_ENABLE && CONFIG_SOC_IEEE802154_SUPPORTED
    /* Wi-Fi and 802.15.4 share this chip's radio. */
    esp_err_t cret = esp_coex_wifi_i154_enable();
    if (cret != ESP_OK)
        ESP_LOGW(TAG, "Wi-Fi/802.15.4 coexistence not enabled: %s", esp_err_to_name(cret));
#endif
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-Hosted co-processor with a Zigbee (802.15.4) RCP starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ot_rcp_uart_start();
}
