/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Umbrella host-CLI registration; calls each per-feature register hook. */

#include "eh_host_port_master_config.h"

#include "argtable3/argtable3.h"
#include "esp_err.h"
#include "esp_log.h"

#if EH_HOST_AUTO_FEAT_INIT
#include "eh_host_auto_init.h"
#endif
#include "eh_host_cli.h"
#include "eh_host_feat_cli_priv.h"

#define CLI_TAG "eh_cli"

/* What register_commands() added, so deinit can remove exactly that. */
#define CLI_MAX_CMDS    64
#define CLI_MAX_TABLES  16
static const char *s_cmds[CLI_MAX_CMDS];
static size_t s_n_cmds;
static struct { void **table; size_t n; } s_tables[CLI_MAX_TABLES];
static size_t s_n_tables;

esp_err_t eh_host_feat_cli_cmd_register(const esp_console_cmd_t *cmd)
{
    if (s_n_cmds == CLI_MAX_CMDS) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t rc = esp_console_cmd_register(cmd);
    if (rc == ESP_OK) {
        s_cmds[s_n_cmds++] = cmd->command;
    }
    return rc;
}

esp_err_t eh_host_feat_cli_track_args(void *table, size_t n)
{
    if (s_n_tables == CLI_MAX_TABLES) {
        arg_freetable(table, n);
        return ESP_ERR_NO_MEM;
    }
    s_tables[s_n_tables].table = table;
    s_tables[s_n_tables++].n = n;
    return ESP_OK;
}

static void cli_unregister_all(void)
{
    while (s_n_cmds) {
        esp_console_cmd_deregister(s_cmds[--s_n_cmds]);
    }
    while (s_n_tables) {
        s_n_tables--;
        arg_freetable(s_tables[s_n_tables].table, s_tables[s_n_tables].n);
    }
}

esp_err_t eh_host_feat_cli_register_commands(void)
{
    esp_err_t rc = ESP_OK;

    if (s_n_cmds) {
        return ESP_OK;
    }

#if EH_HOST_FEAT_POWER_SAVE_READY
    rc = eh_host_feat_cli_host_ps_register();
    if (rc != ESP_OK) {
        ESP_LOGE(CLI_TAG, "host_ps CLI register failed: 0x%x", rc);
        cli_unregister_all();
        return rc;
    }
#endif

#if EH_HOST_FEAT_RF_CERT_READY
    rc = eh_host_feat_cli_rf_cert_register();
    if (rc != ESP_OK) {
        ESP_LOGE(CLI_TAG, "rf_cert CLI register failed: 0x%x", rc);
        cli_unregister_all();
        return rc;
    }
#endif

    return rc;
}

int eh_host_feat_cli_init(void)
{
    return (eh_host_feat_cli_register_commands() == ESP_OK) ? 0 : -1;
}

int eh_host_feat_cli_deinit(void)
{
    cli_unregister_all();
    return 0;
}

#if EH_HOST_AUTO_FEAT_INIT && EH_HOST_FEAT_CLI_AUTO_INIT
EH_HOST_FEAT_REGISTER(eh_host_feat_cli_init,
                      eh_host_feat_cli_deinit,
                      "cli", 400);
#endif

#if !EH_HOST_FEAT_RF_CERT_READY
void eh_host_feat_cli_rf_cert_overview(void) { }
#endif

/* Boxed command listing, shared so an application can add a section of its
 * own in the same shape. */
#include <stdio.h>
#include <string.h>


#define EH_CLI_BOX_W     75           /* inner width, fits an 80-column terminal */
#define EH_CLI_COL_CMD   25
#define EH_CLI_COL_SW    21

static void rule(int n)
{
    while (n-- > 0) {
        fputs("─", stdout);
    }
}

void eh_host_cli_box_open(const char *label)
{
    printf("%s╭─ %s%s%s ", EH_CLI_C_BOX, EH_CLI_C_LBL, label, EH_CLI_C_BOX);
    rule(EH_CLI_BOX_W - (int)strlen(label) - 4);
    printf("╮%s\n", EH_CLI_C_OFF);
}

void eh_host_cli_box_row(const char *cmd, const char *sw, const char *what)
{
    printf("%s│%s %s%-*s%s%-*s%s%-*s%s│%s\n",
           EH_CLI_C_BOX, EH_CLI_C_OFF,
           EH_CLI_C_CMD, EH_CLI_COL_CMD, cmd,
           EH_CLI_C_SW,  EH_CLI_COL_SW,  sw,
           EH_CLI_C_TXT, EH_CLI_BOX_W - EH_CLI_COL_CMD - EH_CLI_COL_SW - 2, what,
           EH_CLI_C_BOX, EH_CLI_C_OFF);
}

void eh_host_cli_box_close(void)
{
    printf("%s╰", EH_CLI_C_BOX);
    rule(EH_CLI_BOX_W - 1);        /* the label row spends one cell on "╭─ " */
    printf("╯%s\n", EH_CLI_C_OFF);
}


void eh_host_cli_box_text(const char *line)
{
    printf("%s│%s %-*s%s│%s\n", EH_CLI_C_BOX, EH_CLI_C_OFF,
           EH_CLI_BOX_W - 1, line, EH_CLI_C_BOX, EH_CLI_C_OFF);
}
