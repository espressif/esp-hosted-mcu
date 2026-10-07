/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eh_host_port_master_config.h"

#if defined(ESP_PLATFORM) && EH_HOST_TRANSPORT_BUS_USB && EH_HOST_USB_RNDIS

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

static const usb_device_match_id_t s_rndis_match[] = {
    {
        .match_flags = USB_DEVICE_ID_MATCH_VID_PID,
        .idVendor = USB_RNDIS_VID,
        .idProduct = USB_RNDIS_PID,
    },
    { 0 },
};

static eh_host_port_queue_t *s_tx_q;
static eh_host_port_task_t *s_tx_task;
static iot_eth_handle_t s_eth;
static volatile bool s_running;
static uint8_t s_ap_mac[6];
static bool s_ap_mac_valid;

static void free_bh(interface_buffer_handle_t *bh)
{
    if (bh && bh->free_buf_handle) {
        bh->free_buf_handle(bh->priv_buffer_handle);
    }
}

/*
 * iot_usbh_rndis transfers ownership of data to this callback. The common
 * dispatcher copies it before handing it to the hosted Wi-Fi channel, so
 * release the original buffer before returning on every path.
 */
static esp_err_t rndis_rx(iot_eth_handle_t handle, uint8_t *data, size_t len, void *ctx)
{
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

    interface_buffer_handle_t bh = {
        .if_type = s_ap_mac_valid &&
                   memcmp(data + USB_ETH_SRC_OFF, s_ap_mac, sizeof(s_ap_mac)) == 0
                       ? ESP_AP_IF : ESP_STA_IF,
        .payload = data,
        .payload_len = (uint16_t)len,
    };
    eh_host_mcu_transport_dispatch_frame(&bh);
    free(data);
#if EH_HOST_PKT_STATS
    pkt_stats.usb_rndis_rx_delivered++;
#endif
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
    if (!s_tx_q) {
        return -1;
    }
    s_running = true;
    const eh_host_port_task_create_cfg_t cfg = {
        .fn = rndis_tx_task,
        .stack_bytes = EH_HOST_PORT_USB_RNDIS_TX_TASK_STACK,
        .priority = EH_HOST_PORT_USB_RNDIS_TX_TASK_PRIO,
        .name = "eh_rndis_tx",
    };
    if (eh_host_port_task_create(&cfg, &s_tx_task) != EH_HOST_PORT_OK) {
        eh_host_port_queue_destroy(s_tx_q);
        s_tx_q = NULL;
        s_running = false;
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
    if (s_eth) {
        iot_eth_uninstall(s_eth);
        s_eth = NULL;
    }
    if (s_tx_task) {
        s_running = false;
        interface_buffer_handle_t wake = {0};
        (void)eh_host_port_queue_send(s_tx_q, &wake, EH_HOST_PORT_WAIT_FOREVER);
        eh_host_port_task_join(s_tx_task);
        eh_host_port_task_destroy(s_tx_task);
        s_tx_task = NULL;
    }
    if (s_tx_q) {
        interface_buffer_handle_t bh;
        while (eh_host_port_queue_receive(s_tx_q, &bh, 0) == EH_HOST_PORT_OK) {
            free_bh(&bh);
        }
        eh_host_port_queue_destroy(s_tx_q);
        s_tx_q = NULL;
    }
}

int eh_host_usb_rndis_tx(interface_buffer_handle_t *bh)
{
    if (!bh || !s_tx_q || !s_eth || !s_running) {
#if EH_HOST_PKT_STATS
        pkt_stats.usb_rndis_tx_rejected++;
#endif
        free_bh(bh);
        return -1;
    }
    if (eh_host_port_queue_send(s_tx_q, bh, 0) != EH_HOST_PORT_OK) {
        ESP_LOGW(TAG, "RNDIS TX queue full; applying lwIP backpressure");
#if EH_HOST_PKT_STATS
        pkt_stats.usb_rndis_tx_rejected++;
#endif
        free_bh(bh);
        return -1;
    }
#if EH_HOST_PKT_STATS
    pkt_stats.usb_rndis_tx_queued++;
#endif
    return 0;
}

int eh_host_usb_rndis_is_ready(void)
{
    return s_running && s_eth ? 1 : 0;
}

#endif /* ESP_PLATFORM && EH_HOST_TRANSPORT_BUS_USB && EH_HOST_USB_RNDIS */
