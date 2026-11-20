/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* IDF esp_power_level_t of the configured CP chip (ESP_HOSTED_CP_TARGET), for
 * the compat esp_bt.h. Values are the CP chip's PA-table indexes, copied from
 * IDF components/bt/include/<dir>/include/esp_bt.h (IDF 5.5 - 6.1;
 * tests/unit/bt_tx_power checks them against IDF).
 *
 * L(sign, dBm, value): canonical name. A(sign, dBm, value): IDF alias, same
 * value as an L row.
 * Per-chip values: docs/features/bluetooth.md, "Level values per chip"
 * (tests/unit/bt_tx_power checks it, and every list here, against the IDF headers). */

#ifndef EH_COMPAT_BT_TX_POWER_TABLE_H
#define EH_COMPAT_BT_TX_POWER_TABLE_H

#include "eh_host_port_master_config.h"
#include <stdint.h>
#include "eh_bt_tx_power.h"

#if EH_HOST_CP_TARGET_ESP32
#define EH_COMPAT_BT_PWR_TABLE(L, A) \
    L(N, 12, 0) L(N, 9, 1) L(N, 6, 2) L(N, 3, 3) L(N, 0, 4) L(P, 3, 5) L(P, 6, 6) L(P, 9, 7) \
    A(N, 14, 0) A(N, 11, 1) A(N, 8, 2) A(N, 5, 3) A(N, 2, 4) A(P, 1, 5) A(P, 4, 6) A(P, 7, 7)
#define EH_COMPAT_BT_PWR_HAS_INVALID    0
#define EH_COMPAT_BT_PWR_HAS_ENHANCED   0
#elif EH_HOST_CP_TARGET_ESP32C2 || EH_HOST_CP_TARGET_ESP32C3 || EH_HOST_CP_TARGET_ESP32S3
#define EH_COMPAT_BT_PWR_TABLE(L, A) \
    L(N, 24, 0) L(N, 21, 1) L(N, 18, 2) L(N, 15, 3) L(N, 12, 4) L(N, 9, 5) L(N, 6, 6) L(N, 3, 7) \
    L(N, 0, 8) L(P, 3, 9) L(P, 6, 10) L(P, 9, 11) L(P, 12, 12) L(P, 15, 13) L(P, 18, 14) L(P, 20, 15) \
    A(P, 21, 15)
#elif EH_HOST_CP_TARGET_ESP32C5
#define EH_COMPAT_BT_PWR_TABLE(L, A) \
    L(N, 24, 0) L(N, 21, 1) L(N, 18, 2) L(N, 15, 3) L(N, 12, 4) L(N, 9, 5) L(N, 6, 6) L(N, 3, 7) \
    L(N, 0, 8) L(P, 3, 9) L(P, 6, 10) L(P, 9, 11) L(P, 12, 12) L(P, 15, 13) L(P, 18, 14) L(P, 20, 15)
#elif EH_HOST_CP_TARGET_ESP32C6 || EH_HOST_CP_TARGET_ESP32C61
#define EH_COMPAT_BT_PWR_TABLE(L, A) \
    L(N, 15, 3) L(N, 12, 4) L(N, 9, 5) L(N, 6, 6) L(N, 3, 7) \
    L(N, 0, 8) L(P, 3, 9) L(P, 6, 10) L(P, 9, 11) L(P, 12, 12) L(P, 15, 13) L(P, 18, 14) L(P, 20, 15)
#elif EH_HOST_CP_TARGET_ESP32H2 || EH_HOST_CP_TARGET_ESP32H21
#define EH_COMPAT_BT_PWR_TABLE(L, A) \
    L(N, 24, 0) L(N, 21, 1) L(N, 18, 2) L(N, 15, 3) L(N, 12, 4) L(N, 9, 5) L(N, 6, 6) L(N, 3, 7) \
    L(N, 0, 8) L(P, 3, 9) L(P, 6, 10) L(P, 9, 11) L(P, 12, 12) L(P, 15, 13) L(P, 18, 14) L(P, 20, 15) \
    A(P, 16, 14) A(P, 17, 14) A(P, 19, 15)
#elif EH_HOST_CP_TARGET_ESP32H4 || EH_HOST_CP_TARGET_ESP32S31
#define EH_COMPAT_BT_PWR_TABLE(L, A) \
    L(N, 24, 0) L(N, 21, 1) L(N, 18, 2) L(N, 15, 3) L(N, 12, 4) L(N, 9, 5) L(N, 6, 6) L(N, 3, 7) \
    L(N, 0, 8) L(P, 3, 9) L(P, 6, 10) L(P, 9, 11) L(P, 12, 12) L(P, 15, 13) \
    L(P, 16, 14) L(P, 17, 15) L(P, 18, 16) L(P, 19, 17) L(P, 20, 18)
/* BIG_CTRL / BIS / CIS: IDF >= 6.1.1 on these chips. */
#define EH_COMPAT_BT_PWR_HAS_ISO        1
#endif

#ifdef EH_COMPAT_BT_PWR_TABLE
#ifndef EH_COMPAT_BT_PWR_HAS_INVALID
#define EH_COMPAT_BT_PWR_HAS_INVALID    1
#endif
#ifndef EH_COMPAT_BT_PWR_HAS_ENHANCED
#define EH_COMPAT_BT_PWR_HAS_ENHANCED   1
#endif
#ifndef EH_COMPAT_BT_PWR_HAS_ISO
#define EH_COMPAT_BT_PWR_HAS_ISO        0
#endif

/* IDF get error value: ESP_PWR_LVL_INVALID; ESP32 has no such name and returns -1. */
#if EH_COMPAT_BT_PWR_HAS_INVALID
#define EH_COMPAT_BT_PWR_ERROR          0xFF
#else
#define EH_COMPAT_BT_PWR_ERROR          (-1)
#endif

/* Native value -> level name. Canonical rows only (aliases share their value).
 * A value the CP chip lacks -> EH_PWR_LVL_INVALID. */
static inline eh_power_level_t eh_compat_bt_pwr_to_eh(int native)
{
    switch (native) {
#define EH_COMPAT_BT_PWR_CASE_(s, x, v) case v: return EH_PWR_LVL_##s##x;
#define EH_COMPAT_BT_PWR_SKIP_(s, x, v)
    EH_COMPAT_BT_PWR_TABLE(EH_COMPAT_BT_PWR_CASE_, EH_COMPAT_BT_PWR_SKIP_)
#undef EH_COMPAT_BT_PWR_CASE_
#undef EH_COMPAT_BT_PWR_SKIP_
    default:
        return EH_PWR_LVL_INVALID;
    }
}

/* Level name -> native value; aliases too. A name the configured chip lacks
 * (a newer or different CP) -> EH_COMPAT_BT_PWR_ERROR. */
static inline int eh_compat_bt_pwr_from_eh(eh_power_level_t level)
{
    switch (level) {
#define EH_COMPAT_BT_PWR_CASE_(s, x, v) case EH_PWR_LVL_##s##x: return v;
    EH_COMPAT_BT_PWR_TABLE(EH_COMPAT_BT_PWR_CASE_, EH_COMPAT_BT_PWR_CASE_)
#undef EH_COMPAT_BT_PWR_CASE_
    default:
        return EH_COMPAT_BT_PWR_ERROR;
    }
}
#endif /* EH_COMPAT_BT_PWR_TABLE */

#endif /* EH_COMPAT_BT_TX_POWER_TABLE_H */
