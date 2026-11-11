/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eh_host_port_master_config.h"

#if defined(ESP_PLATFORM) && EH_HOST_TRANSPORT_BUS_USB && EH_HOST_USB_RNDIS

#if CONFIG_USBH_CDC_CONTROL_TRANSFER_BUFFER_SIZE < 2048 || \
		CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE < 2048
#error "USB RNDIS needs 2048-byte control transfers for the supported-OID list. A saved sdkconfig may still have the 256-byte default."
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "iot_eth.h"
#include "iot_usbh_rndis.h"

#include "eh_common_interface.h"
#include "eh_host_mcu_transport_priv.h"
#include "eh_host_pkt_stats.h"
#include "eh_host_port.h"
#include "eh_host_port_sync.h"
#include "eh_host_bus_usb_priv.h"

#define TAG "eh_bus_rndis"
#define USB_ETH_HDR_LEN 14
#define USB_ETH_SRC_OFF 6
#define USB_RNDIS_VID EH_HOST_PORT_USB_VID
#define USB_RNDIS_PID EH_HOST_PORT_USB_PID

#define TX_TASK_STACK_SIZE                EH_HOST_DEFLT_TASK_STACK_BYTES
#define TX_TASK_PRIO                      EH_HOST_DEFLT_TASK_PRIORITY
#define RX_TASK_STACK_SIZE                EH_HOST_DEFLT_TASK_STACK_BYTES
#define RX_TASK_PRIO                      EH_HOST_DEFLT_TASK_PRIORITY

/* A full queue returns within this time after s_running is cleared, so
 * stop can destroy the queue once every sender has left queue_send. */
#define EH_USB_ENQUEUE_MS                 100

static const usb_device_match_id_t s_rndis_match[] = {
    {
        .match_flags = USB_DEVICE_ID_MATCH_VID_PID,
        .idVendor = USB_RNDIS_VID,
        .idProduct = USB_RNDIS_PID,
    },
    { 0 },
};

typedef struct {
    uint8_t *data;
    uint16_t len;
} rndis_rx_item_t;

static eh_host_port_queue_t *s_tx_q;
static eh_host_port_queue_t *s_rx_q;
static eh_host_port_task_t *s_tx_task;
static eh_host_port_task_t *s_rx_task;
static eh_host_port_mutex_t *s_tx_wait_lock;
static uint32_t s_tx_waiters;
static iot_eth_handle_t s_eth;
static volatile bool s_running;
static uint8_t s_ap_mac[6];
static bool s_ap_mac_valid;
static volatile eh_host_usb_rndis_route_t s_route = EH_HOST_USB_RNDIS_ROUTE_STA;

static uint8_t rndis_rx_if_type(const uint8_t *data)
{
    if (s_route == EH_HOST_USB_RNDIS_ROUTE_AP) {
        return ESP_AP_IF;
    }
    if (s_route == EH_HOST_USB_RNDIS_ROUTE_BOTH && s_ap_mac_valid &&
        memcmp(data + USB_ETH_SRC_OFF, s_ap_mac, sizeof(s_ap_mac)) == 0) {
        return ESP_AP_IF;
    }
    return ESP_STA_IF;
}

static void free_bh(interface_buffer_handle_t *bh)
{
    if (bh && bh->free_buf_handle) {
        bh->free_buf_handle(bh->priv_buffer_handle);
    }
}

/*
 * iot_usbh_rndis transfers ownership of data. The common dispatcher copies
 * it before handing it to the hosted Wi-Fi channel, so release the original
 * buffer on every path. Normally that copy runs on eh_rndis_rx so the usbh_cdc
 * client task can complete the bulk OUT; if s_rx_q is full, dispatch falls back
 * to running inline.
 */
static void rndis_rx_dispatch(uint8_t *data, uint16_t len)
{
    interface_buffer_handle_t bh = {
        .if_type = rndis_rx_if_type(data),
        .payload = data,
        .payload_len = len,
    };
    eh_host_mcu_transport_dispatch_frame(&bh);
    free(data);
#if EH_HOST_PKT_STATS
    pkt_stats.usb_rndis_rx_delivered++;
#endif
}

static void rndis_rx_task(void *arg)
{
    (void)arg;
    while (s_running) {
        rndis_rx_item_t item = {0};
        if (eh_host_port_queue_receive(s_rx_q, &item, EH_HOST_PORT_WAIT_FOREVER) != EH_HOST_PORT_OK) {
            continue;
        }
        if (!s_running || !item.data) {
            free(item.data);
            break;
        }
        rndis_rx_dispatch(item.data, item.len);
    }
}

static esp_err_t rndis_rx(iot_eth_handle_t handle, uint8_t *data, size_t len, void *ctx)
{
    rndis_rx_item_t item;
    (void)handle;
    (void)ctx;
    if (!data || len < USB_ETH_HDR_LEN || len > EH_USB_MAX_BUF) {
        ESP_LOGW(TAG, "drop malformed RNDIS frame len=%u", (unsigned int)len);
#if EH_HOST_PKT_STATS
        pkt_stats.usb_rndis_rx_dropped++;
#endif
        free(data);
        return ESP_ERR_INVALID_SIZE;
    }

    item.data = data;
    item.len = (uint16_t)len;
    if (!s_rx_q || !s_running ||
        eh_host_port_queue_send(s_rx_q, &item, 0) != EH_HOST_PORT_OK) {
        rndis_rx_dispatch(data, item.len);
        return ESP_OK;
    }
    return ESP_OK;
}

void eh_host_mcu_transport_usb_rndis_set_ap_mac(const uint8_t mac[6])
{
    if (!mac) {
        return;
    }
    memcpy(s_ap_mac, mac, sizeof(s_ap_mac));
    s_ap_mac_valid = true;
}

eh_host_usb_rndis_route_t eh_host_mcu_transport_usb_rndis_set_route(eh_host_usb_rndis_route_t route)
{
    eh_host_usb_rndis_route_t prev = s_route;

    s_route = route;
    return prev;
}

static void rndis_tx_task(void *arg)
{
    (void)arg;
    while (s_running) {
        interface_buffer_handle_t bh = {0};
        if (eh_host_port_queue_receive(s_tx_q, &bh, EH_HOST_PORT_WAIT_FOREVER) != EH_HOST_PORT_OK) {
            continue;
        }
        if (!s_running) {
            free_bh(&bh);
            break;
        }
        if (!bh.payload || !bh.payload_len || !s_eth) {
            free_bh(&bh);
            continue;
        }

        esp_err_t err = iot_eth_transmit(s_eth, (void *)bh.payload, bh.payload_len);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "RNDIS TX failed: %s", esp_err_to_name(err));
#if EH_HOST_PKT_STATS
            pkt_stats.usb_rndis_tx_failed++;
#endif
        } else {
#if EH_HOST_PKT_STATS
            pkt_stats.usb_rndis_tx_sent++;
#endif
        }
        free_bh(&bh);
    }
}

int eh_host_usb_rndis_init(void)
{
    if (s_tx_q) {
        return 0;
    }
    s_tx_q = eh_host_port_queue_create(EH_HOST_PORT_USB_RNDIS_TX_QUEUE_SIZE,
                                       sizeof(interface_buffer_handle_t));
    s_rx_q = eh_host_port_queue_create(EH_HOST_PORT_USB_RNDIS_RX_QUEUE_SIZE,
                                       sizeof(rndis_rx_item_t));
    s_tx_wait_lock = eh_host_port_mutex_create();
    if (!s_tx_q || !s_rx_q || !s_tx_wait_lock) {
        eh_host_port_queue_destroy(s_tx_q);
        eh_host_port_queue_destroy(s_rx_q);
        eh_host_port_mutex_destroy(s_tx_wait_lock);
        s_tx_q = NULL;
        s_rx_q = NULL;
        s_tx_wait_lock = NULL;
        return -1;
    }
    s_tx_waiters = 0;
    s_running = true;
    const eh_host_port_task_create_cfg_t tx_cfg = {
        .fn = rndis_tx_task,
        .stack_bytes = TX_TASK_STACK_SIZE,
        .priority = TX_TASK_PRIO,
        .name = "eh_rndis_tx",
    };
    const eh_host_port_task_create_cfg_t rx_cfg = {
        .fn = rndis_rx_task,
        .stack_bytes = RX_TASK_STACK_SIZE,
        .priority = RX_TASK_PRIO,
        .name = "eh_rndis_rx",
    };
    if (eh_host_port_task_create(&tx_cfg, &s_tx_task) != EH_HOST_PORT_OK ||
        eh_host_port_task_create(&rx_cfg, &s_rx_task) != EH_HOST_PORT_OK) {
        eh_host_usb_rndis_stop();
        return -1;
    }
    return 0;
}

int eh_host_usb_rndis_start(void)
{
    if (s_eth) {
        return 0;
    }

    esp_err_t err;
    iot_eth_driver_t *driver = NULL;
    const iot_usbh_rndis_config_t rndis_cfg = {
        .match_id_list = s_rndis_match,
    };
    err = iot_eth_new_usb_rndis(&rndis_cfg, &driver);
    if (err == ESP_OK) {
        const iot_eth_config_t eth_cfg = {
            .driver = driver,
            .stack_input = rndis_rx,
        };
        err = iot_eth_install(&eth_cfg, &s_eth);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RNDIS setup failed: %s", esp_err_to_name(err));
        if (s_eth) {
            iot_eth_uninstall(s_eth);
            s_eth = NULL;
        }
        return -1;
    }
    ESP_LOGI(TAG, "RNDIS registered; waiting for %04x:%04x", USB_RNDIS_VID, USB_RNDIS_PID);
    return 0;
}

void eh_host_usb_rndis_stop(void)
{
    s_ap_mac_valid = false;
    s_running = false;
    if (s_tx_task || s_rx_task) {
        if (s_tx_q) {
            /* A full queue must not block: the task exits on !s_running
             * without receiving. */
            interface_buffer_handle_t wake = {0};
            (void)eh_host_port_queue_send(s_tx_q, &wake, 0);
        }
        if (s_rx_q) {
            rndis_rx_item_t wake = {0};
            (void)eh_host_port_queue_send(s_rx_q, &wake, 0);
        }
    }
    if (s_tx_task) {
        eh_host_port_task_join(s_tx_task);
        eh_host_port_task_destroy(s_tx_task);
        s_tx_task = NULL;
    }
    if (s_rx_task) {
        eh_host_port_task_join(s_rx_task);
        eh_host_port_task_destroy(s_rx_task);
        s_rx_task = NULL;
    }
    while (s_tx_wait_lock) {
        uint32_t waiters;

        eh_host_port_mutex_lock(s_tx_wait_lock);
        waiters = s_tx_waiters;
        eh_host_port_mutex_unlock(s_tx_wait_lock);
        if (waiters == 0) {
            break;
        }
        eh_host_port_task_delay_ms(EH_USB_ENQUEUE_MS);
    }
    if (s_eth) {
        iot_eth_uninstall(s_eth);
        s_eth = NULL;
    }
    if (s_tx_q) {
        interface_buffer_handle_t bh;
        while (eh_host_port_queue_receive(s_tx_q, &bh, 0) == EH_HOST_PORT_OK) {
            free_bh(&bh);
        }
        eh_host_port_queue_destroy(s_tx_q);
        s_tx_q = NULL;
    }
    if (s_rx_q) {
        rndis_rx_item_t item;
        while (eh_host_port_queue_receive(s_rx_q, &item, 0) == EH_HOST_PORT_OK) {
            free(item.data);
        }
        eh_host_port_queue_destroy(s_rx_q);
        s_rx_q = NULL;
    }
    if (s_tx_wait_lock) {
        eh_host_port_mutex_destroy(s_tx_wait_lock);
        s_tx_wait_lock = NULL;
    }
}

int eh_host_usb_rndis_tx(interface_buffer_handle_t *bh)
{
    int running;

    if (!bh || !s_tx_wait_lock) {
#if EH_HOST_PKT_STATS
        pkt_stats.usb_rndis_tx_rejected++;
#endif
        free_bh(bh);
        return -1;
    }

    eh_host_port_mutex_lock(s_tx_wait_lock);
    s_tx_waiters++;
    running = s_running && s_tx_q && s_eth;
    eh_host_port_mutex_unlock(s_tx_wait_lock);
    if (!running) {
        eh_host_port_mutex_lock(s_tx_wait_lock);
        s_tx_waiters--;
        eh_host_port_mutex_unlock(s_tx_wait_lock);
#if EH_HOST_PKT_STATS
        pkt_stats.usb_rndis_tx_rejected++;
#endif
        free_bh(bh);
        return -1;
    }

    while (s_running) {
        if (eh_host_port_queue_send(s_tx_q, bh, EH_USB_ENQUEUE_MS) == EH_HOST_PORT_OK) {
            eh_host_port_mutex_lock(s_tx_wait_lock);
            s_tx_waiters--;
            eh_host_port_mutex_unlock(s_tx_wait_lock);
#if EH_HOST_PKT_STATS
            pkt_stats.usb_rndis_tx_queued++;
#endif
            return 0;
        }
    }

    eh_host_port_mutex_lock(s_tx_wait_lock);
    s_tx_waiters--;
    eh_host_port_mutex_unlock(s_tx_wait_lock);
#if EH_HOST_PKT_STATS
    pkt_stats.usb_rndis_tx_rejected++;
#endif
    free_bh(bh);
    return -1;
}

int eh_host_usb_rndis_is_ready(void)
{
    return s_running && s_eth ? 1 : 0;
}

#endif /* ESP_PLATFORM && EH_HOST_TRANSPORT_BUS_USB && EH_HOST_USB_RNDIS */
