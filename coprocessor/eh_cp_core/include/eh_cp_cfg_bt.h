/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Bluetooth: IDF controller selectors and UART values. Part of eh_cp_master_config.h: include that header, not this one. */

#ifndef EH_CP_CFG_BT_H
#define EH_CP_CFG_BT_H

#ifndef EH_CP_MASTER_CONFIG_H
#error "include eh_cp_master_config.h, not eh_cp_cfg_bt.h"
#endif

/* Bluetooth: IDF controller selectors and UART values → EH_CP_BT_*. */
#if defined(CONFIG_BT_ENABLED)
#  define EH_CP_BT_ENABLED                      1
#else
#  define EH_CP_BT_ENABLED                      0
#endif
#if EH_CP_BT_ENABLED && !defined(CONFIG_SOC_BT_SUPPORTED)
#  error "Bluetooth is enabled but this chip has no Bluetooth (CONFIG_SOC_BT_SUPPORTED is unset). Disable Bluetooth for this target."
#endif
#if defined(CONFIG_BT_DUAL_MODE_ARCH)
#  define EH_CP_BT_DUAL_MODE_ARCH               1
#else
#  define EH_CP_BT_DUAL_MODE_ARCH               0
#endif

/* Controller mode — ESP32 (BTDM_CONTROLLER_*) and dual-mode arch (BTDM_CTRL_*) */
#if defined(CONFIG_BTDM_CONTROLLER_MODE_BLE_ONLY) || defined(CONFIG_BTDM_CTRL_MODE_BLE_ONLY)
#  define EH_CP_BT_CTRL_MODE_BLE_ONLY           1
#else
#  define EH_CP_BT_CTRL_MODE_BLE_ONLY           0
#endif
#if defined(CONFIG_BTDM_CONTROLLER_MODE_BR_EDR_ONLY) || defined(CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY)
#  define EH_CP_BT_CTRL_MODE_BR_EDR_ONLY        1
#else
#  define EH_CP_BT_CTRL_MODE_BR_EDR_ONLY        0
#endif
#if defined(CONFIG_BTDM_CONTROLLER_MODE_BTDM) || defined(CONFIG_BTDM_CTRL_MODE_BTDM)
#  define EH_CP_BT_CTRL_MODE_BTDM               1
#else
#  define EH_CP_BT_CTRL_MODE_BTDM               0
#endif

/* HCI interface and UART flow control, per controller */
#if defined(CONFIG_BTDM_CONTROLLER_HCI_MODE_VHCI)
#  define EH_CP_BT_HCI_VHCI_ESP32               1
#else
#  define EH_CP_BT_HCI_VHCI_ESP32               0
#endif
#if defined(CONFIG_BTDM_CTRL_HCI_UART_FLOW_CTRL_EN)
#  define EH_CP_BT_UART_FLOWCTRL_ESP32          1
#else
#  define EH_CP_BT_UART_FLOWCTRL_ESP32          0
#endif
#if defined(CONFIG_BT_CTRL_HCI_MODE_VHCI)
#  define EH_CP_BT_HCI_VHCI_C3_S3               1
#else
#  define EH_CP_BT_HCI_VHCI_C3_S3               0
#endif
#if defined(CONFIG_BT_CTRL_HCI_MODE_UART_H4)
#  define EH_CP_BT_HCI_UART_C3_S3               1
#else
#  define EH_CP_BT_HCI_UART_C3_S3               0
#endif
#if defined(CONFIG_BT_CTRL_HCI_INTERFACE_USE_RAM)
#  define EH_CP_BT_HCI_VHCI_DM_ARCH             1
#else
#  define EH_CP_BT_HCI_VHCI_DM_ARCH             0
#endif
#if defined(CONFIG_BT_CTRL_HCI_INTERFACE_USE_UART)
#  define EH_CP_BT_HCI_UART_DM_ARCH             1
#else
#  define EH_CP_BT_HCI_UART_DM_ARCH             0
#endif
#if defined(CONFIG_BT_CTRL_HCI_UART_FLOWCTRL)
#  define EH_CP_BT_UART_FLOWCTRL_DM_ARCH        1
#else
#  define EH_CP_BT_UART_FLOWCTRL_DM_ARCH        0
#endif
#if defined(CONFIG_BT_LE_HCI_INTERFACE_USE_RAM)
#  define EH_CP_BT_HCI_VHCI_LE                  1
#else
#  define EH_CP_BT_HCI_VHCI_LE                  0
#endif
#if defined(CONFIG_BT_LE_HCI_INTERFACE_USE_UART)
#  define EH_CP_BT_HCI_UART_LE                  1
#else
#  define EH_CP_BT_HCI_UART_LE                  0
#endif
#if defined(CONFIG_BT_LE_HCI_UART_FLOWCTRL)
#  define EH_CP_BT_UART_FLOWCTRL_LE             1
#else
#  define EH_CP_BT_UART_FLOWCTRL_LE             0
#endif

/* UART — ESP32 */
#ifdef CONFIG_ESP_HOSTED_BT_UART_TX_PIN_ESP32
#  define EH_CP_BT_UART_TX_PIN_ESP32            CONFIG_ESP_HOSTED_BT_UART_TX_PIN_ESP32
#endif
#ifdef CONFIG_ESP_HOSTED_BT_UART_RX_PIN_ESP32
#  define EH_CP_BT_UART_RX_PIN_ESP32            CONFIG_ESP_HOSTED_BT_UART_RX_PIN_ESP32
#endif
#ifdef CONFIG_ESP_HOSTED_BT_UART_RTS_PIN_ESP32
#  define EH_CP_BT_UART_RTS_PIN_ESP32           CONFIG_ESP_HOSTED_BT_UART_RTS_PIN_ESP32
#endif
#ifdef CONFIG_ESP_HOSTED_BT_UART_CTS_PIN_ESP32
#  define EH_CP_BT_UART_CTS_PIN_ESP32           CONFIG_ESP_HOSTED_BT_UART_CTS_PIN_ESP32
#endif

/* UART — ESP32-C3 / ESP32-S3 */
#ifdef CONFIG_ESP_HOSTED_BT_UART_PORT_ESP32_C3_S3
#  define EH_CP_BT_UART_PORT_C3_S3              CONFIG_ESP_HOSTED_BT_UART_PORT_ESP32_C3_S3
#endif
#ifdef CONFIG_ESP_HOSTED_BT_UART_BAUDRATE_ESP32_C3_S3
#  define EH_CP_BT_UART_BAUDRATE_C3_S3          CONFIG_ESP_HOSTED_BT_UART_BAUDRATE_ESP32_C3_S3
#endif
#ifdef CONFIG_ESP_HOSTED_BT_UART_TX_PIN_ESP32_C3_S3
#  define EH_CP_BT_UART_TX_PIN_C3_S3            CONFIG_ESP_HOSTED_BT_UART_TX_PIN_ESP32_C3_S3
#endif
#ifdef CONFIG_ESP_HOSTED_BT_UART_RX_PIN_ESP32_C3_S3
#  define EH_CP_BT_UART_RX_PIN_C3_S3            CONFIG_ESP_HOSTED_BT_UART_RX_PIN_ESP32_C3_S3
#endif
#ifdef CONFIG_ESP_HOSTED_BT_UART_FLOWCONTROL_ESP32_C3_S3
#  define EH_CP_BT_UART_FLOWCTRL_C3_S3          CONFIG_ESP_HOSTED_BT_UART_FLOWCONTROL_ESP32_C3_S3
#endif
#ifdef CONFIG_ESP_HOSTED_BT_UART_RTS_PIN_ESP32_C3_S3
#  define EH_CP_BT_UART_RTS_PIN_C3_S3           CONFIG_ESP_HOSTED_BT_UART_RTS_PIN_ESP32_C3_S3
#endif
#ifdef CONFIG_ESP_HOSTED_BT_UART_CTS_PIN_ESP32_C3_S3
#  define EH_CP_BT_UART_CTS_PIN_C3_S3           CONFIG_ESP_HOSTED_BT_UART_CTS_PIN_ESP32_C3_S3
#endif

/* UART — IDF controller, ESP32 */
#if defined(CONFIG_BT_HCI_UART_NO)
#  define EH_CP_BT_UART_PORT_ESP32              CONFIG_BT_HCI_UART_NO
#elif defined(CONFIG_BTDM_CTRL_HCI_UART_NO)
#  define EH_CP_BT_UART_PORT_ESP32              CONFIG_BTDM_CTRL_HCI_UART_NO
#endif
#ifdef CONFIG_BTDM_CTRL_HCI_UART_BAUDRATE
#  define EH_CP_BT_UART_BAUDRATE_ESP32          CONFIG_BTDM_CTRL_HCI_UART_BAUDRATE
#endif

/* UART — IDF controller, dual-mode arch (ESP32-S31) */
#ifdef CONFIG_BT_CTRL_HCI_UART_PORT
#  define EH_CP_BT_UART_PORT_DM_ARCH            CONFIG_BT_CTRL_HCI_UART_PORT
#endif
#ifdef CONFIG_BT_CTRL_HCI_UART_TX_PIN
#  define EH_CP_BT_UART_TX_PIN_DM_ARCH          CONFIG_BT_CTRL_HCI_UART_TX_PIN
#endif
#ifdef CONFIG_BT_CTRL_HCI_UART_RX_PIN
#  define EH_CP_BT_UART_RX_PIN_DM_ARCH          CONFIG_BT_CTRL_HCI_UART_RX_PIN
#endif
#ifdef CONFIG_BT_CTRL_HCI_UART_RTS_PIN
#  define EH_CP_BT_UART_RTS_PIN_DM_ARCH         CONFIG_BT_CTRL_HCI_UART_RTS_PIN
#endif
#ifdef CONFIG_BT_CTRL_HCI_UART_CTS_PIN
#  define EH_CP_BT_UART_CTS_PIN_DM_ARCH         CONFIG_BT_CTRL_HCI_UART_CTS_PIN
#endif

/* UART — IDF controller, BLE-only chips */
#ifdef CONFIG_BT_LE_HCI_UART_PORT
#  define EH_CP_BT_UART_PORT_LE                 CONFIG_BT_LE_HCI_UART_PORT
#endif
#ifdef CONFIG_BT_LE_HCI_UART_TX_PIN
#  define EH_CP_BT_UART_TX_PIN_LE               CONFIG_BT_LE_HCI_UART_TX_PIN
#endif
#ifdef CONFIG_BT_LE_HCI_UART_RX_PIN
#  define EH_CP_BT_UART_RX_PIN_LE               CONFIG_BT_LE_HCI_UART_RX_PIN
#endif
#ifdef CONFIG_BT_LE_HCI_UART_RTS_PIN
#  define EH_CP_BT_UART_RTS_PIN_LE              CONFIG_BT_LE_HCI_UART_RTS_PIN
#endif
#ifdef CONFIG_BT_LE_HCI_UART_CTS_PIN
#  define EH_CP_BT_UART_CTS_PIN_LE              CONFIG_BT_LE_HCI_UART_CTS_PIN
#endif

/* UART flow control choice */
#ifdef CONFIG_ESP_HOSTED_BT_UART_FLOWCONTROL_ENABLED
#  define EH_CP_BT_UART_FLOWCTRL_ENABLED        1
#else
#  define EH_CP_BT_UART_FLOWCTRL_ENABLED        0
#endif

#endif /* EH_CP_CFG_BT_H */
