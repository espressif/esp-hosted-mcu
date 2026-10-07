/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * USB host controller lifecycle.
 *
 * Reference-counted. Class drivers (CDC-ACM now, others later) share one
 * installed host library and one background event task.
 */

#ifndef EH_HOST_PORT_USB_H_
#define EH_HOST_PORT_USB_H_

#include "eh_host_port_config.h"
#include "eh_host_port_err.h"
#include "endian.h"

#ifdef __cplusplus
extern "C" {
#endif

#if EH_HOST_PORT_HAS_USB

eh_host_port_err_t eh_host_port_usb_init(void);

eh_host_port_err_t eh_host_port_usb_deinit(void);

#endif /* EH_HOST_PORT_HAS_USB */

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_PORT_USB_H_ */
