/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eh_host_port_master_config.h"

#ifdef ESP_PLATFORM
#if EH_HOST_TRANSPORT_BUS_USB

#if !EH_HOST_USB_STACK_CDC_ACM
#error "eh_host_bus_usb_cdc_acm.c requires ESP_HOSTED_HOST_USB_STACK_CDC_ACM"
#endif

#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_err.h"

#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include "usb/cdc_acm_host_ops.h"
#include "eh_host_port_usb.h"
#include "eh_host_port_sync.h"
#include "eh_host_bus_usb_priv.h"

static const char *TAG = "eh_bus_usb";

static cdc_acm_dev_hdl_t s_cdc_dev = NULL;
static volatile uint8_t s_cdc_opened = 0;

/* CDC-ACM device event callback */
static void cdc_acm_dev_event_cb(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{
    (void)user_ctx;
    if (!event) return;

    switch (event->type) {
    case CDC_ACM_HOST_ERROR:
        ESP_LOGE(TAG, "CDC-ACM error event: %d", event->data.error);
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
        ESP_LOGD(TAG, "CDC-ACM serial state: 0x%04x", event->data.serial_state.val);
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        s_cdc_opened = 0;
        eh_host_usb_report_disconnect();
        break;
    default:
        break;
    }
}

/* CDC-ACM bulk IN data callback: pushes incoming bytes into the port stream buffer */
static bool cdc_acm_data_rx_cb(const uint8_t *data, size_t data_len, void *user_arg)
{
    (void)user_arg;
    eh_host_usb_push_rx(data, data_len);
    return true;
}

static int cdc_acm_stack_start(void)
{
    eh_host_port_err_t usb_rc = eh_host_port_usb_init();
    if (usb_rc != EH_HOST_PORT_OK) {
        ESP_LOGE(TAG, "USB host subsystem init failed: %d", (int)usb_rc);
        return -1;
    }

    const cdc_acm_host_driver_config_t driver_cfg = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = 20,
        .xCoreID = 0,
        .new_dev_cb = NULL,
    };
    esp_err_t err = cdc_acm_host_install(&driver_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "CDC host install failed: %s", esp_err_to_name(err));
        eh_host_port_usb_deinit();
        return -1;
    }
    return 0;
}

static int cdc_acm_stack_open(void)
{
    const struct eh_host_usb_otg_config *cfg = eh_host_usb_cfg();
    const cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = cfg->connection_timeout_ms,
        .out_buffer_size = cfg->out_buffer_size,
        .in_buffer_size = cfg->in_buffer_size,
        .event_cb = cdc_acm_dev_event_cb,
        .data_cb = cdc_acm_data_rx_cb,
        .user_arg = NULL,
    };

    esp_err_t err = cdc_acm_host_open(cfg->vid, cfg->pid, 0, &dev_config, &s_cdc_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cdc_acm_host_open failed: %s", esp_err_to_name(err));
        return -1;
    }

    s_cdc_opened = 1;

    /* Assert DTR so the coprocessor starts the data path and sends INIT */
    err = cdc_acm_host_set_control_line_state(s_cdc_dev, true, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cdc_acm_host_set_control_line_state(DTR=1) err: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Asserted DTR to coprocessor");
    }
    return 0;
}

static int cdc_acm_stack_tx(const uint8_t *buf, size_t len)
{
    esp_err_t err = cdc_acm_host_data_tx_blocking(s_cdc_dev, buf, len, EH_USB_TX_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cdc_acm_host_data_tx_blocking failed: %s", esp_err_to_name(err));
        return -1;
    }
    return 0;
}

static void cdc_acm_stack_close(void)
{
    if (s_cdc_dev) {
        cdc_acm_host_set_control_line_state(s_cdc_dev, false, false);
        cdc_acm_host_close(s_cdc_dev);
        s_cdc_dev = NULL;
    }
    s_cdc_opened = 0;
    cdc_acm_host_uninstall();
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

#endif /* EH_HOST_TRANSPORT_BUS_USB */
#endif /* ESP_PLATFORM */
