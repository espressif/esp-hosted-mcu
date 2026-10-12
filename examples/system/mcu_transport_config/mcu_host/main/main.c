/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Transport configuration example.
 *
 * The application, not menuconfig, chooses the bus settings. Auto-init is off
 * (see sdkconfig.defaults), so app_main starts ESP-Hosted itself:
 *
 *   1. set_config(clock A) -> esp_hosted_init -> connect_to_slave    link up at clock A
 *   2. set_config(clock B)                                           refused: link is up
 *   3. esp_hosted_deinit -> set_config(clock B) -> init -> connect   link up at clock B
 *   4. optional extras, one call each: link monitor, Wi-Fi station, BLE advertising
 *
 * The "EH stage=..." lines are what tests/suites/.../test_transport_config.py checks.
 */
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_hosted.h"
#include "esp_hosted_transport_config.h"
#include "example_private.h"

static const char *TAG = EH_EX_TAG;

/*
 * The clock used for the first start (A) and after the restart (B), for the bus
 * chosen in menuconfig. UART keeps its baud on both starts because it must match
 * the co-processor; it only tries another baud for the refused change in step 2.
 */
#if EH_HOST_TRANSPORT_BUS_SDIO
#define BUS "sdio"
#define KEY "clk_khz"
#define CLOCK_A 20000
#define CLOCK_B 40000
#define CHANGE_WHILE_UP CLOCK_B
#elif EH_HOST_TRANSPORT_BUS_SPI
#define BUS "spi"
#define KEY "clk_mhz"
#define CLOCK_A 20
#define CLOCK_B 26
#define CHANGE_WHILE_UP CLOCK_B
#elif EH_HOST_TRANSPORT_BUS_SPI_HD
#define BUS "spi_hd"
#define KEY "clk_mhz"
#define CLOCK_A 20
#define CLOCK_B 40
#define CHANGE_WHILE_UP CLOCK_B
#else
#define BUS "uart"
#define KEY "baud"
#define CLOCK_A 921600
#define CLOCK_B 921600
#define CHANGE_WHILE_UP 460800
#endif

#define LINK_UP_TIMEOUT_MS 10000

/*
 * Set the bus clock (UART: baud rate). Start from the menuconfig defaults
 * (INIT_DEFAULT_HOST_*_CONFIG) and change only the field you need; pins and
 * the rest stay as configured for the board.
 *
 * Returns ESP_TRANSPORT_ERR_ALREADY_SET while the link is up.
 */
static esp_hosted_transport_err_t set_bus_config(uint32_t value)
{
#if EH_HOST_TRANSPORT_BUS_SDIO
    struct esp_hosted_sdio_config cfg = INIT_DEFAULT_HOST_SDIO_CONFIG();
    cfg.clock_freq_khz = value;
    return esp_hosted_sdio_set_config(&cfg);
#elif EH_HOST_TRANSPORT_BUS_SPI
    struct esp_hosted_spi_config cfg = INIT_DEFAULT_HOST_SPI_CONFIG();
    cfg.clk_mhz = value;
    return esp_hosted_spi_set_config(&cfg);
#elif EH_HOST_TRANSPORT_BUS_SPI_HD
    struct esp_hosted_spi_hd_config cfg = INIT_DEFAULT_HOST_SPI_HD_CONFIG();
    cfg.clk_mhz = value;
    return esp_hosted_spi_hd_set_config(&cfg);
#else
    struct esp_hosted_uart_config cfg = INIT_DEFAULT_HOST_UART_CONFIG();
    cfg.baud_rate = value;
    return esp_hosted_uart_set_config(&cfg);
#endif
}

/* Read back the clock (UART: baud) that ESP-Hosted holds for the bus. */
static uint32_t get_bus_config(void)
{
    struct esp_hosted_transport_config *cfg = NULL;
    if (esp_hosted_transport_get_config(&cfg) != ESP_TRANSPORT_OK || !cfg) {
        return 0;
    }
#if EH_HOST_TRANSPORT_BUS_SDIO
    return cfg->u.sdio.clock_freq_khz;
#elif EH_HOST_TRANSPORT_BUS_SPI
    return cfg->u.spi.clk_mhz;
#elif EH_HOST_TRANSPORT_BUS_SPI_HD
    return cfg->u.spi_hd.clk_mhz;
#else
    return cfg->u.uart.baud_rate;
#endif
}

/* Set the clock and log what was set and what is read back. */
static esp_hosted_transport_err_t configure(uint32_t value)
{
    esp_hosted_transport_err_t rc = set_bus_config(value);
    ESP_LOGI(TAG, "EH stage=transport bus=" BUS " set " KEY "=%lu rc=%d", (unsigned long)value, rc);
    ESP_LOGI(TAG, "EH stage=transport get " KEY "=%lu", (unsigned long)get_bus_config());
    return rc;
}

/*
 * Start ESP-Hosted on the configured bus: init, reset the co-processor and
 * connect, then wait for the link. The bus driver logs the clock it actually
 * set, e.g. "bus applied: clk_khz=20000".
 */
static esp_err_t hosted_start(void)
{
    if (esp_hosted_init() != 0) {
        return ESP_FAIL;
    }
    example_expect_cp_init();              /* the co-processor's next start-up message belongs to this start */
    if (esp_hosted_connect_to_slave() != 0) {
        return ESP_FAIL;
    }
    if (example_wait_link_up(LINK_UP_TIMEOUT_MS) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG, "EH stage=hosted linked");
    ESP_LOGI(TAG, "ESP-Hosted initialized successfully");
    return ESP_OK;
}

/* Recovery used by the link monitor: stop and start again with the current config. */
static esp_err_t hosted_restart(void)
{
    ESP_LOGW(TAG, "EH stage=recovery reinit begin");
    example_clear_conn_bits();
    esp_hosted_deinit();
    esp_err_t err = hosted_start();
    ESP_LOGW(TAG, "EH stage=recovery reinit end rc=%d", err);
    return err;
}

void app_main(void)
{
    /* NVS holds the Wi-Fi calibration and NimBLE data. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(example_events_init());       /* tracks link up/down events */

    /* 1. Set the clock before starting ESP-Hosted, then start it. */
    esp_hosted_transport_err_t rc = configure(CLOCK_A);
    if (rc == ESP_TRANSPORT_ERR_ALREADY_SET) {
        ESP_LOGE(TAG, "link already up: set CONFIG_ESP_HOSTED_AUTO_CALL_INIT_BEFORE_APP_MAIN=n");
    }
    if (rc != ESP_TRANSPORT_OK || hosted_start() != ESP_OK) {
        ESP_LOGE(TAG, "hosted bring-up failed");
        return;
    }

    /* 2. While the link is up, the config cannot change: set_config is refused
     *    and the current config stays in use. */
    rc = set_bus_config(CHANGE_WHILE_UP);
    ESP_LOGI(TAG, "EH stage=transport set-while-up refused=%d rc=%d",
             rc == ESP_TRANSPORT_ERR_ALREADY_SET, rc);
    ESP_LOGI(TAG, "EH stage=transport get " KEY "=%lu", (unsigned long)get_bus_config());

    /* 3. To change it: stop ESP-Hosted, set the new clock, start again. */
    example_clear_conn_bits();
    esp_hosted_deinit();
    err = (configure(CLOCK_B) == ESP_TRANSPORT_OK) ? hosted_start() : ESP_FAIL;
    ESP_LOGI(TAG, "EH stage=transport reconfigured rc=%d", err);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hosted reconfigure failed");
        return;
    }

    /* 4. Optional extras, each in its own file. */
    ESP_ERROR_CHECK(example_bus_monitor_start(hosted_restart));   /* heartbeat; restarts the link if the co-processor stops */
#if CONFIG_EXAMPLE_MANUAL_ENABLE_WIFI_STA
    example_wifi_sta_start();                                     /* connect to the AP set in menuconfig */
#endif
#if CONFIG_EXAMPLE_MANUAL_ENABLE_BLE_ADV
    if (example_ble_start() != ESP_OK) {                          /* NimBLE advertising */
        ESP_LOGE(TAG, "BLE bring-up failed");
    }
#endif
    ESP_LOGI(TAG, "EH manual bringup complete");
}
