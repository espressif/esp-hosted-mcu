/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* SPI half-duplex bus. Part of eh_cp_master_config.h: include that header, not this one. */

#ifndef EH_CP_CFG_BUS_SPI_HD_H
#define EH_CP_CFG_BUS_SPI_HD_H

#ifndef EH_CP_MASTER_CONFIG_H
#error "include eh_cp_master_config.h, not eh_cp_cfg_bus_spi_hd.h"
#endif

/* SPI Half-Duplex. */
#if EH_CP_TRANSPORT_SPI_HD
#  define EH_CP_SPI_HD_NUM_DATA_LINES   CONFIG_EH_TRANSPORT_CP_SPI_HD_NUM_DATA_LINES
#  define EH_CP_SPI_HD_MODE             CONFIG_EH_TRANSPORT_CP_SPI_HD_MODE_VALUE
#  define EH_CP_SPI_HD_Q_SIZE           CONFIG_EH_TRANSPORT_CP_SPI_HD_Q_SIZE
#  define EH_CP_SPI_HD_PIN_CLK          CONFIG_EH_TRANSPORT_CP_SPI_HD_GPIO_CLK
#  define EH_CP_SPI_HD_PIN_CS           CONFIG_EH_TRANSPORT_CP_SPI_HD_GPIO_CS
#  define EH_CP_SPI_HD_PIN_D0           CONFIG_EH_TRANSPORT_CP_SPI_HD_GPIO_D0
#  if (EH_CP_SPI_HD_NUM_DATA_LINES >= 2)
#    define EH_CP_SPI_HD_PIN_D1         CONFIG_EH_TRANSPORT_CP_SPI_HD_GPIO_D1
#  else
#    define EH_CP_SPI_HD_PIN_D1         -1
#  endif
#  if (EH_CP_SPI_HD_NUM_DATA_LINES == 4)
#    define EH_CP_SPI_HD_PIN_D2         CONFIG_EH_TRANSPORT_CP_SPI_HD_GPIO_D2
#    define EH_CP_SPI_HD_PIN_D3         CONFIG_EH_TRANSPORT_CP_SPI_HD_GPIO_D3
#  else
#    define EH_CP_SPI_HD_PIN_D2         -1
#    define EH_CP_SPI_HD_PIN_D3         -1
#  endif
#  define EH_CP_SPI_HD_PIN_DATA_READY   CONFIG_EH_TRANSPORT_CP_SPI_HD_GPIO_DATA_READY
#  define EH_CP_SPI_HD_PIN_RESET        CONFIG_EH_TRANSPORT_CP_SPI_HD_GPIO_RESET
#endif

#endif /* EH_CP_CFG_BUS_SPI_HD_H */
