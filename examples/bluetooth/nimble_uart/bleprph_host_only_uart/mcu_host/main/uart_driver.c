/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "uart_driver.h"
#include "nimble/hci_common.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"

#define TAG "UART_HCI"

#define UART_NO                  (1)
#define UART_BUF_SZ              (1024)

#define UART_TX_PIN              (CONFIG_EXAMPLE_HCI_UART_TX_PIN)
#define UART_RX_PIN              (CONFIG_EXAMPLE_HCI_UART_RX_PIN)
#define UART_RTS_PIN             (CONFIG_EXAMPLE_HCI_UART_RTS_PIN)
#define UART_CTS_PIN             (CONFIG_EXAMPLE_HCI_UART_CTS_PIN)
#define HCI_H4_ACL               (0x02)
#define HCI_H4_CMD               (0x01)
#define HCI_H4_EVT               (0x04)
#define BLE_HCI_EVENT_HDR_LEN    (2)
#define BLE_HCI_CMD_HDR_LEN      (3)

enum {
    UART_RX_TYPE = 0,
    UART_RX_LEN,
    UART_RX_DATA,
};

enum {
    DATA_TYPE_COMMAND = 1,
    DATA_TYPE_ACL     = 2,
    DATA_TYPE_EVENT   = 4
};

#define H4_BUF_SZ                (1026)
#define H4_ACL_PAYLOAD_MAX       (H4_BUF_SZ - 1 - BLE_HCI_DATA_HDR_SZ)

_Static_assert(H4_BUF_SZ >= 1 + BLE_HCI_EVENT_HDR_LEN + UINT8_MAX, "EVT must fit the RX buffer");

TaskHandle_t s_rx_task_hdl;
static volatile bool s_shutdown_flag = false;

static void hci_uart_to_host(uint8_t *data, uint32_t len)
{
    int rc;

    if (data[0] == HCI_H4_EVT) {
        uint8_t *evbuf;
        int totlen = BLE_HCI_EVENT_HDR_LEN + data[2];

        if (totlen > MYNEWT_VAL(BLE_TRANSPORT_EVT_SIZE)) {
            ESP_LOGE(TAG, "Received HCI data length at host (%d)"
                     "exceeds maximum configured HCI event buffer size (%d).",
                     totlen, MYNEWT_VAL(BLE_TRANSPORT_EVT_SIZE));
            return;
        }

        /* Allocate LE Advertising Report Event from lo pool only */
        if ((data[1] == BLE_HCI_EVCODE_LE_META) &&
                (data[3] == BLE_HCI_LE_SUBEV_ADV_RPT || data[3] == BLE_HCI_LE_SUBEV_EXT_ADV_RPT)) {
            evbuf = ble_transport_alloc_evt(1);
            /* Skip advertising report if we're out of memory */
            if (!evbuf) {
                ESP_LOGE(TAG, "No buffers");
                return;
            }
        } else {
            evbuf = ble_transport_alloc_evt(0);
            assert(evbuf != NULL);
        }

        memcpy(evbuf, &data[1], totlen);

        rc = ble_transport_to_hs_evt(evbuf);
        assert(rc == 0);
    } else {
        struct os_mbuf *m = ble_transport_alloc_acl_from_ll();

        if (!m) {
            ESP_LOGE(TAG, "No buffers");
            return;
        }

        if ((rc = os_mbuf_append(m, &data[1], len - 1)) != 0) {
            ESP_LOGE(TAG, "%s failed to os_mbuf_append; rc = %d", __func__, rc);
            os_mbuf_free_chain(m);
            return;
        }

        ble_transport_to_hs_acl(m);
    }
}

/* H4 has no frame delimiter: after a bad type or length nothing in the RX
 * FIFO can be trusted. Drop it and let the host reset the controller. */
static void hci_uart_sync_lost(uint8_t type, uint32_t len)
{
    static TickType_t s_last_log;
    TickType_t now = xTaskGetTickCount();

    uart_flush_input(UART_NO);

    if (s_last_log == 0 || now - s_last_log >= pdMS_TO_TICKS(1000)) {
        s_last_log = now;
        ESP_LOGE(TAG, "H4 sync lost (type 0x%02x len %" PRIu32 "), resetting controller", type, len);
    }

    /* Before sync, NimBLE's own startup retry resends HCI Reset. */
    if (ble_hs_synced()) {
        ble_hs_sched_reset(BLE_HS_ECONTROLLER);
    }
}

static void IRAM_ATTR hci_uart_rx_task(void *arg)
{
    uint8_t buf[H4_BUF_SZ];
    int len_now_read;
    uint32_t len_to_read = 1;
    uint32_t len_total_read = 0;
    uint8_t rx_st = UART_RX_TYPE;

    while (!s_shutdown_flag) {
        /* Bounded wait so the shutdown flag is polled */
        len_now_read = uart_read_bytes(UART_NO, &buf[len_total_read], len_to_read, pdMS_TO_TICKS(100));
        if (len_now_read <= 0) {
            continue;
        }

        len_total_read += len_now_read;
        len_to_read -= len_now_read;
        if (len_to_read) {
            continue;
        }

        switch (rx_st) {
        case UART_RX_TYPE:
            if (buf[0] == DATA_TYPE_ACL) {
                len_to_read = BLE_HCI_DATA_HDR_SZ;
            } else if (buf[0] == DATA_TYPE_EVENT) {
                len_to_read = BLE_HCI_EVENT_HDR_LEN;
            } else {
                hci_uart_sync_lost(buf[0], 0);
                break;
            }
            rx_st = UART_RX_LEN;
            continue;

        case UART_RX_LEN:
            if (buf[0] == DATA_TYPE_ACL) {
                len_to_read = buf[3] | (buf[4] << 8);
                if (len_to_read > H4_ACL_PAYLOAD_MAX ||
                        len_to_read > MYNEWT_VAL(BLE_TRANSPORT_ACL_SIZE)) {
                    hci_uart_sync_lost(buf[0], len_to_read);
                    break;
                }
            } else {
                len_to_read = buf[2];
            }
            rx_st = UART_RX_DATA;
            if (len_to_read) {
                continue;
            }
            /* fall through */

        case UART_RX_DATA:
            hci_uart_to_host(buf, len_total_read);
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


void
ble_transport_ll_init(void)
{
    /* NimBLE calls this after its host pools exist and before its first HCI
     * command (HCI Reset), so hci_uart_open() is the H4 sync point. */
    hci_uart_open();
}

void
ble_transport_ll_deinit(void)
{
    hci_uart_close();
}

int
ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    uint8_t buf[OS_MBUF_PKTLEN(om) + 1];
    int rc;
    buf[0] = HCI_H4_ACL;
    rc = ble_hs_mbuf_to_flat(om, buf + 1, OS_MBUF_PKTLEN(om), NULL);
    if(rc) {
        ESP_LOGE(TAG, "Error copying data %d", rc);
        os_mbuf_free_chain(om);
        return rc;
    }
    hci_uart_send(buf, OS_MBUF_PKTLEN(om) + 1);
    os_mbuf_free_chain(om);
    return 0;
}

int
ble_transport_to_ll_cmd_impl(void *buf)
{
    int len = 3 + ((uint8_t *)buf)[2] + 1;
    uint8_t data[258];
    data[0] = HCI_H4_CMD;
    memcpy(data + 1, buf, len - 1);
    hci_uart_send(data, len);
#if MYNEWT_VAL(MP_RUNTIME_ALLOC)
    ble_transport_free(BLE_HCI_CMD, buf);
#else
    ble_transport_free(buf);
#endif
    return 0;
}

void hci_uart_open(void)
{
    uart_config_t uart_config = {
        .baud_rate  = CONFIG_EXAMPLE_HCI_UART_BAUDRATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = CONFIG_EXAMPLE_HCI_UART_FLOW_CTRL,
        .source_clk = UART_SCLK_DEFAULT,
    };

    int intr_alloc_flags = 0;
#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    s_shutdown_flag = false;  // Reset shutdown flag on open
    ESP_ERROR_CHECK(uart_driver_install(UART_NO, UART_BUF_SZ * 2, UART_BUF_SZ * 2, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(UART_NO, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NO, UART_TX_PIN, UART_RX_PIN, -1, -1));
    /* uart_set_pin() leaves RX without pull: a floating line reads as noise */
    ESP_ERROR_CHECK(gpio_set_pull_mode(UART_RX_PIN, GPIO_PULLUP_ONLY));
    /* Sync point: the host has not sent HCI Reset yet, so nothing received is a reply */
    ESP_ERROR_CHECK(uart_flush_input(UART_NO));

    xTaskCreate(hci_uart_rx_task, "hci_uart_rx_task", 2048, NULL, 12, &s_rx_task_hdl);
}

void hci_uart_close(void)
{
    if (s_rx_task_hdl) {

        s_shutdown_flag = true;

        int wait_count = 0;
        const int max_wait_count = 5;
        TaskHandle_t task_handle = s_rx_task_hdl;

        while (wait_count < max_wait_count) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;

        }


        if (s_rx_task_hdl == task_handle) {
            vTaskDelete(s_rx_task_hdl);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        s_rx_task_hdl = NULL;
    }

    uart_driver_delete(UART_NO);
}
