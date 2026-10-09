/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * phy_cert_cp_ota — always registered, so the console can answer for it
 * either way. The work lives in components/rf_cert_ota; built out, this
 * says how to build it in.
 */

#include "sdkconfig.h"

#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"

#include "eh_host_cli.h"
#include "rf_cert_ota_cmd.h"

#if CONFIG_RF_CERT_EXAMPLE_INTEGRATED_OTA
#include "rf_cert_ota.h"
#else
static const char *TAG = "rf_cert_ota";
#endif

static int cmd_cp_ota(int argc, char **argv)
{
    (void)argc; (void)argv;

#if CONFIG_RF_CERT_EXAMPLE_INTEGRATED_OTA
    /* Returns only when it did not send: ESP_OK means it deliberately
     * skipped, anything else is a failure. A successful send restarts. */
    return rf_cert_ota_perform() == ESP_OK ? 0 : 1;
#else
    ESP_LOGW(TAG, "not built in");
    printf("\n"
           "  1. Build the coprocessor app you want to end up running\n"
           "       <cp-project>/build/<project_name>.bin\n"
           "\n"
           "  2. Copy that .bin into the host project, one file only\n"
           "       components/rf_cert_ota/cp_fw_bin/\n"
           "\n"
           "  3. Turn it on, with sdkconfig.defaults.ota or by hand\n"
           "       RF certification test example\n"
           "       └── [*] Integrate coprocessor OTA (phy_cert_cp_ota)\n"
           "\n"
           "  4. Rebuild the host\n"
           "\n"
           "  5. Flash the host — the image lands in cp_fw with it\n"
           "\n"
           "  Then run phy_cert_cp_ota again.\n"
           "\n");
    return 1;
#endif
}

void rf_cert_ota_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "phy_cert_cp_ota",
        .help    = "Update the coprocessor from the host's cp_fw partition, over the hosted link.",
        .hint    = NULL,
        .func    = &cmd_cp_ota,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void rf_cert_ota_print_hint(void)
{
    eh_host_cli_box_open("After certification");
    eh_host_cli_box_row("phy_cert_cp_ota", "",
#if CONFIG_RF_CERT_EXAMPLE_INTEGRATED_OTA
                        "replace the CP firmware");
#else
                        "not built in; run it");
#endif
    eh_host_cli_box_text("");
    eh_host_cli_box_text("  Sends a coprocessor image staged on this host over the");
    eh_host_cli_box_text("  same link the tests use, then both sides restart. It is");
    eh_host_cli_box_text("  the way off this test firmware when the coprocessor's");
    eh_host_cli_box_text("  own UART cannot be reached.");
    eh_host_cli_box_close();
    printf("\n");
}
