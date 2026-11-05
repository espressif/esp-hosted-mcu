/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Transport selection and checksum. Part of eh_cp_master_config.h: include that header, not this one. */

#ifndef EH_CP_CFG_TRANSPORT_H
#define EH_CP_CFG_TRANSPORT_H

#ifndef EH_CP_MASTER_CONFIG_H
#error "include eh_cp_master_config.h, not eh_cp_cfg_transport.h"
#endif

/* Transport */

#ifdef CONFIG_EH_TRANSPORT_CP_SPI
#  define EH_CP_TRANSPORT_SPI           1
#else
#  define EH_CP_TRANSPORT_SPI           0
#endif

#ifdef CONFIG_EH_TRANSPORT_CP_SDIO
#  define EH_CP_TRANSPORT_SDIO          1
#else
#  define EH_CP_TRANSPORT_SDIO          0
#endif

#ifdef CONFIG_EH_TRANSPORT_CP_SPI_HD
#  define EH_CP_TRANSPORT_SPI_HD        1
#else
#  define EH_CP_TRANSPORT_SPI_HD        0
#endif

#ifdef CONFIG_EH_TRANSPORT_CP_UART
#  define EH_CP_TRANSPORT_UART          1
#else
#  define EH_CP_TRANSPORT_UART          0
#endif

#ifdef CONFIG_EH_TRANSPORT_CP_USB_OTG_DEVICE
#  define EH_CP_TRANSPORT_USB_OTG_DEVICE 1
#else
#  define EH_CP_TRANSPORT_USB_OTG_DEVICE 0
#endif

#if defined(CONFIG_EH_TRANSPORT_CP_SPI_CHECKSUM)    || \
    defined(CONFIG_EH_TRANSPORT_CP_SPI_HD_CHECKSUM) || \
    defined(CONFIG_EH_TRANSPORT_CP_UART_CHECKSUM)   || \
    defined(CONFIG_EH_TRANSPORT_CP_USB_OTG_DEVICE_CHECKSUM) || \
    defined(CONFIG_EH_TRANSPORT_CP_SDIO_CHECKSUM)
#  define EH_CP_CHECKSUM               1
#else
#  define EH_CP_CHECKSUM               0
#endif

#endif /* EH_CP_CFG_TRANSPORT_H */
