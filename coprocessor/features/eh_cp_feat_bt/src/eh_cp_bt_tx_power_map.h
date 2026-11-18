/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* eh_power_level_t symbol <-> this chip's esp_power_level_t, by name.
 *
 * Include after esp_bt.h and with one EH_CP_BT_PWR_FAMILY_<hdr> defined: the IDF
 * header directory that esp_bt.h comes from. Values come from that header, so a
 * renumber in IDF needs no change here; a removed name breaks the build.
 *
 * L = canonical level. A = deprecated or backward-compatibility alias: accepted
 * on set, never returned by get (its value belongs to an L row).
 * Per-chip values: docs/features/bluetooth.md, "Level values per chip"
 * (tests/unit/bt_tx_power checks it, and every list here, against the IDF headers).
 */

#ifndef EH_CP_BT_TX_POWER_MAP_H
#define EH_CP_BT_TX_POWER_MAP_H

#include <stdbool.h>
#include <stdint.h>
#include "eh_bt_tx_power.h"

#if defined(EH_CP_BT_PWR_FAMILY_ESP32)
#define EH_CP_BT_PWR_NAMES(L, A) \
    L(N, 12) L(N, 9) L(N, 6) L(N, 3) L(N, 0) L(P, 3) L(P, 6) L(P, 9) \
    A(N, 14) A(N, 11) A(N, 8) A(N, 5) A(N, 2) A(P, 1) A(P, 4) A(P, 7)
#elif defined(EH_CP_BT_PWR_FAMILY_ESP32C2) || defined(EH_CP_BT_PWR_FAMILY_ESP32C3)
#define EH_CP_BT_PWR_NAMES(L, A) \
    L(N, 24) L(N, 21) L(N, 18) L(N, 15) L(N, 12) L(N, 9) L(N, 6) L(N, 3) L(N, 0) \
    L(P, 3) L(P, 6) L(P, 9) L(P, 12) L(P, 15) L(P, 18) L(P, 20) \
    A(P, 21)
#elif defined(EH_CP_BT_PWR_FAMILY_ESP32C5)
#define EH_CP_BT_PWR_NAMES(L, A) \
    L(N, 24) L(N, 21) L(N, 18) L(N, 15) L(N, 12) L(N, 9) L(N, 6) L(N, 3) L(N, 0) \
    L(P, 3) L(P, 6) L(P, 9) L(P, 12) L(P, 15) L(P, 18) L(P, 20)
#elif defined(EH_CP_BT_PWR_FAMILY_ESP32C6)
#define EH_CP_BT_PWR_NAMES(L, A) \
    L(N, 15) L(N, 12) L(N, 9) L(N, 6) L(N, 3) L(N, 0) \
    L(P, 3) L(P, 6) L(P, 9) L(P, 12) L(P, 15) L(P, 18) L(P, 20)
#elif defined(EH_CP_BT_PWR_FAMILY_ESP32H2)
#define EH_CP_BT_PWR_NAMES(L, A) \
    L(N, 24) L(N, 21) L(N, 18) L(N, 15) L(N, 12) L(N, 9) L(N, 6) L(N, 3) L(N, 0) \
    L(P, 3) L(P, 6) L(P, 9) L(P, 12) L(P, 15) L(P, 18) L(P, 20) \
    A(P, 16) A(P, 17) A(P, 19)
#elif defined(EH_CP_BT_PWR_FAMILY_ESP32H4) || defined(EH_CP_BT_PWR_FAMILY_ESP32S31)
#define EH_CP_BT_PWR_NAMES(L, A) \
    L(N, 24) L(N, 21) L(N, 18) L(N, 15) L(N, 12) L(N, 9) L(N, 6) L(N, 3) L(N, 0) \
    L(P, 3) L(P, 6) L(P, 9) L(P, 12) L(P, 15) L(P, 16) L(P, 17) L(P, 18) L(P, 19) L(P, 20)
#else
#error "no BLE TX power level table for this target"
#endif

static inline bool eh_cp_bt_pwr_sym_to_native(uint8_t sym, esp_power_level_t *out)
{
    switch (sym) {
#define EH_CP_BT_PWR_CASE_(s, x) case EH_PWR_LVL_##s##x: *out = ESP_PWR_LVL_##s##x; return true;
    EH_CP_BT_PWR_NAMES(EH_CP_BT_PWR_CASE_, EH_CP_BT_PWR_CASE_)
#undef EH_CP_BT_PWR_CASE_
    default:
        return false;
    }
}

/* Native value -> canonical symbol. Negative, 0xFF or unknown -> INVALID. */
static inline uint8_t eh_cp_bt_pwr_native_to_sym(int native)
{
#define EH_CP_BT_PWR_IF_(s, x) if (native == (int)ESP_PWR_LVL_##s##x) return EH_PWR_LVL_##s##x;
#define EH_CP_BT_PWR_SKIP_(s, x)
    EH_CP_BT_PWR_NAMES(EH_CP_BT_PWR_IF_, EH_CP_BT_PWR_SKIP_)
#undef EH_CP_BT_PWR_IF_
#undef EH_CP_BT_PWR_SKIP_
    return EH_PWR_LVL_INVALID;
}

#endif /* EH_CP_BT_TX_POWER_MAP_H */
