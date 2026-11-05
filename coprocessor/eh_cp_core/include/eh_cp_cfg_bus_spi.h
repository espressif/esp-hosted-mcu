/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* SPI full-duplex bus. Part of eh_cp_master_config.h: include that header, not this one. */

#ifndef EH_CP_CFG_BUS_SPI_H
#define EH_CP_CFG_BUS_SPI_H

#ifndef EH_CP_MASTER_CONFIG_H
#error "include eh_cp_master_config.h, not eh_cp_cfg_bus_spi.h"
#endif

/* SPI (full-duplex). */
#if EH_CP_TRANSPORT_SPI
#  define EH_CP_SPI_CONTROLLER          CONFIG_EH_TRANSPORT_CP_SPI_CONTROLLER_NUM
#  define EH_CP_SPI_MODE                CONFIG_EH_TRANSPORT_CP_SPI_MODE_VALUE
#  define EH_CP_SPI_PIN_MOSI            CONFIG_EH_TRANSPORT_CP_SPI_GPIO_MOSI
#  define EH_CP_SPI_PIN_MISO            CONFIG_EH_TRANSPORT_CP_SPI_GPIO_MISO
#  define EH_CP_SPI_PIN_CLK             CONFIG_EH_TRANSPORT_CP_SPI_GPIO_CLK
#  define EH_CP_SPI_PIN_CS              CONFIG_EH_TRANSPORT_CP_SPI_GPIO_CS
#  define EH_CP_SPI_PIN_HANDSHAKE       CONFIG_EH_TRANSPORT_CP_SPI_GPIO_HANDSHAKE
#  define EH_CP_SPI_PIN_DATA_READY      CONFIG_EH_TRANSPORT_CP_SPI_GPIO_DATA_READY
#  define EH_CP_SPI_PIN_RESET           CONFIG_EH_TRANSPORT_CP_SPI_GPIO_RESET
#endif

#endif /* EH_CP_CFG_BUS_SPI_H */
