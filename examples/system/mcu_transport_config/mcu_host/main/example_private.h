/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All stages emit "EH stage=<name> ..." lines — the pytest asserts on these. */
#define EH_EX_TAG "eh_manual"

/* events.c — esp_event foundation + hosted link-state (LINK_UP) and STA (GOT_IP) bits. */
esp_err_t example_events_init(void);
esp_err_t example_wait_link_up(uint32_t timeout_ms);
esp_err_t example_wait_got_ip(uint32_t timeout_ms);
void      example_signal_got_ip(void);      /* wifi_sta.c: IP acquired */
void      example_clear_conn_bits(void);     /* teardown/recovery: drop LINK_UP + GOT_IP */
void      example_expect_cp_init(void);      /* next CP_INIT belongs to this bring-up */
void      example_on_cp_reboot(void (*cb)(void)); /* called on any other CP_INIT */

/* bus_monitor.c — CP heartbeat + recovery through the restart callback main.c passes in. */
esp_err_t example_bus_monitor_start(esp_err_t (*reinit_fn)(void));
void      example_bus_monitor_stop(void);

/* wifi_sta.c — CONFIG_EXAMPLE_MANUAL_ENABLE_WIFI_STA. */
esp_err_t example_wifi_sta_start(void);
void      example_wifi_sta_stop(void);

/* ble_nimble.c — CONFIG_EXAMPLE_MANUAL_ENABLE_BLE_ADV. */
esp_err_t example_ble_start(void);
void      example_ble_stop(void);

#ifdef __cplusplus
}
#endif
