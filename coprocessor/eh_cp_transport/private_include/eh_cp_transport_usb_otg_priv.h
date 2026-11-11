/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Shared by the USB OTG device transport and its RNDIS function. */

#pragma once

#include <stdint.h>

#include "eh_cp_master_config.h"
#include "eh_transport_cp.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define BUFFER_SIZE                MAX_TRANSPORT_BUF_SIZE
#define USB_TX_FLUSH_MS            1000

extern SemaphoreHandle_t usb_rx_sem;
extern SemaphoreHandle_t usb_rx_credits;
extern QueueHandle_t usb_rx_queue[MAX_PRIORITY_QUEUES];

void *h_usb_buffer_rx_alloc(uint8_t need_memset);
void usb_rx_credit_release(void);
void usb_rx_read_done(void *handle);
void start_rx_data_throttling_if_needed(void);

#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
extern volatile bool usb_exit_requested;
#endif

#if CONFIG_EH_TRANSPORT_CP_USB_OTG_DEVICE_RNDIS
#include "tinyusb.h"

void usb_rndis_prepare(void);
const tusb_desc_device_t *usb_rndis_device_descriptor(void);
const uint8_t *usb_rndis_fs_config(void);
#if TUD_OPT_HIGH_SPEED
const uint8_t *usb_rndis_hs_config(void);
#endif
esp_err_t usb_rndis_send(const uint8_t *frame, uint16_t len);
#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
void usb_rndis_rx_stop_task(void);
#endif
void usb_rndis_delete_tx_semaphores(void);
#endif
