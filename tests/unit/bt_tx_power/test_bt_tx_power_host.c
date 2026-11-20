/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Dumps the host compat table for one CP target; test_bt_tx_power.py checks it.
 * Built with a stub eh_host_port_master_config.h that defines EH_HOST_CP_TARGET_<chip>. */

#include <stdio.h>
#include "eh_compat_bt_tx_power_table.h"

int main(void)
{
    printf("F %d %d %d\n", EH_COMPAT_BT_PWR_HAS_INVALID, EH_COMPAT_BT_PWR_HAS_ENHANCED,
           EH_COMPAT_BT_PWR_HAS_ISO);

#define DUMP_L_(s, x, v) printf("L %s%d %d\n", #s, x, v);
#define DUMP_A_(s, x, v) printf("A %s%d %d\n", #s, x, v);
    EH_COMPAT_BT_PWR_TABLE(DUMP_L_, DUMP_A_)
#undef DUMP_L_
#undef DUMP_A_

#define DUMP_SYM_(s, x) printf("S %s%d %d\n", #s, x, eh_compat_bt_pwr_from_eh(EH_PWR_LVL_##s##x));
    EH_PWR_LVL_LIST(DUMP_SYM_)
#undef DUMP_SYM_
    printf("S INVALID %d\n", eh_compat_bt_pwr_from_eh(EH_PWR_LVL_INVALID));

    for (int n = -1; n < 32; n++) {
        printf("R %d %d\n", n, (int)eh_compat_bt_pwr_to_eh(n));
    }
    printf("R 255 %d\n", (int)eh_compat_bt_pwr_to_eh(255));
    return 0;
}
