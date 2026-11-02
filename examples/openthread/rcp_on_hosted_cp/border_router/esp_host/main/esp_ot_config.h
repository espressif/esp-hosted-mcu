/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * OpenThread Border Router Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#pragma once
#include "sdkconfig.h"
#include "esp_coexist.h"
#include "esp_openthread_types.h"

#if CONFIG_OPENTHREAD_RADIO_NATIVE
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()              \
    {                                                      \
        .radio_mode = RADIO_MODE_NATIVE,                   \
    }
#elif CONFIG_OPENTHREAD_RADIO_SPINEL_UART
/* Standard OpenThread over a dedicated UART: ESP-Hosted does not carry spinel. */
_Static_assert(CONFIG_EXAMPLE_HOST_UART_TX_PIN >= 0 && CONFIG_EXAMPLE_HOST_UART_RX_PIN >= 0,
               "Dedicated spinel UART: set EXAMPLE_HOST_UART_TX_PIN and EXAMPLE_HOST_UART_RX_PIN");
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                          \
    {                                                                  \
        .radio_mode = RADIO_MODE_UART_RCP,                             \
        .radio_uart_config = {                                         \
            .port = CONFIG_EXAMPLE_HOST_UART_PORT,                     \
            .uart_config =                                             \
                {                                                      \
                    .baud_rate = CONFIG_EXAMPLE_HOST_UART_BAUDRATE,    \
                    .data_bits = UART_DATA_8_BITS,                     \
                    .parity = UART_PARITY_DISABLE,                     \
                    .stop_bits = UART_STOP_BITS_1,                     \
                    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,             \
                    .rx_flow_ctrl_thresh = 0,                          \
                    .source_clk = UART_SCLK_DEFAULT,                   \
                },                                                     \
            .rx_pin = CONFIG_EXAMPLE_HOST_UART_RX_PIN,                 \
            .tx_pin = CONFIG_EXAMPLE_HOST_UART_TX_PIN,                 \
        },                                                             \
    }
#elif CONFIG_OPENTHREAD_RADIO_SPINEL_CUSTOM
/* Spinel over the ESP-Hosted transport (ESP_AUX_IF). */
#if !CONFIG_ESP_HOSTED_HOST_FEAT_OPENTHREAD_READY
#error "Spinel over ESP-Hosted: enable CONFIG_ESP_HOSTED_HOST_FEAT_OPENTHREAD"
#endif
#include "esp_hosted_openthread.h"
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG() ESP_HOSTED_OPENTHREAD_RADIO_CONFIG()
#else
#error "Set CONFIG_OPENTHREAD_RADIO_SPINEL_CUSTOM (ESP-Hosted) or CONFIG_OPENTHREAD_RADIO_SPINEL_UART"
#endif

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                        \
    {                                                               \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,          \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()    \
    {                                           \
        .storage_partition_name = "nvs",        \
        .netif_queue_size = 10,                 \
        .task_queue_size = 10,                  \
    }
