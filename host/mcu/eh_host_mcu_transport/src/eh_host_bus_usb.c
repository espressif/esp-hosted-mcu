/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eh_host_port_master_config.h"

#ifdef ESP_PLATFORM
#if EH_HOST_TRANSPORT_BUS_USB

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"

#include "eh_host_port.h"
#include "eh_host_port_power.h"
#include "eh_host_event.h"
#include "eh_host_mcu_transport_priv.h"
#include "eh_host_mcu_transport_state.h"
#include "eh_host_transport_config.h"
#include "eh_frame.h"
#include "eh_common_header.h"
#include "eh_common_header_v2.h"
#include "eh_host_mcu_hci_internal.h"
#include "eh_host_bus_usb_priv.h"

static const char *TAG = "eh_bus_usb";

static struct eh_host_usb_otg_config s_cfg;

static eh_host_port_mutex_t *s_tx_lock = NULL;
static eh_host_port_stream_buffer_t *s_rx_stream = NULL;
static eh_host_port_task_t *s_rx_task = NULL;
static eh_host_port_sem_t *s_rx_open = NULL;
static volatile uint8_t s_running = 0;
static const struct eh_usb_stack_ops *s_usb_ops = NULL;

static void show_config(void)
{
    ESP_LOGI(TAG, "transport[host]: USB OTG %s timeout=%u ms tx_q=%u rx_q=%u reset_pin=%d",
             eh_host_usb_stack_ops.name,
             (unsigned int)s_cfg.connection_timeout_ms,
             (unsigned int)s_cfg.tx_queue_size,
             (unsigned int)s_cfg.rx_queue_size,
             s_cfg.pin_reset.pin);
}

void eh_host_usb_report_disconnect(void)
{
    ESP_LOGW(TAG, "CDC-ACM device disconnected");
    esp_event_post(EH_HOST_EVENT, (int32_t)EH_HOST_EVENT_TRANSPORT_FAILURE, NULL, 0, 0);
#if EH_HOST_TRANSPORT_RESTART_ON_FAILURE
    eh_host_port_restart_host();
#endif
}

const struct eh_host_usb_otg_config *eh_host_usb_cfg(void)
{
    return &s_cfg;
}

static int stream_read_exact(uint8_t *dst, size_t n)
{
    size_t got = 0;
    while (got < n) {
        if (!s_running) {
            return -1;
        }
        size_t r = eh_host_port_stream_buffer_receive(
            s_rx_stream, dst + got, n - got, 100);
        if (r == 0) {
            continue;
        }
        got += r;
    }
    return (int)got;
}

static void usb_hdr_len_flags(const uint8_t *buf, uint16_t *payload_len, uint8_t *flags)
{
    if (eh_frame_hdr_size() == EH_ESP_PAYLOAD_HEADER_V2_OFFSET) {
        const eh_header_v2_t *hdr = (const eh_header_v2_t *)buf;
        *payload_len = le16toh(hdr->len);
        *flags = hdr->flags;
    } else {
        const struct esp_payload_header *hdr = (const struct esp_payload_header *)buf;
        *payload_len = le16toh(hdr->len);
        *flags = hdr->flags;
    }
}

void eh_host_usb_push_rx(const uint8_t *data, size_t data_len)
{
    if (!s_rx_stream || !data || data_len == 0) {
        return;
    }

    size_t sent = 0;
    while (sent < data_len) {
        size_t n = eh_host_port_stream_buffer_send(
            s_rx_stream, data + sent, data_len - sent, 0);
        if (n == 0) {
            ESP_LOGW(TAG, "RX stream full, dropped %u bytes", (unsigned int)(data_len - sent));
            break;
        }
        sent += n;
    }
}

/* RX worker task: reads full frames from s_rx_stream, decodes, and dispatches */
static void eh_usb_rx_task(void *arg)
{
    (void)arg;
    uint8_t *scratch = (uint8_t *)malloc(EH_USB_MAX_BUF);
    if (!scratch) {
        ESP_LOGE(TAG, "failed to allocate scratch buffer");
        esp_event_post(EH_HOST_EVENT, (int32_t)EH_HOST_EVENT_TRANSPORT_FAILURE, NULL, 0, 0);
#if EH_HOST_TRANSPORT_RESTART_ON_FAILURE
        eh_host_port_restart_host();
#endif
        return;
    }

    ESP_LOGI(TAG, "USB RX task started");

    /* Block until connect_to_slave has opened the device and asserted DTR */
    eh_host_port_sem_wait_ms(s_rx_open, EH_HOST_PORT_WAIT_FOREVER);

    while (s_running) {
        uint8_t hdr_sz = eh_frame_hdr_size();
        if (hdr_sz == 0) {
            eh_host_port_task_delay_ms(10);
            continue;
        }

        if (stream_read_exact(scratch, hdr_sz) < 0) {
            break;
        }

        uint16_t payload_len = 0;
        uint8_t flags = 0;
        usb_hdr_len_flags(scratch, &payload_len, &flags);

        int total_len = (int)payload_len + (int)hdr_sz;
        if (total_len > EH_USB_MAX_BUF || total_len < (int)hdr_sz) {
            ESP_LOGE(TAG, "incoming data too big: %d, dropping payload", total_len);
            while (payload_len > 0) {
                size_t chunk = payload_len > EH_USB_MAX_BUF ? EH_USB_MAX_BUF : payload_len;
                if (stream_read_exact(scratch, chunk) < 0) {
                    break;
                }
                payload_len = (uint16_t)(payload_len - chunk);
            }
            continue;
        }

        if (payload_len > 0) {
            if (stream_read_exact(scratch + hdr_sz, payload_len) < 0) {
                break;
            }
        }

        interface_buffer_handle_t buf_handle = {0};
        eh_frame_result_t fres = eh_frame_decode(scratch, (uint16_t)total_len, &buf_handle);
        if (fres != EH_FRAME_OK && fres != EH_FRAME_DUMMY) {
            ESP_LOGE(TAG, "frame_decode err %d, drop", fres);
            continue;
        }
        if (buf_handle.throttle_cmd == EH_FRAME_FLOW_CTRL_ON) {
            eh_host_wifi_tx_set_throttle(true);
        } else if (buf_handle.throttle_cmd == EH_FRAME_FLOW_CTRL_OFF) {
            eh_host_wifi_tx_set_throttle(false);
        }
        if (fres != EH_FRAME_OK) {
            continue;
        }

        if (buf_handle.payload && buf_handle.payload_len) {
            if (buf_handle.if_type == ESP_HCI_IF) {
                eh_host_mcu_hci_rx_deliver(buf_handle.payload, buf_handle.payload_len);
            } else {
                eh_host_mcu_transport_dispatch_frame(&buf_handle);
            }
        } else if (buf_handle.if_type == ESP_PRIV_IF || buf_handle.flags != 0) {
            eh_host_mcu_transport_dispatch_frame(&buf_handle);
        }
    }

    free(scratch);
    ESP_LOGI(TAG, "USB RX task exiting");
}

int eh_host_bus_init(void)
{
    struct eh_host_usb_otg_config *live_cfg = NULL;
    if (eh_host_usb_otg_get_config(&live_cfg) == EH_HOST_TRANSPORT_RC_OK && live_cfg) {
        s_cfg = *live_cfg;
    } else {
        s_cfg = eh_host_get_default_usb_otg_config();
    }

    show_config();

    eh_frame_cfg_t frame_cfg = EH_FRAME_CFG_HOST_MCU_USB_DEFAULT;
    frame_cfg.checksum_enabled = 1;
    if (eh_frame_init(&frame_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "eh_frame_init failed");
        return -1;
    }

    s_usb_ops = &eh_host_usb_stack_ops;

    if (s_usb_ops->start() != 0) {
        s_usb_ops = NULL;
        eh_frame_deinit();
        return -1;
    }

#if EH_HOST_USB_RNDIS
    if (eh_host_usb_rndis_init() != 0) {
        s_usb_ops->close();
        s_usb_ops = NULL;
        eh_frame_deinit();
        return -1;
    }
    /*
     * Register RNDIS before the CP is reset/reattached in connect_to_slave().
     * iot_usbh_cdc reports only attach events, so registering after CDC open
     * can miss the already-enumerated composite data interface.
     */
    if (eh_host_usb_rndis_start() != 0) {
        eh_host_usb_rndis_stop();
        s_usb_ops->close();
        s_usb_ops = NULL;
        eh_frame_deinit();
        return -1;
    }
#endif

    s_tx_lock = eh_host_port_mutex_create();
    s_rx_stream = eh_host_port_stream_buffer_create(
        (size_t)s_cfg.rx_queue_size * EH_USB_MAX_BUF, 1);
    s_rx_open = eh_host_port_sem_create();
    if (!s_tx_lock || !s_rx_stream || !s_rx_open) {
        ESP_LOGE(TAG, "failed to create sync objects");
        eh_host_bus_deinit();
        return -1;
    }

    s_running = 1;
    {
        eh_host_port_task_create_cfg_t task_cfg = {
            .fn = eh_usb_rx_task,
            .arg = NULL,
            .stack_bytes = EH_HOST_DEFLT_TASK_STACK_BYTES,
            .priority = CONFIG_ESP_HOSTED_HOST_USB_RX_TASK_PRIO,
            .name = "eh_usb_rx",
        };
        if (eh_host_port_task_create(&task_cfg, &s_rx_task) != EH_HOST_PORT_OK) {
            ESP_LOGE(TAG, "failed to create USB RX task");
            eh_host_bus_deinit();
            return -1;
        }
    }

    return 0;
}

int eh_host_bus_connect_to_slave(void)
{
    /* Reset slave if reset GPIO is configured */
    eh_gpio_pin_t rst_pin = { .port = NULL, .pin = -1 };
    if (eh_host_transport_get_reset_config(&rst_pin) == EH_HOST_TRANSPORT_RC_OK &&
        rst_pin.pin >= 0) {
        eh_host_port_err_t rst = eh_host_port_reset_slave();
        if (rst == EH_HOST_PORT_OK) {
            ESP_LOGI(TAG, "Slave reset issued; connecting via USB");
        } else {
            ESP_LOGW(TAG, "Slave reset failed (rc=%d); continuing", (int)rst);
        }
    } else {
        ESP_LOGI(TAG, "RST pin not configured; connecting via USB directly");
    }

    ESP_LOGI(TAG, "Opening CDC-ACM device (timeout=%u ms)...", (unsigned int)s_cfg.connection_timeout_ms);
    if (!s_usb_ops || s_usb_ops->open() != 0) {
        return -1;
    }

    ESP_LOGI(TAG, "CDC-ACM device opened successfully");
    eh_host_mcu_transport_state_set(EH_HOST_MCU_TRANSPORT_RX_ACTIVE);
    eh_host_port_sem_post(s_rx_open);
    return 0;
}

int eh_host_bus_deinit(void)
{
    s_running = 0;

    if (s_rx_open) {
        eh_host_port_sem_post(s_rx_open);
    }

    if (s_rx_task) {
        eh_host_port_task_join(s_rx_task);
        eh_host_port_task_destroy(s_rx_task);
        s_rx_task = NULL;
    }

 #if EH_HOST_USB_RNDIS
    eh_host_usb_rndis_stop();
 #endif

    if (s_usb_ops) {
        s_usb_ops->close();
        s_usb_ops = NULL;
    }

    if (s_rx_stream) {
        eh_host_port_stream_buffer_destroy(s_rx_stream);
        s_rx_stream = NULL;
    }

    if (s_tx_lock) {
        eh_host_port_mutex_destroy(s_tx_lock);
        s_tx_lock = NULL;
    }

    if (s_rx_open) {
        eh_host_port_sem_destroy(s_rx_open);
        s_rx_open = NULL;
    }

    eh_frame_deinit();
    return 0;
}

int eh_host_bus_tx(interface_buffer_handle_t *bh)
{
    bool header_only = false;
    if (!bh) {
        return -1;
    }

#if EH_HOST_USB_RNDIS
    if ((bh->if_type == ESP_STA_IF || bh->if_type == ESP_AP_IF) &&
        bh->payload && bh->payload_len) {
        /*
         * Payload is an Ethernet frame. Do not put an ESP-Hosted header on
         * the RNDIS bulk pipe; the RNDIS class adds its own encapsulation.
         */
        return eh_host_usb_rndis_tx(bh);
    }
#endif

    header_only = (bh->payload_len == 0 && bh->flags != 0);
    if (!header_only && (!bh->payload || bh->payload_len == 0)) {
        if (bh->free_buf_handle) {
            bh->free_buf_handle(bh->priv_buffer_handle);
        }
        return -1;
    }

    const uint8_t *buf = header_only ? NULL : (bh->payload - eh_frame_hdr_size());
    uint8_t hdr[sizeof(eh_header_v2_t)] = {0};
    size_t len = 0;

    if (header_only) {
        interface_buffer_handle_t hb = { .if_type = ESP_SERIAL_IF, .flags = bh->flags };
        eh_frame_encode(hdr, &hb, 0);
        buf = hdr;
        len = eh_frame_hdr_size();
    } else {
        len = (size_t)bh->payload_len + eh_frame_hdr_size();
    }

    if (!header_only && bh->if_type == ESP_HCI_IF) {
        uint8_t *mframe = (uint8_t *)(uintptr_t)buf;
        interface_buffer_handle_t hb = *bh;
        hb.pkt_type = eh_host_mcu_hci_take_type(mframe + eh_frame_hdr_size(), &hb.payload_len);
        eh_frame_encode(mframe, &hb, hb.payload_len);
        len = (size_t)eh_frame_hdr_size() + hb.payload_len;
    }

    int rc = -1;
    if (len > EH_USB_MAX_BUF || !s_usb_ops || !s_usb_ops->is_ready() || !s_tx_lock) {
        goto out;
    }

    if (eh_host_port_mutex_lock_wait_ms(s_tx_lock, EH_USB_TX_TIMEOUT_MS) != EH_HOST_PORT_OK) {
        ESP_LOGE(TAG, "TX lock timeout");
        goto out;
    }

    rc = s_usb_ops->tx(buf, len);
    eh_host_port_mutex_unlock(s_tx_lock);

out:
    if (bh->free_buf_handle) {
        bh->free_buf_handle(bh->priv_buffer_handle);
    }
    return rc;
}

int eh_host_bus_is_tx_ready(void)
{
    return s_usb_ops && s_usb_ops->is_ready() ? 1 : 0;
}

static int eh_usb_send_ps_flag(uint8_t flag)
{
    interface_buffer_handle_t bh = {0};
    bh.payload_len = 0;
    bh.flags = flag;
    return eh_host_bus_tx(&bh) < 0 ? -1 : 0;
}

int eh_host_bus_inform_slave_ps_enter(void)
{
    return eh_usb_send_ps_flag(FLAG_POWER_SAVE_STARTED);
}

int eh_host_bus_inform_slave_ps_exit(void)
{
    return eh_usb_send_ps_flag(FLAG_POWER_SAVE_STOPPED);
}

#endif /* EH_HOST_TRANSPORT_BUS_USB */
#endif /* ESP_PLATFORM */
