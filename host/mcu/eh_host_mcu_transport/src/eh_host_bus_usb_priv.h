/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Shared by eh_host_bus_usb.c and the one USB host stack file CMake links. */

#ifndef EH_HOST_BUS_USB_PRIV_H_
#define EH_HOST_BUS_USB_PRIV_H_

#include <stddef.h>
#include <stdint.h>

#include "eh_common_interface.h"
#include "eh_frame.h"
#include "eh_host_transport_config.h"

struct eh_usb_stack_ops {
    const char *name;
    int (*start)(void);
    int (*open)(void);
    int (*tx)(const uint8_t *buf, size_t len);
    void (*close)(void);
    int (*is_ready)(void);
};

extern const struct eh_usb_stack_ops eh_host_usb_stack_ops;

#define EH_USB_MAX_BUF        ESP_TRANSPORT_USB_MAX_BUF_SIZE
#define EH_USB_TX_TIMEOUT_MS  1000

void eh_host_usb_push_rx(const uint8_t *data, size_t data_len);
void eh_host_usb_report_disconnect(void);
const struct eh_host_usb_otg_config *eh_host_usb_cfg(void);

#if EH_HOST_USB_RNDIS
int eh_host_usb_rndis_init(void);
int eh_host_usb_rndis_start(void);
void eh_host_usb_rndis_stop(void);
int eh_host_usb_rndis_tx(interface_buffer_handle_t *bh);
int eh_host_usb_rndis_is_ready(void);
#endif

#endif /* EH_HOST_BUS_USB_PRIV_H_ */
