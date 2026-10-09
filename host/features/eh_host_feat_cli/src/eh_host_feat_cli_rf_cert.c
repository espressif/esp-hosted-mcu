/* SPDX-License-Identifier: Apache-2.0 */
/* RF certification test console — a port of the ESP-IDF phy/cert_test example
 * (examples/phy/cert_test/main/cmd_phy.c, Unlicense OR CC0-1.0).
 *
 * Names, option letters, defaults and printed text are kept identical to that
 * example, so commands and scripts carry over.  Only the call target changes:
 * esp_phy_* becomes eh_host_phy_cert_*.  The per-test tasks stay on the
 * coprocessor, where the blocking PHY calls are.
 *
 * phy_cert_init / _deinit / _query have no cert_test equivalent: cert_test
 * enters cert mode from app_main, and so does the host example.
 * gpio_output_set needs the GPIO expander, since the pin is on the
 * coprocessor.
 */

#include "eh_host_port_master_config.h"

#if EH_HOST_FEAT_RF_CERT_READY

#include <stdio.h>
#include <string.h>

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"

#include "eh_host_phy_cert.h"
#include "eh_host_cli.h"
#include "eh_host_feat_cli_priv.h"

#if EH_HOST_FEAT_GPIO_EXP_READY
#include "eh_host_cp_gpio.h"
#endif

#define TAG "cmd_phy"

/* Transport failures have no cert_test equivalent, so they carry their own tag
 * and leave the cmd_phy stream exactly as cert_test prints it. */
#define RPC_TAG "rf_cert_rpc"

typedef struct {
    struct arg_int *enable;
    struct arg_end *end;
} phy_args_t;

typedef struct {
    struct arg_int *channel;
    struct arg_int *rate;
    struct arg_int *attenuation;
    struct arg_int *length_byte;
    struct arg_int *packet_delay;
    struct arg_int *packet_num;
    struct arg_end *end;
} phy_wifi_tx_t;

typedef struct {
    struct arg_int *channel;
    struct arg_int *rate;
    struct arg_end *end;
} phy_wifi_rx_t;

typedef struct {
    struct arg_int *enable;
    struct arg_int *channel;
    struct arg_int *attenuation;
    struct arg_end *end;
} phy_wifiscwout_t;

typedef struct {
    struct arg_int *he_format;
    struct arg_int *pe;
    struct arg_int *giltf_num;
    struct arg_int *ru_index;
    struct arg_end *end;
} phy_wifi_11ax_tx_set_t;

typedef struct {
    struct arg_int *txpwr;
    struct arg_int *channel;
    struct arg_int *len;
    struct arg_int *data_type;
    struct arg_int *syncw;
    struct arg_int *rate;
    struct arg_int *tx_num_in;
    struct arg_end *end;
} phy_ble_tx_t;

typedef struct {
    struct arg_int *channel;
    struct arg_int *syncw;
    struct arg_int *rate;
    struct arg_end *end;
} phy_ble_rx_t;

typedef struct {
    struct arg_int *start;
    struct arg_int *channel;
    struct arg_int *attenuation;
    struct arg_end *end;
} phy_bt_tx_tone_t;

#if EH_HOST_FEAT_GPIO_EXP_READY
typedef struct {
    struct arg_int *gpio_number;
    struct arg_int *gpio_level;
    struct arg_end *end;
} phy_gpio_output_set_t;
#endif

static phy_args_t             phy_args;
static phy_wifi_tx_t          phy_wifi_tx_args;
static phy_wifi_rx_t          phy_wifi_rx_args;
static phy_wifiscwout_t       phy_wifiscwout_args;
static phy_wifi_11ax_tx_set_t phy_wifi_11ax_tx_set_args;
static phy_ble_tx_t           phy_ble_tx_args;
static phy_ble_rx_t           phy_ble_rx_args;
static phy_bt_tx_tone_t       phy_bt_tx_tone_args;
#if EH_HOST_FEAT_GPIO_EXP_READY
static phy_gpio_output_set_t  phy_gpio_output_set_args;
#endif

/* An RPC can fail where cert_test's local call cannot. */
static void report_rpc_error(const char *what, esp_err_t rc)
{
    if (rc == ESP_OK) {
        return;
    }
    if (rc == ESP_ERR_INVALID_STATE) {
        ESP_LOGE(RPC_TAG, "%s: coprocessor is not in RF cert mode", what);
    } else if (rc == ESP_ERR_NOT_FINISHED) {
        ESP_LOGE(RPC_TAG, "%s: a test is already running; send phy_cert_cmdstop first",
                 what);
    } else if (rc == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGE(RPC_TAG, "%s: not supported by this coprocessor", what);
    } else {
        ESP_LOGE(RPC_TAG, "%s: %s", what, esp_err_to_name(rc));
    }
}

static int esp_phy_tx_contin_en_func(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &phy_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, phy_args.end, argv[0]);
        return 1;
    }

    if (phy_args.enable->count == 1) {
        report_rpc_error("phy_cert_tx_contin_en",
                eh_host_phy_cert_tx_contin_en(phy_args.enable->ival[0]));
    } else {
        ESP_LOGW(TAG, "Please enter the enable parameter");
    }
    return 0;
}

static int esp_phy_cmdstop_func(int argc, char **argv)
{
    uint8_t value = 0;
    int nerrors = arg_parse(argc, argv, (void **) &phy_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, phy_args.end, argv[0]);
        return 1;
    }
    if (phy_args.enable->count == 1) {
        value = phy_args.enable->ival[0];
    }

    report_rpc_error("phy_cert_cmdstop", eh_host_phy_cert_test_start_stop(value));
    return 0;
}

static int esp_phy_get_rx_result_func(int argc, char **argv)
{
    esp_phy_rx_result_t rx_result = {0};
    esp_err_t rc;

    (void)argc;
    (void)argv;

    rc = eh_host_phy_cert_get_rx_result(&rx_result);
    if (rc != ESP_OK) {
        report_rpc_error("phy_cert_get_rx_result", rc);
        return 1;
    }

    ESP_LOGI(TAG, "Correct: %lu, Desired: %lu, RSSI: %d, flag: %lu",
                (unsigned long)rx_result.phy_rx_total_count,
                (unsigned long)rx_result.phy_rx_correct_count,
                rx_result.phy_rx_rssi,
                (unsigned long)rx_result.phy_rx_result_flag);

    return 0;
}

static int esp_phy_cbw40m_en_func(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &phy_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, phy_args.end, argv[0]);
        return 1;
    }
    if (phy_args.enable->count == 1) {
        report_rpc_error("phy_cert_cbw40m_en",
                eh_host_phy_cert_cbw40m_en(phy_args.enable->ival[0]));
    } else {
        ESP_LOGW(TAG, "Please enter the enable parameter");
    }
    return 0;
}

static int esp_phy_wifi_tx_func(int argc, char **argv)
{
    uint32_t channel;
    esp_phy_wifi_rate_t rate;
    int8_t backoff;
    uint32_t length_byte;
    uint32_t packet_delay;
    uint32_t packet_num;
    int nerrors = arg_parse(argc, argv, (void **) &phy_wifi_tx_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, phy_wifi_tx_args.end, argv[0]);
        return 1;
    }

    if (phy_wifi_tx_args.channel->count == 1) {
        channel = phy_wifi_tx_args.channel->ival[0];
    } else {
        channel = 1;
        ESP_LOGW(TAG, "Default channel is 1");
    }

    if (phy_wifi_tx_args.rate->count == 1) {
        rate = phy_wifi_tx_args.rate->ival[0];
    } else {
        rate = PHY_RATE_1M;
        ESP_LOGW(TAG, "Default rate is PHY_RATE_1M");
    }

    if (phy_wifi_tx_args.attenuation->count == 1) {
        backoff = phy_wifi_tx_args.attenuation->ival[0];
    } else {
        backoff = 0;
        ESP_LOGW(TAG, "Default backoff is 0");
    }

    if (phy_wifi_tx_args.length_byte->count == 1) {
        length_byte = phy_wifi_tx_args.length_byte->ival[0];
    } else {
        length_byte = 1000;
        ESP_LOGW(TAG, "Default length_byte is 1000");
    }

    if (phy_wifi_tx_args.packet_delay->count == 1) {
        packet_delay = phy_wifi_tx_args.packet_delay->ival[0];
    } else {
        packet_delay = 1000;
        ESP_LOGW(TAG, "Default packet_delay is 1000");
    }

    if (phy_wifi_tx_args.packet_num->count == 1) {
        packet_num = phy_wifi_tx_args.packet_num->ival[0];
    } else {
        packet_num = 0;
        ESP_LOGW(TAG, "Default packet_num is 0");
    }

    report_rpc_error("phy_cert_esp_tx",
            eh_host_phy_cert_wifi_tx(channel, rate, backoff, length_byte,
                                   packet_delay, packet_num));
    return 0;
}

static int esp_phy_wifi_rx_func(int argc, char **argv)
{
    uint32_t channel;
    esp_phy_wifi_rate_t rate;
    int nerrors = arg_parse(argc, argv, (void **) &phy_wifi_rx_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, phy_wifi_rx_args.end, argv[0]);
        return 1;
    }

    if (phy_wifi_rx_args.channel->count == 1) {
        channel = phy_wifi_rx_args.channel->ival[0];
    } else {
        channel = 1;
        ESP_LOGW(TAG, "Default channel is 1");
    }

    if (phy_wifi_rx_args.rate->count == 1) {
        rate = phy_wifi_rx_args.rate->ival[0];
    } else {
        rate = PHY_RATE_1M;
        ESP_LOGW(TAG, "Default rate is PHY_RATE_1M");
    }

    report_rpc_error("phy_cert_esp_rx", eh_host_phy_cert_wifi_rx(channel, rate));
    return 0;
}

static int esp_phy_wifiscwout_func(int argc, char **argv)
{
    uint32_t enable;
    uint32_t channel;
    uint32_t attenuation;
    int nerrors = arg_parse(argc, argv, (void **) &phy_wifiscwout_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, phy_wifiscwout_args.end, argv[0]);
        return 1;
    }

    if (phy_wifiscwout_args.enable->count == 1) {
        enable = phy_wifiscwout_args.enable->ival[0];
    } else {
        enable = 1;
        ESP_LOGW(TAG, "Default enable is 1");
    }

    if (phy_wifiscwout_args.channel->count == 1) {
        channel = phy_wifiscwout_args.channel->ival[0];
    } else {
        channel = 1;
        ESP_LOGW(TAG, "Default channel is 1");
    }

    if (phy_wifiscwout_args.attenuation->count == 1) {
        attenuation = phy_wifiscwout_args.attenuation->ival[0];
    } else {
        attenuation = 0;
        ESP_LOGW(TAG, "Default attenuation is 0");
    }

    report_rpc_error("phy_cert_wifiscwout",
            eh_host_phy_cert_wifi_tx_tone(enable, channel, attenuation));
    return 0;
}

static int esp_phy_wifi_11ax_tx_set_func(int argc, char **argv)
{
    uint32_t he_format;
    uint32_t pe;
    uint32_t giltf_num;
    uint32_t ru_index = 0;
    int nerrors = arg_parse(argc, argv, (void **) &phy_wifi_11ax_tx_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, phy_wifi_11ax_tx_set_args.end, argv[0]);
        return 1;
    }

    if (phy_wifi_11ax_tx_set_args.he_format->count == 1) {
        he_format = phy_wifi_11ax_tx_set_args.he_format->ival[0];
    } else {
        ESP_LOGE(TAG, "Please enter the HE format 1:HESU, 2:HEER, 3:HETB, 0:exit 11ax mode");
        return 1;
    }

    if (phy_wifi_11ax_tx_set_args.pe->count == 1) {
        pe = phy_wifi_11ax_tx_set_args.pe->ival[0];
    } else {
        pe = 16;
        ESP_LOGW(TAG, "Default pe is 16");
    }

    if (phy_wifi_11ax_tx_set_args.giltf_num->count == 1) {
        giltf_num = phy_wifi_11ax_tx_set_args.giltf_num->ival[0];
    } else {
        giltf_num = 1;
        ESP_LOGW(TAG, "Default giltf_num is 1");
    }

    if (he_format > 3) {
        ESP_LOGE(TAG, "HE format 1:HESU, 2:HEER, 3:HETB, 0:exit 11ax mode");
        return 1;
    }

    if (he_format == 3) {
        if (phy_wifi_11ax_tx_set_args.ru_index->count == 1) {
            ru_index = phy_wifi_11ax_tx_set_args.ru_index->ival[0];
        } else {
            ru_index = 0;
            ESP_LOGW(TAG, "Default ru_index is 0");
        }
    }

    report_rpc_error("phy_cert_phy_11ax_tx_set",
            eh_host_phy_cert_11ax_tx_set(he_format, pe, giltf_num, ru_index));
    return 0;
}

static int esp_phy_ble_tx_func(int argc, char **argv)
{
    uint32_t txpwr;
    uint32_t channel;
    uint32_t len;
    esp_phy_ble_type_t data_type;
    uint32_t syncw;
    esp_phy_ble_rate_t rate;
    uint32_t tx_num_in;
    int nerrors = arg_parse(argc, argv, (void **) &phy_ble_tx_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, phy_ble_tx_args.end, argv[0]);
        return 1;
    }

    if (phy_ble_tx_args.txpwr->count == 1) {
        txpwr = phy_ble_tx_args.txpwr->ival[0];
    } else {
        txpwr = 8;
        ESP_LOGW(TAG, "Default txpwr is 8");
    }

    if (phy_ble_tx_args.channel->count == 1) {
        channel = phy_ble_tx_args.channel->ival[0];
    } else {
        channel = 1;
        ESP_LOGW(TAG, "Default channel is 1");
    }

    if (phy_ble_tx_args.len->count == 1) {
        len = phy_ble_tx_args.len->ival[0];
    } else {
        len = 37;
        ESP_LOGW(TAG, "Default len is 37");
    }

    if (phy_ble_tx_args.data_type->count == 1) {
        data_type = phy_ble_tx_args.data_type->ival[0];
    } else {
        data_type = PHY_BLE_TYPE_prbs9;
        ESP_LOGW(TAG, "Default data_type is PHY_BLE_TYPE_prbs9");
    }

    if (phy_ble_tx_args.syncw->count == 1) {
        syncw = phy_ble_tx_args.syncw->ival[0];
    } else {
        syncw = 0x71764129;
        ESP_LOGW(TAG, "Default syncw is 0x71764129");
    }

    if (phy_ble_tx_args.rate->count == 1) {
        rate = phy_ble_tx_args.rate->ival[0];
    } else {
        rate = PHY_BLE_RATE_1M;
        ESP_LOGW(TAG, "Default rate is PHY_BLE_RATE_1M");
    }

    if (phy_ble_tx_args.tx_num_in->count == 1) {
        tx_num_in = phy_ble_tx_args.tx_num_in->ival[0];
    } else {
        tx_num_in = 0;
        ESP_LOGW(TAG, "Default tx_num_in is 0");
    }

    report_rpc_error("phy_cert_esp_ble_tx",
            eh_host_phy_cert_ble_tx(txpwr, channel, len, data_type, syncw,
                                  rate, tx_num_in));
    return 0;
}

static int esp_phy_ble_rx_func(int argc, char **argv)
{
    uint32_t channel;
    uint32_t syncw;
    esp_phy_ble_rate_t rate;
    int nerrors = arg_parse(argc, argv, (void **) &phy_ble_rx_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, phy_ble_rx_args.end, argv[0]);
        return 1;
    }

    if (phy_ble_rx_args.channel->count == 1) {
        channel = phy_ble_rx_args.channel->ival[0];
    } else {
        channel = 1;
        ESP_LOGW(TAG, "Default channel is 1");
    }

    if (phy_ble_rx_args.syncw->count == 1) {
        syncw = phy_ble_rx_args.syncw->ival[0];
    } else {
        syncw = 0x71764129;
        ESP_LOGW(TAG, "Default syncw is 0x71764129");
    }

    if (phy_ble_rx_args.rate->count == 1) {
        rate = phy_ble_rx_args.rate->ival[0];
    } else {
        rate = PHY_BLE_RATE_1M;
        ESP_LOGW(TAG, "Default rate is PHY_BLE_RATE_1M");
    }

    report_rpc_error("phy_cert_esp_ble_rx",
            eh_host_phy_cert_ble_rx(channel, syncw, rate));
    return 0;
}

static int esp_phy_bt_tx_tone_func(int argc, char **argv)
{
    uint32_t start;
    uint32_t channel;
    uint32_t attenuation;
    int nerrors = arg_parse(argc, argv, (void **) &phy_bt_tx_tone_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, phy_bt_tx_tone_args.end, argv[0]);
        return 1;
    }

    if (phy_bt_tx_tone_args.start->count == 1) {
        start = phy_bt_tx_tone_args.start->ival[0];
    } else {
        start = 1;
        ESP_LOGW(TAG, "Default start is 1");
    }

    if (phy_bt_tx_tone_args.channel->count == 1) {
        channel = phy_bt_tx_tone_args.channel->ival[0];
    } else {
        channel = 1;
        ESP_LOGW(TAG, "Default channel is 1");
    }

    if (phy_bt_tx_tone_args.attenuation->count == 1) {
        attenuation = phy_bt_tx_tone_args.attenuation->ival[0];
    } else {
        attenuation = 0;
        ESP_LOGW(TAG, "Default backoff is 0");
    }

    report_rpc_error("phy_cert_bt_tx_tone",
            eh_host_phy_cert_bt_tx_tone(start, channel, attenuation));
    return 0;
}

#if EH_HOST_FEAT_GPIO_EXP_READY
/* cert_test drives a pin on the chip it runs on — the one with the radio, so
 * here the coprocessor.  The pin number is a coprocessor pin, and the
 * coprocessor validates it; the host has no view of its pin map. */
static void esp_phy_gpio_output_set(int number, int level)
{
    eh_host_cp_gpio_config_t io_conf = {0};
    esp_err_t rc;

    if (level != 0 && level != 1) {
        ESP_LOGE(TAG, "gpio level %d is invalid, should be 0 or 1", level);
        return;
    }

    io_conf.pin_bit_mask = (1ULL << number);
    io_conf.mode         = EH_HOST_CP_GPIO_MODE_OUTPUT;
    io_conf.pull_up_en   = 0;
    io_conf.pull_down_en = 0;
    io_conf.intr_type    = 0;

    rc = eh_host_cp_gpio_config(&io_conf);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "gpio number %d is invalid out gpio", number);
        report_rpc_error("phy_cert_gpio_output_set", rc);
        return;
    }

    ESP_LOGI(TAG, "Set output gpio number %d level to %d", number, level);
    report_rpc_error("phy_cert_gpio_output_set",
            eh_host_cp_gpio_set_level(number, level));
}

static int esp_phy_gpio_output_set_func(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &phy_gpio_output_set_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, phy_gpio_output_set_args.end, argv[0]);
        return 1;
    }

    if (phy_gpio_output_set_args.gpio_number->count != 1) {
        ESP_LOGE(TAG, "please input gpio number");
        return 1;
    }
    if (phy_gpio_output_set_args.gpio_level->count != 1) {
        ESP_LOGE(TAG, "please input gpio level");
        return 1;
    }

    esp_phy_gpio_output_set(phy_gpio_output_set_args.gpio_number->ival[0],
                            phy_gpio_output_set_args.gpio_level->ival[0]);
    return 0;
}
#endif /* EH_HOST_FEAT_GPIO_EXP_READY */

/* ---- hosted-only lifecycle commands ---------------------------------- */

static int phy_cert_init_func(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    report_rpc_error("phy_cert_init", eh_host_phy_cert_init());
    return 0;
}

static int phy_cert_deinit_func(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    report_rpc_error("phy_cert_deinit", eh_host_phy_cert_deinit());
    return 0;
}

static int phy_cert_status_func(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("cert support: %s\n",
           eh_host_phy_cert_query(EH_HOST_PHY_CERT_QUERY_CONFIGURED) == ESP_OK
                   ? "yes" : "no");
    printf("cert mode entered: %s\n",
           eh_host_phy_cert_query(EH_HOST_PHY_CERT_QUERY_INITED) == ESP_OK
                   ? "yes" : "no");
    return 0;
}

/* ---- grouped overview ------------------------------------------------ */

/* esp_console's own `help` lists every command flat, with its arguments.

 *
 * Only commands this feature registers are listed. An application that adds
 * its own, such as the rf_cert example's coprocessor OTA, prints them after
 * calling this.
 * This adds the other half: what exists, grouped by radio, and the order a
 * run goes in.  Colours follow CONFIG_LOG_COLORS so a console configured
 * without them stays plain. */

void eh_host_feat_cli_rf_cert_overview(void)
{
    printf("\n  %shelp%s lists every command; %shelp <command>%s gives its switches in detail.\n\n",
           EH_CLI_C_CMD, EH_CLI_C_OFF, EH_CLI_C_CMD, EH_CLI_C_OFF);
    printf("  %s%-*s%-*s%s%s\n", EH_CLI_C_LBL, 25, "command", 21, "switches", "what it does", EH_CLI_C_OFF);

    eh_host_cli_box_open("Session");
    eh_host_cli_box_row("phy_cert_query",      "",           "can the CP do this?");
    eh_host_cli_box_row("phy_cert_init",       "",           "re-enter after a deinit");
    eh_host_cli_box_row("phy_cert_deinit",     "",           "stop, power radio down");
    eh_host_cli_box_row("phy_cert_commands",   "",           "this overview");
    eh_host_cli_box_close();

    eh_host_cli_box_open("Wi-Fi");
    eh_host_cli_box_row("phy_cert_esp_tx",     "-n -r -p -l -d -c", "packet TX, -c 0 forever");
    eh_host_cli_box_row("phy_cert_esp_rx",     "-n -r",      "packet RX, counts for PER");
    eh_host_cli_box_row("phy_cert_wifiscwout", "-e -c -p",   "carrier wave on/off");
    eh_host_cli_box_row("phy_cert_cbw40m_en",  "<0|1>",      "HT40 on/off");
    eh_host_cli_box_row("phy_cert_tx_contin_en", "<0|1>",    "continuous TX on/off");
    eh_host_cli_box_row("phy_cert_phy_11ax_tx_set", "-f -p -g -i", "11ax format, HE parts");
    eh_host_cli_box_close();

    eh_host_cli_box_open("Bluetooth LE");
    eh_host_cli_box_row("phy_cert_esp_ble_tx", "-p -n -l -t -s -r -m", "packet TX, -m 0 forever");
    eh_host_cli_box_row("phy_cert_esp_ble_rx", "-n -s -r",   "packet RX, counts for PER");
    eh_host_cli_box_row("phy_cert_bt_tx_tone", "-e -n -p",   "carrier wave on/off");
    eh_host_cli_box_close();

    eh_host_cli_box_open("Any test");
    eh_host_cli_box_row("phy_cert_cmdstop",    "[<val>]",    "end the running test");
    eh_host_cli_box_row("phy_cert_get_rx_result", "",        "correct, total, RSSI, PER");
#if EH_HOST_FEAT_GPIO_EXP_READY
    eh_host_cli_box_row("phy_cert_gpio_output_set", "-n -l", "drive a coprocessor pin");
#endif
    eh_host_cli_box_close();

    printf("\n  %sProcedure%s\n", EH_CLI_C_LBL, EH_CLI_C_OFF);
    printf("    1. phy_cert_query           confirm support; cert mode is already on\n");
    printf("    2. start a test             e.g. phy_cert_esp_tx -n 1 -r 0 -c 0\n");
    printf("    3. measure                  on the analyser\n");
    printf("    4. phy_cert_cmdstop         end it, then repeat from 2\n");
    printf("    5. phy_cert_deinit          optional; phy_cert_init re-enters\n");
    printf("\n  Restart clears the PHY's cert state, not this test firmware.\n");
    printf("  When testing is done, put your production image on the coprocessor.\n");
}

static int cmd_overview(int argc, char **argv)
{
    (void)argc; (void)argv;
    eh_host_feat_cli_rf_cert_overview();
    return 0;
}

/* ---- registration ---------------------------------------------------- */

/* Help text is corrected against esp_phy_cert_test.h where cert_test's own
 * is wrong or drops a unit; it is documentation only and no switch changes.
 *
 * Two names per command: cert_test's own, so commands and scripts carry over,
 * and the same name behind a phy_cert_ prefix, which keeps it findable in a
 * console carrying other features.  Only the prefixed one carries help text,
 * so `help` lists each command once. */
#if EH_HOST_FEAT_RF_CERT_CLI_IDF_NAMES
#define IDF_NAME(s) (s)
#else
#define IDF_NAME(s) NULL
#endif

static esp_err_t reg(const char *native, const char *idf, const char *help,
                     esp_console_cmd_func_t fn, void *args)
{
    const esp_console_cmd_t n = {
        .command = native, .help = help, .hint = NULL,
        .func = fn, .argtable = args,
    };
    esp_err_t rc = esp_console_cmd_register(&n);

    if (rc != ESP_OK || idf == NULL) {
        return rc;
    }
    const esp_console_cmd_t a = {
        .command = idf, .help = NULL, .hint = NULL,
        .func = fn, .argtable = args,
    };
    return esp_console_cmd_register(&a);
}

esp_err_t eh_host_feat_cli_rf_cert_register(void)
{
    esp_err_t rc;

#define REG(native, idf, help, fn, args) do {                               \
        rc = reg((native), (idf), (help), (fn), (args));                     \
        if (rc != ESP_OK) {                                                  \
            return rc;                                                       \
        }                                                                    \
    } while (0)

/* Parameters are underscored: the designators .help and .command would
 * otherwise be substituted by the preprocessor. */
#define REG_ONE(_cmd, _help, _fn, _args) do {                                \
        const esp_console_cmd_t c = {                                        \
            .command = (_cmd), .help = (_help), .hint = NULL,                \
            .func = (_fn), .argtable = (_args),                              \
        };                                                                   \
        rc = esp_console_cmd_register(&c);                                   \
        if (rc != ESP_OK) {                                                  \
            return rc;                                                       \
        }                                                                    \
    } while (0)

    phy_args.enable = arg_int0(NULL, NULL, "<enable>", "enable");
    phy_args.end = arg_end(1);

    REG_ONE("phy_cert_commands", "Show the commands grouped by radio, and the order a run goes in.",
        &cmd_overview, NULL);

    REG("phy_cert_tx_contin_en", IDF_NAME("tx_contin_en"),
        "TX Continuous mode, 1: enable, 0: disable",
        &esp_phy_tx_contin_en_func, &phy_args);
    REG("phy_cert_cmdstop", IDF_NAME("cmdstop"), "TX/RX test stop command. 0 ends the running test; the coprocessor sets the value 3 that arms a test itself.",
        &esp_phy_cmdstop_func, NULL);
    REG("phy_cert_get_rx_result", IDF_NAME("get_rx_result"), "Get RX information. Reports correct and total packet counts and average RSSI in 0.1 dBm; the flag says which radio, 1 Wi-Fi and 2 BLE.",
        &esp_phy_get_rx_result_func,
        NULL);
    REG("phy_cert_cbw40m_en", IDF_NAME("cbw40m_en"),
        "HT40/HT20 mode selection, 0: HT20, 1: HT40",
        &esp_phy_cbw40m_en_func, &phy_args);

    phy_wifi_tx_args.channel     = arg_int0("n", "channel"     , "<channel>"     , "channel setting, 1~14");
    phy_wifi_tx_args.rate        = arg_int0("r", "rate"        , "<rate>"        , "rate setting");
    phy_wifi_tx_args.attenuation = arg_int0("p", "attenuation" , "<attenuation>" , "Transmit power attenuation");
    phy_wifi_tx_args.length_byte = arg_int0("l", "length_byte" , "<length_byte>" , "TX packet length configuration");
    phy_wifi_tx_args.packet_delay= arg_int0("d", "packet_delay", "<packet_delay>", "TX packet interval configuration");
    phy_wifi_tx_args.packet_num  = arg_int0("c", "packet_num"  , "<packet_num>"  , "The number of packets to send");
    phy_wifi_tx_args.end         = arg_end(1);
    REG("phy_cert_esp_tx", IDF_NAME("esp_tx"), "WiFi TX command. Attenuation is in 0.25 dB steps, so 4 gives 1 dB. Length is the PSDU in bytes, delay is in us. A packet count of 0 transmits until cmdstop.", &esp_phy_wifi_tx_func,
        &phy_wifi_tx_args);

    phy_wifi_rx_args.channel = arg_int0("n", "channel", "<channel>", "channel setting, 1~14");
    phy_wifi_rx_args.rate    = arg_int0("r", "rate"   , "<rate>"   , "rate setting");
    phy_wifi_rx_args.end     = arg_end(1);
    REG("phy_cert_esp_rx", IDF_NAME("esp_rx"), "WiFi RX command. Runs until cmdstop; read the counts with get_rx_result.", &esp_phy_wifi_rx_func,
        &phy_wifi_rx_args);

    phy_wifiscwout_args.enable      = arg_int0("e", "start"      , "<start>"      , "enable CW");
    phy_wifiscwout_args.channel     = arg_int0("c", "channel"    , "<channel>"    , "channel setting, 1~14");
    phy_wifiscwout_args.attenuation = arg_int0("p", "attenuation", "<attenuation>", "Transmit power attenuation");
    phy_wifiscwout_args.end         = arg_end(1);
    REG("phy_cert_wifiscwout", IDF_NAME("wifiscwout"), "Wi-Fi CW TX command. Attenuation is in 0.25 dB steps, so 4 gives 1 dB.",
        &esp_phy_wifiscwout_func,
        &phy_wifiscwout_args);

    phy_wifi_11ax_tx_set_args.he_format = arg_int0("f", "he_format", "<he_format>", "1:HESU, 2:HEER, 3:HETB, 0:exit 11ax mode");
    phy_wifi_11ax_tx_set_args.pe        = arg_int0("p", "pe"       , "<pe>"       , "pe=0/8/16, default 16");
    phy_wifi_11ax_tx_set_args.giltf_num = arg_int0("g", "giltf_num", "<giltf_num>", "giltf_num=1/2/3, default 1");
    phy_wifi_11ax_tx_set_args.ru_index  = arg_int0("i", "ru_index" , "<ru_index>" , "0~8, 37~40, 53~54, 61,\
        ru_index is only effective in HETB mode. In other modes, this parameter can be omitted or set to 0, with the default value being 0");
    phy_wifi_11ax_tx_set_args.end       = arg_end(1);
    REG("phy_cert_phy_11ax_tx_set", IDF_NAME("phy_11ax_tx_set"), "WiFi 11ax TX set command. Needs a coprocessor with 802.11ax. ru_index applies in HETB mode only.",
        &esp_phy_wifi_11ax_tx_set_func, &phy_wifi_11ax_tx_set_args);

    phy_ble_tx_args.txpwr     = arg_int0("p", "txpwr"    , "<txpwr>"    , "Transmit power level setting");
    phy_ble_tx_args.channel   = arg_int0("n", "channel"  , "<channel>"  , "TX channel setting, range is 0~39");
    phy_ble_tx_args.len       = arg_int0("l", "len"      , "<len>"      , "Payload length setting, range is 0-255");
    phy_ble_tx_args.data_type = arg_int0("t", "data_type", "<data_type>", "Data type setting");
    phy_ble_tx_args.syncw     = arg_int0("s", "syncw"    , "<syncw>"    , "Packet identification");
    phy_ble_tx_args.rate      = arg_int0("r", "rate"     , "<rate>"     , "TX rate setting,0: 1M; 1: 2M; 2: 125K; 3: 500K");
    phy_ble_tx_args.tx_num_in = arg_int0("m", "tx_num_in", "<tx_num_in>", "Number of packets to send; 0 sends continuously");
    phy_ble_tx_args.end       = arg_end(1);
    REG("phy_cert_esp_ble_tx", IDF_NAME("esp_ble_tx"), "BLE TX command. Channel 0~39 is 2402 + chan*2 MHz. TX power is about (level-8)*3 dBm, so level 8 is around 0 dBm. A packet count of 0 transmits until cmdstop.", &esp_phy_ble_tx_func,
        &phy_ble_tx_args);
#if EH_HOST_FEAT_RF_CERT_CLI_IDF_NAMES
    REG_ONE("fcc_le_tx", NULL, &esp_phy_ble_tx_func, &phy_ble_tx_args);
#endif

    phy_ble_rx_args.channel = arg_int0("n", "channel", "<channel>", "RX channel setting, range is 0~39");
    phy_ble_rx_args.syncw   = arg_int0("s", "syncw"  , "<syncw>"  , "Packet identification");
    phy_ble_rx_args.rate    = arg_int0("r", "rate"   , "<rate>"   , "RX rate setting,0: 1M; 1: 2M; 2: 125K; 3: 500K");
    phy_ble_rx_args.end     = arg_end(1);
    REG("phy_cert_esp_ble_rx", IDF_NAME("esp_ble_rx"), "BLE RX command. Channel 0~39 is the BLE data-channel index, where 0 is 2404 MHz and 37, 38, 39 are 2402, 2426, 2480 MHz. Read the counts with get_rx_result.", &esp_phy_ble_rx_func,
        &phy_ble_rx_args);
#if EH_HOST_FEAT_RF_CERT_CLI_IDF_NAMES
    REG_ONE("rw_le_rx_per", NULL, &esp_phy_ble_rx_func, &phy_ble_rx_args);
#endif

    phy_bt_tx_tone_args.start       = arg_int0("e", "start"  , "<start>"  , "enable CW, 1 means transmit, 0 means stop transmitting");
    phy_bt_tx_tone_args.channel     = arg_int0("n", "channel", "<channel>", "Single carrier transmission channel selection");
    phy_bt_tx_tone_args.attenuation = arg_int0("p", "power"  , "<power>"  , "CW power attenuation parameter");
    phy_bt_tx_tone_args.end         = arg_end(1);
    REG("phy_cert_bt_tx_tone", IDF_NAME("bt_tx_tone"), "Single carrier TX command. Channel 0~39 is 2402 + chan*2 MHz. Attenuation is in 0.25 dB steps, so 4 gives 1 dB.",
        &esp_phy_bt_tx_tone_func,
        &phy_bt_tx_tone_args);

#if EH_HOST_FEAT_GPIO_EXP_READY
    phy_gpio_output_set_args.gpio_number = arg_int0("n", "number", "<gpio_number>", "output gpio number");
    phy_gpio_output_set_args.gpio_level  = arg_int0("l", "level" , "<gpio_level>" , "output gpio level");
    phy_gpio_output_set_args.end         = arg_end(1);
    REG("phy_cert_gpio_output_set", IDF_NAME("gpio_output_set"), "gpio output set command. Drives a pin on the coprocessor, through the GPIO expander.",
        &esp_phy_gpio_output_set_func, &phy_gpio_output_set_args);
#endif

    /* No cert_test equivalent, so no aliasing. */
    REG_ONE("phy_cert_init", "enter RF cert mode on the coprocessor",
        &phy_cert_init_func, NULL);
    REG_ONE("phy_cert_deinit", "stop the test and power down the Wi-Fi domain",
        &phy_cert_deinit_func, NULL);
    REG_ONE("phy_cert_query",
        "show cert support and whether cert mode is entered",
        &phy_cert_status_func, NULL);

#undef REG
#undef REG_ONE
#undef IDF_NAME

    ESP_LOGI(TAG, "RF cert commands registered");
    return ESP_OK;
}

#endif /* EH_HOST_FEAT_RF_CERT_READY */
