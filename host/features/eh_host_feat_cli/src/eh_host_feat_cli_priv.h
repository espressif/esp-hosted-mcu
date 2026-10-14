/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Per-feature CLI sub-TU register hooks. */

#ifndef EH_HOST_FEAT_CLI_PRIV_H_
#define EH_HOST_FEAT_CLI_PRIV_H_

#include <stddef.h>

#include "eh_host_port_master_config.h"
#include "esp_console.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register / allocate through these so eh_host_feat_cli_deinit() undoes them. */
esp_err_t eh_host_feat_cli_cmd_register(const esp_console_cmd_t *cmd);
esp_err_t eh_host_feat_cli_track_args(void *table, size_t n);
#define EH_CLI_TRACK_ARGS(s) eh_host_feat_cli_track_args(&(s), sizeof(s) / sizeof(void *))

#if EH_HOST_FEAT_POWER_SAVE_READY
esp_err_t eh_host_feat_cli_host_ps_register(void);
#endif

#if EH_HOST_FEAT_RF_CERT_READY
esp_err_t eh_host_feat_cli_rf_cert_register(void);
void eh_host_feat_cli_rf_cert_overview(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_FEAT_CLI_PRIV_H_ */
