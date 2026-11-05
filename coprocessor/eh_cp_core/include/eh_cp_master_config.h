/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EH_CP_MASTER_CONFIG_H
#define EH_CP_MASTER_CONFIG_H

/* CP Kconfig redirector: EH_CP_XXX aliases for CONFIG_ESP_HOSTED_CP_XXX. */

#include "sdkconfig.h"

#include "eh_cp_cfg_core.h"
#include "eh_cp_cfg_rpc.h"
#include "eh_cp_cfg_transport.h"
#include "eh_cp_cfg_bus_sdio.h"
#include "eh_cp_cfg_bus_spi.h"
#include "eh_cp_cfg_bus_spi_hd.h"
#include "eh_cp_cfg_bus_uart.h"
#include "eh_cp_cfg_bt.h"

#include "eh_cp_idf_compat.h"
/* Inter-extension dep enforcement must come after EH_CP_* defs. */
#include "eh_cp_ext_dep_enforce.h"

#endif /* EH_CP_MASTER_CONFIG_H */
