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
#include <string.h>

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
static eh_host_port_mutex_t *s_tx_enqueue_lock = NULL;
static uint32_t s_tx_enqueue_waiters;
static eh_host_port_queue_t *s_tx_q[MAX_PRIORITY_QUEUES];
static eh_host_port_sem_t *s_tx_sem = NULL;
static eh_host_port_task_t *s_tx_task = NULL;
static eh_host_port_stream_buffer_t *s_rx_stream = NULL;
static eh_host_port_queue_t *s_rx_q[MAX_PRIORITY_QUEUES];
static eh_host_port_sem_t *s_rx_sem = NULL;
static eh_host_port_task_t *s_rx_task = NULL;
static eh_host_port_task_t *s_rx_proc = NULL;
static eh_host_port_sem_t *s_rx_open = NULL;
static uint32_t s_usb_rx_data_drops;

/* A full lane returns within this time after s_running is cleared, so deinit
 * can destroy the queue once every sender has left queue_send. */
#define EH_USB_ENQUEUE_MS  100

#define RX_TASK_STACK_SIZE                EH_HOST_DEFLT_TASK_STACK_BYTES
#define RX_PROC_TASK_STACK_SIZE           EH_HOST_DEFLT_TASK_STACK_BYTES
#define TX_TASK_STACK_SIZE                EH_HOST_DEFLT_TASK_STACK_BYTES

#define RX_TASK_PRIO                      EH_HOST_DEFLT_TASK_PRIORITY
#define RX_PROC_TASK_PRIO                 EH_HOST_DEFLT_TASK_PRIORITY
#define TX_TASK_PRIO                      EH_HOST_DEFLT_TASK_PRIORITY

/* Set to 1 to log high-water marks for queued TX frames, copied RX payloads,
 * and bytes resident in the RX stream. Default builds leave this at 0. */
#define DEBUG_PEAK_MEMORY_USAGE 0

static volatile uint8_t s_running = 0;
static const struct eh_usb_stack_ops *s_usb_ops = NULL;

#if DEBUG_PEAK_MEMORY_USAGE
static uint32_t s_usb_mem_tx_cur;
static uint32_t s_usb_mem_tx_peak;
static uint32_t s_usb_mem_rx_cur;
static uint32_t s_usb_mem_rx_peak;
static uint32_t s_usb_mem_stream_cur;
static uint32_t s_usb_mem_stream_peak;
static eh_host_port_mutex_t *s_usb_mem_lock;

static void usb_mem_log_values(uint32_t tx_peak, uint32_t tx_cur,
                               uint32_t rx_peak, uint32_t rx_cur,
                               uint32_t stream_peak, uint32_t stream_cur)
{
    ESP_LOGI(TAG, "USB mem peak TX %u (now %u) RX %u (now %u) stream %u (now %u)",
             (unsigned)tx_peak, (unsigned)tx_cur,
             (unsigned)rx_peak, (unsigned)rx_cur,
             (unsigned)stream_peak, (unsigned)stream_cur);
}

static void usb_mem_snapshot(uint32_t *tx_peak, uint32_t *tx_cur,
                             uint32_t *rx_peak, uint32_t *rx_cur,
                             uint32_t *stream_peak, uint32_t *stream_cur)
{
    *tx_peak = s_usb_mem_tx_peak;
    *tx_cur = s_usb_mem_tx_cur;
    *rx_peak = s_usb_mem_rx_peak;
    *rx_cur = s_usb_mem_rx_cur;
    *stream_peak = s_usb_mem_stream_peak;
    *stream_cur = s_usb_mem_stream_cur;
}

static void usb_mem_add(uint32_t *cur, uint32_t *peak, uint32_t n)
{
    int raised = 0;
    uint32_t tx_peak = 0;
    uint32_t tx_cur = 0;
    uint32_t rx_peak = 0;
    uint32_t rx_cur = 0;
    uint32_t stream_peak = 0;
    uint32_t stream_cur = 0;

    if (n == 0 || !s_usb_mem_lock) {
        return;
    }
    eh_host_port_mutex_lock(s_usb_mem_lock);
    *cur += n;
    if (*cur > *peak) {
        *peak = *cur;
        raised = 1;
        usb_mem_snapshot(&tx_peak, &tx_cur, &rx_peak, &rx_cur,
                         &stream_peak, &stream_cur);
    }
    eh_host_port_mutex_unlock(s_usb_mem_lock);
    if (raised) {
        usb_mem_log_values(tx_peak, tx_cur, rx_peak, rx_cur, stream_peak, stream_cur);
    }
}

static void usb_mem_sub(uint32_t *cur, uint32_t n)
{
    if (n == 0 || !s_usb_mem_lock) {
        return;
    }
    eh_host_port_mutex_lock(s_usb_mem_lock);
    if (*cur > n) {
        *cur -= n;
    } else {
        *cur = 0;
    }
    eh_host_port_mutex_unlock(s_usb_mem_lock);
}

static uint32_t usb_mem_tx_bytes(const interface_buffer_handle_t *bh)
{
    if (!bh || !bh->payload || bh->payload_len == 0) {
        return 0;
    }
    return (uint32_t)bh->payload_len + eh_frame_hdr_size();
}

static uint32_t usb_mem_rx_bytes(const interface_buffer_handle_t *bh)
{
    if (!bh || !bh->payload || bh->payload_len == 0) {
        return 0;
    }
    return bh->payload_len;
}

static void usb_mem_tx_add(const interface_buffer_handle_t *bh)
{
    usb_mem_add(&s_usb_mem_tx_cur, &s_usb_mem_tx_peak, usb_mem_tx_bytes(bh));
}

static void usb_mem_tx_sub(const interface_buffer_handle_t *bh)
{
    usb_mem_sub(&s_usb_mem_tx_cur, usb_mem_tx_bytes(bh));
}

static void usb_mem_rx_add(const interface_buffer_handle_t *bh)
{
    usb_mem_add(&s_usb_mem_rx_cur, &s_usb_mem_rx_peak, usb_mem_rx_bytes(bh));
}

static void usb_mem_rx_sub(const interface_buffer_handle_t *bh)
{
    usb_mem_sub(&s_usb_mem_rx_cur, usb_mem_rx_bytes(bh));
}

static void usb_mem_stream_add(size_t n)
{
    usb_mem_add(&s_usb_mem_stream_cur, &s_usb_mem_stream_peak, (uint32_t)n);
}

static void usb_mem_stream_sub(size_t n)
{
    usb_mem_sub(&s_usb_mem_stream_cur, (uint32_t)n);
}

static void usb_mem_reset(void)
{
    if (!s_usb_mem_lock) {
        s_usb_mem_lock = eh_host_port_mutex_create();
        if (!s_usb_mem_lock) {
            return;
        }
    }
    eh_host_port_mutex_lock(s_usb_mem_lock);
    s_usb_mem_tx_cur = 0;
    s_usb_mem_tx_peak = 0;
    s_usb_mem_rx_cur = 0;
    s_usb_mem_rx_peak = 0;
    s_usb_mem_stream_cur = 0;
    s_usb_mem_stream_peak = 0;
    eh_host_port_mutex_unlock(s_usb_mem_lock);
}

static void usb_mem_log_final(void)
{
    uint32_t tx_peak;
    uint32_t tx_cur;
    uint32_t rx_peak;
    uint32_t rx_cur;
    uint32_t stream_peak;
    uint32_t stream_cur;

    if (!s_usb_mem_lock) {
        return;
    }
    eh_host_port_mutex_lock(s_usb_mem_lock);
    usb_mem_snapshot(&tx_peak, &tx_cur, &rx_peak, &rx_cur, &stream_peak, &stream_cur);
    eh_host_port_mutex_unlock(s_usb_mem_lock);
    usb_mem_log_values(tx_peak, tx_cur, rx_peak, rx_cur, stream_peak, stream_cur);
}
#else
#define usb_mem_tx_add(bh)       ((void)0)
#define usb_mem_tx_sub(bh)       ((void)0)
#define usb_mem_rx_add(bh)       ((void)0)
#define usb_mem_rx_sub(bh)       ((void)0)
#define usb_mem_stream_add(n)    ((void)0)
#define usb_mem_stream_sub(n)    ((void)0)
#define usb_mem_reset()          ((void)0)
#define usb_mem_log_final()      ((void)0)
#endif

static void usb_tx_free(interface_buffer_handle_t *bh)
{
    if (bh && bh->free_buf_handle) {
        bh->free_buf_handle(bh->priv_buffer_handle);
    }
}

/* Serial, then HCI, then data. Header-only signalling is serial: the encoder
 * sends it as ESP_SERIAL_IF, and the caller's if_type is otherwise 0. */
static uint8_t usb_tx_prio(const interface_buffer_handle_t *bh)
{
    if ((bh->payload_len == 0 && bh->flags != 0) || bh->if_type == ESP_SERIAL_IF) {
        return PRIO_Q_SERIAL;
    }
    if (bh->if_type == ESP_HCI_IF) {
        return PRIO_Q_BT;
    }
    return PRIO_Q_OTHERS;
}

static int usb_pop(eh_host_port_queue_t **lane, interface_buffer_handle_t *bh)
{
    if (eh_host_port_queue_receive(lane[PRIO_Q_SERIAL], bh, 0) == EH_HOST_PORT_OK) {
        return 0;
    }
    if (eh_host_port_queue_receive(lane[PRIO_Q_BT], bh, 0) == EH_HOST_PORT_OK) {
        return 0;
    }
    if (eh_host_port_queue_receive(lane[PRIO_Q_OTHERS], bh, 0) == EH_HOST_PORT_OK) {
        return 0;
    }
    return -1;
}

static void usb_drain(eh_host_port_queue_t **lane, bool rx)
{
    interface_buffer_handle_t bh;

    for (int q = 0; q < MAX_PRIORITY_QUEUES; q++) {
        if (!lane[q]) {
            continue;
        }
        while (eh_host_port_queue_receive(lane[q], &bh, 0) == EH_HOST_PORT_OK) {
            if (rx) {
                usb_mem_rx_sub(&bh);
            } else {
                usb_mem_tx_sub(&bh);
            }
            usb_tx_free(&bh);
        }
        eh_host_port_queue_destroy(lane[q]);
        lane[q] = NULL;
    }
}

/* Encode and write one queued frame. The queue owns the payload until here. */
static void usb_tx_one(interface_buffer_handle_t *bh)
{
    bool header_only = (bh->payload_len == 0 && bh->flags != 0);

    usb_mem_tx_sub(bh);
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

    if (!header_only && bh->if_type == ESP_HCI_IF && !bh->payload_zcopy) {
        uint8_t *mframe = (uint8_t *)(uintptr_t)buf;
        interface_buffer_handle_t hb = *bh;
        hb.pkt_type = eh_host_mcu_hci_take_type(mframe + eh_frame_hdr_size(), &hb.payload_len);
        eh_frame_encode(mframe, &hb, hb.payload_len);
        len = (size_t)eh_frame_hdr_size() + hb.payload_len;
    }

    if (len > EH_USB_MAX_BUF || !s_usb_ops || !s_usb_ops->is_ready() || !s_tx_lock) {
        usb_tx_free(bh);
        return;
    }

    if (eh_host_port_mutex_lock_wait_ms(s_tx_lock, EH_USB_TX_TIMEOUT_MS) != EH_HOST_PORT_OK) {
        ESP_LOGE(TAG, "TX lock timeout");
        usb_tx_free(bh);
        return;
    }
    (void)s_usb_ops->tx(buf, len);
    eh_host_port_mutex_unlock(s_tx_lock);
    usb_tx_free(bh);
}

static void eh_usb_tx_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "USB TX task started");

    while (s_running) {
        interface_buffer_handle_t bh;

        eh_host_port_sem_wait_ms(s_tx_sem, EH_HOST_PORT_WAIT_FOREVER);
        if (!s_running) {
            break;
        }
        if (usb_pop(s_tx_q, &bh) != 0) {
            continue;
        }
        usb_tx_one(&bh);
    }

    ESP_LOGI(TAG, "USB TX task exiting");
}

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
        usb_mem_stream_sub(r);
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
        usb_mem_stream_add(n);
    }
}

/* Serial, then HCI, then data. if_type comes from the decoded header. */
static uint8_t usb_rx_prio(const interface_buffer_handle_t *bh)
{
    if (bh->if_type == ESP_SERIAL_IF) {
        return PRIO_Q_SERIAL;
    }
    if (bh->if_type == ESP_HCI_IF) {
        return PRIO_Q_BT;
    }
    return PRIO_Q_OTHERS;
}

/* Copy a payload out of the reader's scratch buffer. The queue owns it. */
static int usb_rx_enqueue(interface_buffer_handle_t *bh)
{
    uint8_t prio;

    if (bh->payload && bh->payload_len) {
        uint8_t *copy = malloc(bh->payload_len);
        if (!copy) {
            ESP_LOGE(TAG, "RX payload alloc failed len=%u", (unsigned)bh->payload_len);
            return -1;
        }
        memcpy(copy, bh->payload, bh->payload_len);
        bh->payload = copy;
        bh->priv_buffer_handle = copy;
        bh->free_buf_handle = free;
    }

    prio = usb_rx_prio(bh);
    if (!s_rx_sem || !s_rx_q[prio]) {
        usb_tx_free(bh);
        return -1;
    }

    /* A full data lane must not stall the reader, or serial and HCI stay in the stream. */
    if (prio == PRIO_Q_OTHERS) {
        if (eh_host_port_queue_send(s_rx_q[prio], bh, 0) != EH_HOST_PORT_OK) {
            usb_tx_free(bh);
            if ((s_usb_rx_data_drops++ & 0xff) == 0) {
                ESP_LOGW(TAG, "usb rx data queue full: %lu frames dropped",
                         (unsigned long)s_usb_rx_data_drops);
            }
            return -1;
        }
        usb_mem_rx_add(bh);
        eh_host_port_sem_post(s_rx_sem);
        return 0;
    }

    while (s_running) {
        if (eh_host_port_queue_send(s_rx_q[prio], bh, EH_USB_ENQUEUE_MS) == EH_HOST_PORT_OK) {
            usb_mem_rx_add(bh);
            eh_host_port_sem_post(s_rx_sem);
            return 0;
        }
    }

    usb_tx_free(bh);
    return -1;
}

static void usb_rx_dispatch(interface_buffer_handle_t *bh)
{
    if (bh->payload && bh->payload_len &&
        (bh->if_type == ESP_STA_IF || bh->if_type == ESP_AP_IF)) {
        uint8_t *copy = bh->payload;
        uint16_t len = bh->payload_len;
        uint8_t if_type = bh->if_type;
        uint8_t flags = bh->flags;

        /* Queue copy is the only one. The channel keeps it. */
        usb_mem_rx_sub(bh);
        bh->payload = NULL;
        bh->payload_len = 0;
        bh->priv_buffer_handle = NULL;
        bh->free_buf_handle = NULL;
        eh_host_mcu_transport_channel_rx(if_type, flags, copy, len);
        return;
    }

    if (bh->payload && bh->payload_len) {
        if (bh->if_type == ESP_HCI_IF) {
            eh_host_mcu_hci_rx_deliver(bh->payload, bh->payload_len);
        } else {
            eh_host_mcu_transport_dispatch_frame(bh);
        }
    } else if (bh->if_type == ESP_PRIV_IF || bh->flags != 0) {
        eh_host_mcu_transport_dispatch_frame(bh);
    }
    usb_mem_rx_sub(bh);
    usb_tx_free(bh);
}

static void eh_usb_rx_proc(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "USB RX proc started");

    while (s_running) {
        interface_buffer_handle_t bh;

        eh_host_port_sem_wait_ms(s_rx_sem, EH_HOST_PORT_WAIT_FOREVER);
        if (!s_running) {
            break;
        }
        if (usb_pop(s_rx_q, &bh) != 0) {
            continue;
        }
        usb_rx_dispatch(&bh);
    }

    ESP_LOGI(TAG, "USB RX proc exiting");
}

/* RX reader: frames from s_rx_stream are decoded and queued. eh_usb_rx_proc dispatches. */
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

        if ((buf_handle.payload && buf_handle.payload_len) ||
            buf_handle.if_type == ESP_PRIV_IF || buf_handle.flags != 0) {
            (void)usb_rx_enqueue(&buf_handle);
        }
    }

    free(scratch);
    ESP_LOGI(TAG, "USB RX task exiting");
}

static eh_host_port_task_t *eh_usb_thread_create(const char *name, int prio, size_t stack,
                                                eh_host_port_task_fn_t fn, void *arg)
{
    eh_host_port_task_create_cfg_t cfg = {
        .fn = fn, .arg = arg, .stack_bytes = stack,
        .priority = prio, .name = name,
    };
    eh_host_port_task_t *t = NULL;
    if (eh_host_port_task_create(&cfg, &t) != EH_HOST_PORT_OK) {
        return NULL;
    }
    return t;
}

int eh_host_bus_init(void)
{
    struct eh_host_usb_otg_config *live_cfg = NULL;

    usb_mem_reset();
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
    s_tx_enqueue_lock = eh_host_port_mutex_create();
    s_rx_stream = eh_host_port_stream_buffer_create(
        (size_t)EH_USB_RX_STREAM_FRAMES * EH_USB_MAX_BUF, 1);
    s_rx_open = eh_host_port_sem_create();
    {
        uint16_t tx_depth = s_cfg.tx_queue_size ? s_cfg.tx_queue_size : EH_HOST_PORT_USB_TX_QUEUE_SIZE;
        uint16_t rx_depth = s_cfg.rx_queue_size ? s_cfg.rx_queue_size : EH_HOST_PORT_USB_RX_QUEUE_SIZE;
        s_tx_sem = eh_host_port_sem_create_counting(
            (uint32_t)tx_depth * MAX_PRIORITY_QUEUES);
        s_rx_sem = eh_host_port_sem_create_counting(
            (uint32_t)rx_depth * MAX_PRIORITY_QUEUES);
        for (int q = 0; q < MAX_PRIORITY_QUEUES; q++) {
            s_tx_q[q] = eh_host_port_queue_create(tx_depth, sizeof(interface_buffer_handle_t));
            s_rx_q[q] = eh_host_port_queue_create(rx_depth, sizeof(interface_buffer_handle_t));
        }
    }
    if (!s_tx_lock || !s_tx_enqueue_lock || !s_rx_stream || !s_rx_open || !s_tx_sem || !s_rx_sem ||
        !s_tx_q[PRIO_Q_SERIAL] || !s_tx_q[PRIO_Q_BT] || !s_tx_q[PRIO_Q_OTHERS] ||
        !s_rx_q[PRIO_Q_SERIAL] || !s_rx_q[PRIO_Q_BT] || !s_rx_q[PRIO_Q_OTHERS]) {
        ESP_LOGE(TAG, "failed to create sync objects");
        eh_host_bus_deinit();
        return -1;
    }

    s_running = 1;
    s_rx_task = eh_usb_thread_create("eh_usb_rx", RX_TASK_PRIO, RX_TASK_STACK_SIZE, eh_usb_rx_task, NULL);
    if (!s_rx_task) {
        ESP_LOGE(TAG, "failed to create USB RX task");
        eh_host_bus_deinit();
        return -1;
    }
    s_rx_proc = eh_usb_thread_create("eh_usb_rx_proc", RX_PROC_TASK_PRIO, RX_PROC_TASK_STACK_SIZE, eh_usb_rx_proc, NULL);
    if (!s_rx_proc) {
        ESP_LOGE(TAG, "failed to create USB RX proc task");
        eh_host_bus_deinit();
        return -1;
    }
    s_tx_task = eh_usb_thread_create("eh_usb_tx", TX_TASK_PRIO, TX_TASK_STACK_SIZE, eh_usb_tx_task, NULL);
    if (!s_tx_task) {
        ESP_LOGE(TAG, "failed to create USB TX task");
        eh_host_bus_deinit();
        return -1;
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
    if (s_rx_sem) {
        eh_host_port_sem_post(s_rx_sem);
    }
    if (s_tx_sem) {
        eh_host_port_sem_post(s_tx_sem);
    }

    if (s_rx_task) {
        eh_host_port_task_join(s_rx_task);
        eh_host_port_task_destroy(s_rx_task);
        s_rx_task = NULL;
    }
    if (s_rx_proc) {
        eh_host_port_task_join(s_rx_proc);
        eh_host_port_task_destroy(s_rx_proc);
        s_rx_proc = NULL;
    }
    usb_drain(s_rx_q, true);
    if (s_rx_sem) {
        eh_host_port_sem_destroy(s_rx_sem);
        s_rx_sem = NULL;
    }
    if (s_tx_task) {
        eh_host_port_task_join(s_tx_task);
        eh_host_port_task_destroy(s_tx_task);
        s_tx_task = NULL;
    }
    while (s_tx_enqueue_lock) {
        uint32_t waiters;

        eh_host_port_mutex_lock(s_tx_enqueue_lock);
        waiters = s_tx_enqueue_waiters;
        eh_host_port_mutex_unlock(s_tx_enqueue_lock);
        if (waiters == 0) {
            break;
        }
        eh_host_port_task_delay_ms(EH_USB_ENQUEUE_MS);
    }
    usb_drain(s_tx_q, false);
    usb_mem_log_final();
    if (s_tx_sem) {
        eh_host_port_sem_destroy(s_tx_sem);
        s_tx_sem = NULL;
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
    if (s_tx_enqueue_lock) {
        eh_host_port_mutex_destroy(s_tx_enqueue_lock);
        s_tx_enqueue_lock = NULL;
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
        usb_tx_free(bh);
        return -1;
    }
    if (!header_only &&
        (size_t)bh->payload_len + eh_frame_hdr_size() > EH_USB_MAX_BUF) {
        usb_tx_free(bh);
        return -1;
    }
    if (bh->payload_zcopy) {
        ESP_LOGE(TAG, "zerocopy is not supported on USB CDC-ACM");
        usb_tx_free(bh);
        return -1;
    }

    if (!s_usb_ops || !s_usb_ops->is_ready() || !s_tx_sem || !s_tx_enqueue_lock) {
        usb_tx_free(bh);
        return -1;
    }

    {
        uint8_t prio = usb_tx_prio(bh);
        eh_host_port_queue_t *q;

        eh_host_port_mutex_lock(s_tx_enqueue_lock);
        s_tx_enqueue_waiters++;
        q = s_running ? s_tx_q[prio] : NULL;
        eh_host_port_mutex_unlock(s_tx_enqueue_lock);
        if (!q) {
            eh_host_port_mutex_lock(s_tx_enqueue_lock);
            s_tx_enqueue_waiters--;
            eh_host_port_mutex_unlock(s_tx_enqueue_lock);
            usb_tx_free(bh);
            return -1;
        }

        while (s_running) {
            if (eh_host_port_queue_send(q, bh, EH_USB_ENQUEUE_MS) == EH_HOST_PORT_OK) {
                usb_mem_tx_add(bh);
                eh_host_port_sem_post(s_tx_sem);
                eh_host_port_mutex_lock(s_tx_enqueue_lock);
                s_tx_enqueue_waiters--;
                eh_host_port_mutex_unlock(s_tx_enqueue_lock);
                return 0;
            }
        }

        eh_host_port_mutex_lock(s_tx_enqueue_lock);
        s_tx_enqueue_waiters--;
        eh_host_port_mutex_unlock(s_tx_enqueue_lock);
        usb_tx_free(bh);
        return -1;
    }
}

int eh_host_bus_is_tx_ready(void)
{
    return s_usb_ops && s_usb_ops->is_ready() ? 1 : 0;
}

static int eh_usb_send_ps_flag(uint8_t flag)
{
    uint8_t hdr[sizeof(eh_header_v2_t)] = {0};
    interface_buffer_handle_t hb = { .if_type = ESP_SERIAL_IF, .flags = flag };
    uint8_t len;
    int rc;

    if (!s_usb_ops || !s_usb_ops->is_ready() || !s_tx_lock) {
        return -1;
    }

    len = eh_frame_encode(hdr, &hb, 0);
    if (len == 0) {
        return -1;
    }

    if (eh_host_port_mutex_lock_wait_ms(s_tx_lock, EH_USB_TX_TIMEOUT_MS) != EH_HOST_PORT_OK) {
        ESP_LOGE(TAG, "TX lock timeout");
        return -1;
    }
    rc = s_usb_ops->tx(hdr, len);
    eh_host_port_mutex_unlock(s_tx_lock);
    return rc == 0 ? 0 : -1;
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
