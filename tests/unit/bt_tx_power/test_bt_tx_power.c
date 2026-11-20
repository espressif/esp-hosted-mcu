/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Dumps the CP symbol <-> native map for one IDF header family; the .py checks it.
 * Built with -DEH_CP_BT_PWR_FAMILY_<dir> and idf_pwr.h = that header's esp_power_level_t. */

#include <stdio.h>
#include "idf_pwr.h"
#include "eh_cp_bt_tx_power_map.h"

int main(void)
{
    esp_power_level_t native = (esp_power_level_t)0;

/* Call before printf: argument evaluation order is unspecified (GCC reads
 * `native` first, clang last). */
#define DUMP_SYM_(s, x)                                                         \
    do {                                                                        \
        native = (esp_power_level_t)-1;                                         \
        int ok_ = eh_cp_bt_pwr_sym_to_native(EH_PWR_LVL_##s##x, &native);       \
        printf("S %s%d %d %d %d\n", #s, x, EH_PWR_LVL_##s##x, ok_, (int)native); \
    } while (0);
    EH_PWR_LVL_LIST(DUMP_SYM_)
#undef DUMP_SYM_

    printf("U %d\n", eh_cp_bt_pwr_sym_to_native(EH_PWR_LVL_INVALID, &native));
    printf("U %d\n", eh_cp_bt_pwr_sym_to_native(0, &native));

    for (int n = -1; n < 32; n++) {
        printf("R %d %d\n", n, eh_cp_bt_pwr_native_to_sym(n));
    }
    printf("R 255 %d\n", eh_cp_bt_pwr_native_to_sym(255));
    return 0;
}
