/* SPDX-License-Identifier: Apache-2.0 */
/* Host-side CLI: umbrella registrar for per-feature console commands.
 * Call eh_host_feat_cli_register_commands() from app_main after esp_console_new_repl_*. */

#ifndef EH_HOST_CLI_H_
#define EH_HOST_CLI_H_

#include "esp_err.h"

/* Box colours, honoured only when CONFIG_LOG_COLORS is on so a console
 * configured without them stays plain. */
#include "sdkconfig.h"
#if CONFIG_LOG_COLORS
#define EH_CLI_C_BOX  "\033[36m"      /* borders       cyan      */
#define EH_CLI_C_LBL  "\033[1;36m"    /* box label     bright cyan */
#define EH_CLI_C_CMD  "\033[1;37m"    /* command name  bright white */
#define EH_CLI_C_SW   "\033[33m"      /* switches      yellow    */
#define EH_CLI_C_TXT  "\033[0m"
#define EH_CLI_C_OFF  "\033[0m"
#else
#define EH_CLI_C_BOX  ""
#define EH_CLI_C_LBL  ""
#define EH_CLI_C_CMD  ""
#define EH_CLI_C_SW   ""
#define EH_CLI_C_TXT  ""
#define EH_CLI_C_OFF  ""
#endif


#ifdef __cplusplus
extern "C" {
#endif

int eh_host_feat_cli_init(void);
int eh_host_feat_cli_deinit(void);
esp_err_t eh_host_feat_cli_register_commands(void);

/* Print the RF-cert commands grouped by radio, and the order a run goes in.
 * Also available on the console as phy_cert_commands. No-op unless the
 * RF cert feature is built in. */
void eh_host_feat_cli_rf_cert_overview(void);

/* Boxed listing, so an application can add a section of its own in the same
 * shape as the feature's. Colours follow CONFIG_LOG_COLORS.
 *   open(label) -> row(command, switches, what) ... -> text(line) ... -> close()
 */
void eh_host_cli_box_open(const char *label);
void eh_host_cli_box_row(const char *cmd, const char *sw, const char *what);
void eh_host_cli_box_text(const char *line);
void eh_host_cli_box_close(void);

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_CLI_H_ */
