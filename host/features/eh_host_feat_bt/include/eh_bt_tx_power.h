/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* BLE TX power level symbols, shared by host and CP.
 *
 * IDF's esp_power_level_t value is a per-chip PA-table index (ESP_PWR_LVL_P9 is 7 on
 * ESP32, 11 on C6). The wire carries the level name instead; the CP maps it to its
 * own ESP_PWR_LVL_<name> by name.
 *
 *   0          unused
 *   1..99      reserved
 *   100 + x    N<x>  (-x dBm), x = 0..99
 *   200 + x    P<x>  (+x dBm), x = 1..54
 *   255        INVALID (same value as IDF)
 *
 * A value never changes. A new IDF name takes its own slot.
 *
 * The CP and host BT features carry byte-identical copies of this file
 * (tools/mirrored_files.txt, checked by tools/check_mirrored_files.py).
 */

#ifndef EH_BT_TX_POWER_H
#define EH_BT_TX_POWER_H

#define EH_PWR_LVL_BASE_N   100
#define EH_PWR_LVL_BASE_P   200

/* Every ESP_PWR_LVL_* name in IDF >= 5.5, any chip. X(sign, dBm).
 * Per-chip values: docs/features/bluetooth.md, "Level values per chip"
 * (tests/unit/bt_tx_power checks it, and every list here, against the IDF headers). */
#define EH_PWR_LVL_LIST(X) \
    X(N, 24) X(N, 21) X(N, 18) X(N, 15) X(N, 14) X(N, 12) X(N, 11) \
    X(N, 9)  X(N, 8)  X(N, 6)  X(N, 5)  X(N, 3)  X(N, 2)  X(N, 0)  \
    X(P, 1)  X(P, 3)  X(P, 4)  X(P, 6)  X(P, 7)  X(P, 9)  X(P, 12) \
    X(P, 15) X(P, 16) X(P, 17) X(P, 18) X(P, 19) X(P, 20) X(P, 21)

typedef enum {
#define EH_PWR_LVL_ENUM_(s, x) EH_PWR_LVL_##s##x = EH_PWR_LVL_BASE_##s + (x),
    EH_PWR_LVL_LIST(EH_PWR_LVL_ENUM_)
#undef EH_PWR_LVL_ENUM_
    EH_PWR_LVL_INVALID = 0xFF,
} eh_power_level_t;

#endif /* EH_BT_TX_POWER_H */
