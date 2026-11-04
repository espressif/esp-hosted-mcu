/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* ESP-IDF USB host library: install, event task, reference count. */

#include "eh_host_port_usb.h"

#if EH_HOST_PORT_HAS_USB
#ifdef ESP_PLATFORM

#include "eh_host_port.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "usb/usb_host.h"

#define EH_USB_HOST_LIB_TASK_STACK 4096
#define EH_USB_HOST_LIB_TASK_PRIO  20

static const char *TAG = "eh_port_usb";

static eh_host_port_mutex_t *s_lock = NULL;
static int s_ref_count = 0;

static eh_host_port_err_t port_err_from_esp(esp_err_t err)
{
    if (err == ESP_OK) {
        return EH_HOST_PORT_OK;
    }
    if (err == ESP_ERR_NO_MEM) {
        return EH_HOST_PORT_ERR_NOMEM;
    }
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_STATE) {
        return EH_HOST_PORT_ERR_INVAL;
    }
    return EH_HOST_PORT_ERR;
}

static void usb_host_event_task(void *arg)
{
    (void)arg;
    bool has_clients = true;
    bool has_devices = false;

    ESP_LOGD(TAG, "USB host event task started");

    while (has_clients) {
        uint32_t event_flags = 0;
        esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "usb_host_lib_handle_events failed: %s", esp_err_to_name(err));
            break;
        }

        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGD(TAG, "USB host: all clients deregistered");
            if (usb_host_device_free_all() == ESP_OK) {
                ESP_LOGD(TAG, "All devices freed");
                has_clients = false;
            } else {
                has_devices = true;
            }
        }

        if (has_devices && (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)) {
            ESP_LOGD(TAG, "USB host: all devices freed");
            has_clients = false;
        }

#ifdef CDC_HOST_SUSPEND_RESUME_API_SUPPORTED
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_AUTO_SUSPEND) {
            usb_host_lib_root_port_suspend();
        }
#endif
    }

    ESP_LOGI(TAG, "USB host event task exiting, uninstalling host library");
    eh_host_port_task_delay_ms(10);
    esp_err_t err = usb_host_uninstall();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_uninstall failed: %s", esp_err_to_name(err));
    }
}

eh_host_port_err_t eh_host_port_usb_init(void)
{
    if (!s_lock) {
        s_lock = eh_host_port_mutex_create();
        if (!s_lock) {
            return EH_HOST_PORT_ERR_NOMEM;
        }
    }

    eh_host_port_mutex_lock(s_lock);
    if (s_ref_count++ > 0) {
        eh_host_port_mutex_unlock(s_lock);
        return EH_HOST_PORT_OK;
    }

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };

    eh_host_port_err_t rc = port_err_from_esp(usb_host_install(&host_config));
    if (rc != EH_HOST_PORT_OK) {
        ESP_LOGE(TAG, "usb_host_install failed: %d", (int)rc);
        s_ref_count = 0;
        eh_host_port_mutex_unlock(s_lock);
        return rc;
    }

    const eh_host_port_task_create_cfg_t task_cfg = {
        .fn = usb_host_event_task,
        .arg = NULL,
        .stack_bytes = EH_USB_HOST_LIB_TASK_STACK,
        .priority = EH_USB_HOST_LIB_TASK_PRIO,
        .name = "eh_usb_host_ev",
        .flags = EH_HOST_PORT_TASK_DETACHED,
    };
    if (eh_host_port_task_create(&task_cfg, NULL) != EH_HOST_PORT_OK) {
        ESP_LOGE(TAG, "Failed to create USB host event task");
        usb_host_uninstall();
        s_ref_count = 0;
        eh_host_port_mutex_unlock(s_lock);
        return EH_HOST_PORT_ERR_NOMEM;
    }

    ESP_LOGI(TAG, "USB host subsystem initialized");
    eh_host_port_mutex_unlock(s_lock);
    return EH_HOST_PORT_OK;
}

eh_host_port_err_t eh_host_port_usb_deinit(void)
{
    if (!s_lock) {
        return EH_HOST_PORT_ERR_INVAL;
    }

    eh_host_port_mutex_lock(s_lock);
    if (s_ref_count > 0) {
        s_ref_count--;
    }
    eh_host_port_mutex_unlock(s_lock);
    return EH_HOST_PORT_OK;
}

#endif /* ESP_PLATFORM */
#endif /* EH_HOST_PORT_HAS_USB */
