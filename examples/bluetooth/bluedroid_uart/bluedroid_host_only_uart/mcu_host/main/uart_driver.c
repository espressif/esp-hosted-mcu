/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_bluedroid_hci.h"
#include "uart_driver.h"

#define TAG "UART_HCI"

#define UART_NO       (1)
#define UART_BUF_SZ   (1024)

#define UART_TX_PIN   (CONFIG_EXAMPLE_HCI_UART_TX_PIN)
#define UART_RX_PIN   (CONFIG_EXAMPLE_HCI_UART_RX_PIN)
#if CONFIG_EXAMPLE_HCI_UART_FLOW_CTRL_EN
#define UART_RTS_PIN  (CONFIG_EXAMPLE_HCI_UART_RTS_PIN)
#define UART_CTS_PIN  (CONFIG_EXAMPLE_HCI_UART_CTS_PIN)
#define UART_FLOW_CTRL UART_HW_FLOWCTRL_CTS_RTS
#else
#define UART_RTS_PIN  (UART_PIN_NO_CHANGE)
#define UART_CTS_PIN  (UART_PIN_NO_CHANGE)
#define UART_FLOW_CTRL UART_HW_FLOWCTRL_DISABLE
#endif

enum {
    UART_RX_TYPE = 0,
    UART_RX_LEN,
    UART_RX_DATA,
};

enum {
    DATA_TYPE_COMMAND = 1,
    DATA_TYPE_ACL     = 2,
    DATA_TYPE_SCO     = 3,
    DATA_TYPE_EVENT   = 4
};

#define H4_ACL_HDR_LEN (4)
#define H4_SCO_HDR_LEN (3)
#define H4_EVT_HDR_LEN (2)
#define H4_BUF_SZ      (1026)
#define H4_ACL_PAYLOAD_MAX (H4_BUF_SZ - 1 - H4_ACL_HDR_LEN)

_Static_assert(H4_BUF_SZ >= 1 + H4_SCO_HDR_LEN + UINT8_MAX, "SCO and EVT must fit the RX buffer");

TaskHandle_t s_rx_task_hdl;
esp_bluedroid_hci_driver_callbacks_t s_callback = { 0 };

/* H4 has no frame delimiter: after a bad type or length nothing in the RX
 * FIFO can be trusted, so drop it. Bluedroid has no transport hook to reset
 * the controller. */
static void hci_uart_sync_lost(uint8_t type, uint32_t len)
{
    static TickType_t s_last_log;
    TickType_t now = xTaskGetTickCount();

    uart_flush_input(UART_NO);

    if (s_last_log == 0 || now - s_last_log >= pdMS_TO_TICKS(1000)) {
        s_last_log = now;
        ESP_LOGE(TAG, "H4 sync lost (type 0x%02x len %" PRIu32 "), RX flushed", type, len);
    }
}

static void IRAM_ATTR hci_uart_rx_task(void *arg)
{
    uint8_t buf[H4_BUF_SZ];
    int len_now_read;
    uint32_t len_to_read = 1;
    uint32_t len_total_read = 0;
    uint8_t rx_st = UART_RX_TYPE;

    while (1) {
        len_now_read = uart_read_bytes(UART_NO, &buf[len_total_read], len_to_read, portMAX_DELAY);
        assert(len_now_read == len_to_read);
        len_total_read += len_now_read;

        switch (rx_st) {
        case UART_RX_TYPE:
            if (buf[0] == DATA_TYPE_ACL) {
                len_to_read = H4_ACL_HDR_LEN;
            } else if (buf[0] == DATA_TYPE_SCO) {
                len_to_read = H4_SCO_HDR_LEN;
            } else if (buf[0] == DATA_TYPE_EVENT) {
                len_to_read = H4_EVT_HDR_LEN;
            } else {
                hci_uart_sync_lost(buf[0], 0);
                break;
            }
            rx_st = UART_RX_LEN;
            continue;

        case UART_RX_LEN:
            if (buf[0] == DATA_TYPE_ACL) {
                len_to_read = buf[3] | (buf[4] << 8);
                if (len_to_read > H4_ACL_PAYLOAD_MAX) {
                    hci_uart_sync_lost(buf[0], len_to_read);
                    break;
                }
            } else if (buf[0] == DATA_TYPE_SCO) {
                len_to_read = buf[3];
            } else {
                len_to_read = buf[2];
            }
            rx_st = UART_RX_DATA;
            if (len_to_read) {
                continue;
            }
            /* fall through */

        case UART_RX_DATA:
            if (s_callback.notify_host_recv) {
                s_callback.notify_host_recv(buf, len_total_read);
            }
            break;
        }

        rx_st = UART_RX_TYPE;
        len_to_read = 1;
        len_total_read = 0;
    }

    vTaskDelete(NULL);
}

void hci_uart_send(uint8_t *buf, uint16_t len)
{
    uint8_t *p = buf;
    int len_write = 0;

    while (len) {
        len_write = uart_write_bytes(UART_NO, p, len);
        assert(len_write > 0);
        len -= len_write;
        p += len_write;
    }
}

bool hci_check_send_available(void)
{
    return true;
}

esp_err_t hci_register_host_callback(const esp_bluedroid_hci_driver_callbacks_t *callback)
{
    s_callback.notify_host_send_available = callback->notify_host_send_available;
    s_callback.notify_host_recv = callback->notify_host_recv;

    return ESP_OK;
}

void hci_uart_open(void)
{
    uart_config_t uart_config = {
        .baud_rate  = CONFIG_EXAMPLE_HCI_UART_BAUDRATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_FLOW_CTRL,
        .source_clk = UART_SCLK_DEFAULT,
        .rx_flow_ctrl_thresh = UART_HW_FIFO_LEN(UART_NO) - 1,
    };

    int intr_alloc_flags = 0;
#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(UART_NO, UART_BUF_SZ * 2, UART_BUF_SZ * 2, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(UART_NO, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NO, UART_TX_PIN, UART_RX_PIN, UART_RTS_PIN, UART_CTS_PIN));
    /* uart_set_pin() leaves RX without pull: a floating line reads as noise */
    ESP_ERROR_CHECK(gpio_set_pull_mode(UART_RX_PIN, GPIO_PULLUP_ONLY));
    /* Sync point: callers open the UART right before the host sends HCI Reset,
     * so nothing received yet is a reply */
    ESP_ERROR_CHECK(uart_flush_input(UART_NO));

    xTaskCreate(hci_uart_rx_task, "hci_uart_rx_task", 2048, NULL, 12, &s_rx_task_hdl);
}

void hci_uart_close(void)
{
    if (s_rx_task_hdl) {
        vTaskDelete(s_rx_task_hdl);
    }
    uart_driver_delete(UART_NO);
    memset(&s_callback, 0, sizeof(esp_bluedroid_hci_driver_callbacks_t));
}
