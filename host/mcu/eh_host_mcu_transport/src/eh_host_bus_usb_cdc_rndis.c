/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* CDC-ACM control channel on a composite device that also has RNDIS.
 * Uses iot_usbh_cdc. The Ethernet data plane is eh_host_bus_usb_rndis.c.
 */

#include "eh_host_port_master_config.h"

#ifdef ESP_PLATFORM
#if EH_HOST_TRANSPORT_BUS_USB && EH_HOST_USB_RNDIS

#if !EH_HOST_USB_STACK_CDC_ACM
#error "eh_host_bus_usb_cdc_rndis.c requires ESP_HOSTED_HOST_USB_STACK_CDC_ACM"
#endif

#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_err.h"

#include "usb/usb_host.h"
#include "iot_usbh_cdc.h"
#include "iot_usbh_cdc_type.h"
#include "usb/usb_helpers.h"
#include "eh_host_port_usb.h"
#include "eh_host_port_sync.h"
#include "eh_host_bus_usb_priv.h"

static const char *TAG = "eh_bus_usb";

#define USB_CDC_ACM_SUBCLASS 0x02
#define USB_CDC_PROTOCOL_NONE 0x00

static usbh_cdc_port_handle_t s_cdc_dev = NULL;
static eh_host_port_sem_t *s_cdc_open;
static uint8_t s_cdc_itf;
static volatile uint8_t s_cdc_opened = 0;
static const usb_device_match_id_t s_cdc_match[] = {
    {
        .match_flags = USB_DEVICE_ID_MATCH_VID_PID,
        .idVendor = EH_HOST_PORT_USB_VID,
        .idProduct = EH_HOST_PORT_USB_PID,
    },
    { 0 },
};

/* Communications interface advertised by the coprocessor: CDC class, ACM subclass, protocol 0. */
static int cdc_acm_advertised_interface(const usb_config_desc_t *config)
{
    const usb_standard_desc_t *desc;
    int offset = 0;

    if (!config) {
        return -1;
    }
    desc = (const usb_standard_desc_t *)config;
    while ((desc = usb_parse_next_descriptor_of_type(desc, config->wTotalLength,
                    USB_B_DESCRIPTOR_TYPE_INTERFACE, &offset)) != NULL) {
        const usb_intf_desc_t *intf = (const usb_intf_desc_t *)desc;

        if (intf->bAlternateSetting == 0 &&
            intf->bInterfaceClass == USB_CLASS_COMM &&
            intf->bInterfaceSubClass == USB_CDC_ACM_SUBCLASS &&
            intf->bInterfaceProtocol == USB_CDC_PROTOCOL_NONE) {
            return intf->bInterfaceNumber;
        }
    }
    return -1;
}

static void cdc_iot_data_rx_cb(usbh_cdc_port_handle_t port, void *user_ctx)
{
    uint8_t buf[512];
    size_t available = 0;
    (void)user_ctx;
    while (usbh_cdc_get_rx_buffer_size(port, &available) == ESP_OK && available) {
        size_t n = available > sizeof(buf) ? sizeof(buf) : available;
        if (usbh_cdc_read_bytes(port, buf, &n, 0) != ESP_OK || n == 0) {
            break;
        }
        eh_host_usb_push_rx(buf, n);
    }
}

static void cdc_iot_closed_cb(usbh_cdc_port_handle_t port, void *user_ctx)
{
    (void)port;
    (void)user_ctx;
    s_cdc_dev = NULL;
    s_cdc_itf = 0;
    s_cdc_opened = 0;
    eh_host_usb_report_disconnect();
}

static void cdc_iot_dev_event_cb(usbh_cdc_device_event_t event,
                                 usbh_cdc_device_event_data_t *event_data,
                                 void *user_ctx)
{
    (void)user_ctx;
    if (event == CDC_HOST_DEVICE_EVENT_DISCONNECTED) {
        if (s_cdc_dev) {
            s_cdc_dev = NULL;
            s_cdc_itf = 0;
            s_cdc_opened = 0;
            eh_host_usb_report_disconnect();
        }
        return;
    }
    if (event != CDC_HOST_DEVICE_EVENT_CONNECTED || s_cdc_dev) {
        return;
    }
    const int itf = cdc_acm_advertised_interface(event_data->new_dev.active_config_desc);
    if (itf < 0) {
        ESP_LOGE(TAG, "CDC-ACM interface not advertised");
        return;
    }
    const struct eh_host_usb_otg_config *cfg = eh_host_usb_cfg();
    const usbh_cdc_port_config_t port_cfg = {
        .dev_addr = event_data->new_dev.dev_addr,
        .itf_num = (uint8_t)itf,
        .in_ringbuf_size = 4 * EH_USB_MAX_BUF,
        .in_transfer_buffer_size = cfg->in_buffer_size ? cfg->in_buffer_size : 512,
        .out_transfer_buffer_size = cfg->out_buffer_size,
        .cbs = {
            .closed = cdc_iot_closed_cb,
            .recv_data = cdc_iot_data_rx_cb,
        },
    };
    if (usbh_cdc_port_open(&port_cfg, &s_cdc_dev) != ESP_OK) {
        s_cdc_dev = NULL;
        return;
    }
    s_cdc_itf = (uint8_t)itf;
    s_cdc_opened = 1;
    ESP_LOGI(TAG, "CDC-ACM interface %u", (unsigned)s_cdc_itf);
    eh_host_port_sem_post(s_cdc_open);
}

static int cdc_acm_stack_start(void)
{
    eh_host_port_err_t usb_rc = eh_host_port_usb_init();
    if (usb_rc != EH_HOST_PORT_OK) {
        ESP_LOGE(TAG, "USB host subsystem init failed: %d", (int)usb_rc);
        return -1;
    }

    s_cdc_open = eh_host_port_sem_create();
    if (!s_cdc_open) {
        eh_host_port_usb_deinit();
        return -1;
    }
    const usbh_cdc_driver_config_t driver_cfg = {
        .task_stack_size = 4096,
        .task_priority = 20,
        .task_coreid = -1,
        .skip_init_usb_host_driver = true,
    };
    esp_err_t err = usbh_cdc_driver_install(&driver_cfg);
    if (err == ESP_OK) {
        err = usbh_cdc_register_dev_event_cb(s_cdc_match, cdc_iot_dev_event_cb, NULL);
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "CDC host install failed: %s", esp_err_to_name(err));
        eh_host_port_sem_destroy(s_cdc_open);
        s_cdc_open = NULL;
        eh_host_port_usb_deinit();
        return -1;
    }
    return 0;
}

static int cdc_acm_stack_open(void)
{
    const struct eh_host_usb_otg_config *cfg = eh_host_usb_cfg();
    if (!s_cdc_open ||
        eh_host_port_sem_wait_ms(s_cdc_open, cfg->connection_timeout_ms) != EH_HOST_PORT_OK ||
        !s_cdc_dev) {
        ESP_LOGE(TAG, "CDC-ACM composite interface did not appear");
        return -1;
    }
    esp_err_t err = usbh_cdc_send_custom_request(s_cdc_dev, 0x21, CDC_REQ_SET_CONTROL_LINE_STATE,
                                                 1, s_cdc_itf, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CDC DTR assert failed: %s", esp_err_to_name(err));
    }
    return 0;
}

static int cdc_acm_stack_tx(const uint8_t *buf, size_t len)
{
    esp_err_t err = usbh_cdc_write_bytes(s_cdc_dev, buf, len,
                                         pdMS_TO_TICKS(EH_USB_TX_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cdc_acm_host_data_tx_blocking failed: %s", esp_err_to_name(err));
        return -1;
    }
    return 0;
}

static void cdc_acm_stack_close(void)
{
    if (s_cdc_dev) {
        (void)usbh_cdc_send_custom_request(s_cdc_dev, 0x21, CDC_REQ_SET_CONTROL_LINE_STATE,
                                           0, s_cdc_itf, 0, NULL);
        usbh_cdc_port_close(s_cdc_dev);
        s_cdc_dev = NULL;
        s_cdc_itf = 0;
    }
    usbh_cdc_unregister_dev_event_cb(cdc_iot_dev_event_cb);
    usbh_cdc_driver_uninstall();
    if (s_cdc_open) {
        eh_host_port_sem_destroy(s_cdc_open);
        s_cdc_open = NULL;
    }
    eh_host_port_usb_deinit();
}

static int cdc_acm_stack_is_ready(void)
{
    return s_cdc_opened && s_cdc_dev ? 1 : 0;
}

const struct eh_usb_stack_ops eh_host_usb_stack_ops = {
    .name = "usb_host_cdc_acm",
    .start = cdc_acm_stack_start,
    .open = cdc_acm_stack_open,
    .tx = cdc_acm_stack_tx,
    .close = cdc_acm_stack_close,
    .is_ready = cdc_acm_stack_is_ready,
};

#endif /* EH_HOST_TRANSPORT_BUS_USB && EH_HOST_USB_RNDIS */
#endif /* ESP_PLATFORM */
