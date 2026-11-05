/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* UART bus. Part of eh_cp_master_config.h: include that header, not this one. */

#ifndef EH_CP_CFG_BUS_UART_H
#define EH_CP_CFG_BUS_UART_H

#ifndef EH_CP_MASTER_CONFIG_H
#error "include eh_cp_master_config.h, not eh_cp_cfg_bus_uart.h"
#endif

/* UART. */
#if EH_CP_TRANSPORT_UART
#  define EH_CP_UART_PORT               CONFIG_EH_TRANSPORT_CP_UART_PORT
#  define EH_CP_UART_BAUDRATE           CONFIG_EH_TRANSPORT_CP_UART_BAUDRATE
#  define EH_CP_UART_NUM_DATA_BITS      CONFIG_EH_TRANSPORT_CP_UART_NUM_DATA_BITS
#  define EH_CP_UART_STOP_BITS          CONFIG_EH_TRANSPORT_CP_UART_STOP_BITS_VALUE
#  define EH_CP_UART_PARITY             CONFIG_EH_TRANSPORT_CP_UART_PARITY_VALUE
#  define EH_CP_UART_PIN_TX             CONFIG_EH_TRANSPORT_CP_UART_PIN_TX
#  define EH_CP_UART_PIN_RX             CONFIG_EH_TRANSPORT_CP_UART_PIN_RX
#  define EH_CP_UART_PIN_RESET          CONFIG_EH_TRANSPORT_CP_UART_GPIO_RESET
#  define EH_CP_UART_TX_QUEUE_SIZE      CONFIG_EH_TRANSPORT_CP_UART_TX_Q_SIZE
#  define EH_CP_UART_RX_QUEUE_SIZE      CONFIG_EH_TRANSPORT_CP_UART_RX_Q_SIZE
#endif

#endif /* EH_CP_CFG_BUS_UART_H */
