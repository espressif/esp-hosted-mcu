/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Host-side OpenThread port macros: spinel over the ESP-Hosted transport. */

#ifndef EH_HOST_PORT_OPENTHREAD_H_
#define EH_HOST_PORT_OPENTHREAD_H_

#include "sdkconfig.h"
#include "eh_host_port_master_config.h"

#if EH_HOST_FEAT_OPENTHREAD_READY
#define H_OT_HOSTED_RX_QUEUE_LEN CONFIG_ESP_HOSTED_OT_HOST_HOSTED_RX_QUEUE_LEN

/* Bus clock in Hz, for OpenThread CSL timing. Kconfig value only. */
#if H_TRANSPORT_IN_USE == H_TRANSPORT_SDIO
#define H_OT_HOSTED_BUS_SPEED_HZ (EH_HOST_PORT_SDIO_CLOCK_FREQ_KHZ * 1000)
#elif H_TRANSPORT_IN_USE == H_TRANSPORT_SPI
#define H_OT_HOSTED_BUS_SPEED_HZ (EH_HOST_PORT_SPI_FD_CLK_MHZ * 1000000)
#elif H_TRANSPORT_IN_USE == H_TRANSPORT_SPI_HD
#define H_OT_HOSTED_BUS_SPEED_HZ (EH_HOST_PORT_SPI_HD_CLK_MHZ * 1000000)
#elif H_TRANSPORT_IN_USE == H_TRANSPORT_UART
#define H_OT_HOSTED_BUS_SPEED_HZ EH_HOST_PORT_UART_BAUD_RATE
#elif H_TRANSPORT_IN_USE == H_TRANSPORT_USB
/* USB line rate in bits/s. ESP32-S31 is high-speed; S2 and S3 are full-speed. */
#if EH_HOST_CP_TARGET_ESP32S31
#define H_OT_HOSTED_BUS_SPEED_HZ 480000000
#else
#define H_OT_HOSTED_BUS_SPEED_HZ 12000000
#endif
#else
#error "OpenThread over ESP-Hosted: unknown host transport"
#endif

/* Use as ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(). */
#define EH_HOST_PORT_OPENTHREAD_RADIO_CONFIG()                              \
    {                                                                       \
        .radio_mode = RADIO_MODE_TRANSPORT,                                 \
        .radio_transport_config = {                                         \
            .transport_tx = eh_host_openthread_transport_tx,                \
            .transport_rx = eh_host_openthread_transport_rx,                \
            .bus_speed    = H_OT_HOSTED_BUS_SPEED_HZ,                       \
        },                                                                  \
    }
#endif /* EH_HOST_FEAT_OPENTHREAD_READY */

#endif
