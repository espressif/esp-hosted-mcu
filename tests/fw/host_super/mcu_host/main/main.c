/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

/* Test-only super host: asks for a scenario on the console, starts ESP-Hosted the way
 * the auto-init constructor would, then runs that example's own app_main. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_rom_serial_output.h"
#include "esp_log.h"
#include "eh_host_core.h"
#include "eh_host_transport_config.h"
#include "eh_host_event.h"

static const char *TAG = "eh_super";

/* What the auto-init constructor runs (eh_host_core_auto_init.c), started once the
 * scenario is known so runtime options can apply before init. */
void eh_host_core_lifecycle_locks_init(void);

static void hosted_start_task(void *arg)
{
	(void)arg;
	if (eh_host_init(NULL) != 0 || eh_host_connect_to_slave() != 0)
		ESP_LOGE(TAG, "hosted start failed");
	vTaskDelete(NULL);
}

/* init_race: one eh_host_init() from a single context, then two tasks on different
 * cores call eh_host_connect_to_slave() at once. One bring-up must run: one
 * TRANSPORT_UP, both callers 0. No lock is pre-created, unlike a normal scenario. */
static volatile int s_up_events;
static SemaphoreHandle_t s_go, s_done;

static void on_transport_up(void *a, esp_event_base_t b, int32_t id, void *d)
{
	s_up_events++;
}

static void race_connect(void *arg)
{
	xSemaphoreTake(s_go, portMAX_DELAY);
	int rc = eh_host_connect_to_slave();
	ESP_LOGI(TAG, "init_race: connect%d rc=%d", (int)(intptr_t)arg, rc);
	xSemaphoreGive(s_done);
	vTaskDelete(NULL);
}

static void init_race(void)
{
	s_go = xSemaphoreCreateCounting(2, 0);
	s_done = xSemaphoreCreateCounting(2, 0);
	ESP_ERROR_CHECK(eh_host_init(NULL));
	esp_event_handler_register(EH_HOST_EVENT, EH_HOST_EVENT_TRANSPORT_UP, on_transport_up, NULL);
	for (int i = 0; i < 2; i++)
		xTaskCreatePinnedToCore(race_connect, "race", 4096, (void *)(intptr_t)i, 23, NULL,
					i % portNUM_PROCESSORS);
	vTaskDelay(pdMS_TO_TICKS(50));
	xSemaphoreGive(s_go);
	xSemaphoreGive(s_go);
	xSemaphoreTake(s_done, portMAX_DELAY);
	xSemaphoreTake(s_done, portMAX_DELAY);
	vTaskDelay(pdMS_TO_TICKS(1000));
	ESP_LOGI(TAG, "init_race: done transport_up=%d", s_up_events);
}

/* Every super host shares this table; a scenario not linked into this image is NULL. */
#define SC(n) __attribute__((weak)) void eh_sc_##n##_main(void);
#define SCENARIOS \
	SC(api_exerciser) SC(get_cp_fw_version) SC(hosted_events) SC(mem_monitor) \
	SC(peer_data_transfer) SC(ext_coex) SC(wifi_sta) SC(shut_down_cp_when_unused) SC(ota) SC(rf_cert) \
	SC(bleprph_minimal) SC(bleprph_gatt) SC(bleprph_wifi_coex) \
	SC(ble_gatt_server) SC(ble_compatibility_test) SC(controller_mac_addr)
SCENARIOS
#undef SC

#define SC(n) { #n, eh_sc_##n##_main },
static const struct { const char *name; void (*main)(void); } s_scenarios[] = { SCENARIOS };
#undef SC

static void read_line(char *buf, size_t len)
{
	size_t n = 0;

	for (;;) {
		uint8_t c;
		if (esp_rom_output_rx_one_char(&c) != 0) {
			vTaskDelay(pdMS_TO_TICKS(10));
			continue;
		}
		if (c == '\r' || c == '\n') {
			if (n)
				break;
			continue;
		}
		if (n + 1 < len)
			buf[n++] = (char)c;
	}
	buf[n] = '\0';
}

/* "key=value" options after the scenario name: transport sub-variants and log level
 * that would otherwise each need their own image. A test asked for them, so one that
 * does not apply stops the boot instead of running the default silently. */
static void apply_option(const char *opt)
{
	int v, rc = 0;

	if (strcmp(opt, "log=debug") == 0) {
		esp_log_level_set("*", ESP_LOG_DEBUG);
#if CONFIG_ESP_HOSTED_HOST_TRANSPORT_BUS_SDIO
	} else if (sscanf(opt, "sdio_width=%d", &v) == 1) {
		struct eh_host_sdio_config cfg = INIT_DEFAULT_HOST_SDIO_CONFIG();
		cfg.bus_width = (uint8_t)v;
		rc = eh_host_sdio_set_config(&cfg);
#endif
#if CONFIG_ESP_HOSTED_HOST_TRANSPORT_BUS_SPI_HD
	} else if (sscanf(opt, "spi_hd_lines=%d", &v) == 1) {
		struct eh_host_spi_hd_config cfg = INIT_DEFAULT_HOST_SPI_HD_CONFIG();
		cfg.num_data_lines = (uint8_t)v;
		rc = eh_host_spi_hd_set_config(&cfg);
#endif
	} else {
		rc = -1;
	}
	ESP_LOGI(TAG, "option %s rc=%d", opt, rc);
	if (rc != 0)
		abort();
}

void app_main(void)
{
	char line[96];

	for (;;) {
		printf("eh_super: select scenario\n");
		read_line(line, sizeof(line));
		char *name = strncmp(line, "scenario ", 9) == 0 ? line + 9 : line;
		char *save = NULL;
		name = strtok_r(name, " ", &save);
		if (!name)
			continue;
		for (char *opt = strtok_r(NULL, " ", &save); opt; opt = strtok_r(NULL, " ", &save))
			apply_option(opt);
		if (strcmp(name, "init_race") == 0) {
			init_race();
			return;
		}
		for (size_t i = 0; i < sizeof(s_scenarios) / sizeof(s_scenarios[0]); i++) {
			if (s_scenarios[i].main && strcmp(name, s_scenarios[i].name) == 0) {
				ESP_LOGI(TAG, "scenario %s", name);
				eh_host_core_lifecycle_locks_init();
				xTaskCreate(hosted_start_task, "eh_init", 4096, NULL, 23, NULL);
				s_scenarios[i].main();
				return;
			}
		}
		ESP_LOGE(TAG, "unknown scenario '%s'", name);
	}
}
