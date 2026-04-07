/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_hosted.h"
#include "esp_hosted_cp_gpio.h"

static const char* TAG = "gpio_expander_example";

#define SLAVE_GPIO_PIN      2
#define SLAVE_GPIO_ISR_PIN  4   /* Demo 5: bridge this pin to GND to trigger interrupt */

/* GPIO_INTR_ANYEDGE = 3 on ESP32-C6; defined here since driver/gpio.h is slave-side only */
#ifndef GPIO_INTR_ANYEDGE
#define GPIO_INTR_ANYEDGE 3
#endif

static volatile int s_isr_count = 0;

static void gpio_interrupt_cb(int gpio_num, int level, void *arg)
{
	s_isr_count++;
	ESP_LOGI(TAG, "ISR fired! pin=%d level=%d count=%d",
	         gpio_num, level, s_isr_count);
}

static esp_err_t configure_and_test_gpio(esp_hosted_cp_gpio_config_t *config, const char *demo_name)
{
	esp_err_t ret;
	int level;

	ESP_LOGW(TAG, "---- %s ----", demo_name);

	ret = esp_hosted_cp_gpio_config(config);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "GPIO config failed: 0x%x", ret);
		return ret;
	}

	// For output modes, toggle the pin
	if (config->mode == H_CP_GPIO_MODE_OUTPUT ||
			config->mode == H_CP_GPIO_MODE_OUTPUT_OD) {

		ESP_LOGI(TAG, "Setting GPIO %d LOW", SLAVE_GPIO_PIN);
		esp_hosted_cp_gpio_set_level(SLAVE_GPIO_PIN, 0);
		vTaskDelay(pdMS_TO_TICKS(500));

		ESP_LOGI(TAG, "Setting GPIO %d HIGH", SLAVE_GPIO_PIN);
		esp_hosted_cp_gpio_set_level(SLAVE_GPIO_PIN, 1);
		vTaskDelay(pdMS_TO_TICKS(500));

		ESP_LOGI(TAG, "GPIO toggled successfully");
	} else {
		// For input modes, read the level
		ret = esp_hosted_cp_gpio_get_level(SLAVE_GPIO_PIN, &level);
		if (ret == ESP_OK) {
			ESP_LOGI(TAG, "GPIO %d level: %d", SLAVE_GPIO_PIN, level);
		}
	}

	return ESP_OK;
}

void app_main(void)
{
	esp_err_t ret;
	esp_hosted_cp_gpio_config_t gpio_config = {
		.pin_bit_mask = (1ULL << SLAVE_GPIO_PIN),
	};

	// Initialize ESP-Hosted
	ret = esp_hosted_init();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "ESP-Hosted init failed: 0x%x", ret);
		return;
	}

	ret = esp_hosted_connect_to_slave();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Connect to slave failed: %s", esp_err_to_name(ret));
		goto cleanup;
	}

	ESP_LOGI(TAG, "ESP-Hosted initialized successfully");

	// Demo 1: Standard output
	gpio_config.mode = H_CP_GPIO_MODE_OUTPUT;
	gpio_config.pull_up_en = 0;
	gpio_config.pull_down_en = 0;
	if (configure_and_test_gpio(&gpio_config, "Demo 1: Standard Output") != ESP_OK) {
		goto cleanup;
	}

	// Demo 2: Open-drain output with pull-up
	gpio_config.mode = H_CP_GPIO_MODE_OUTPUT_OD;
	gpio_config.pull_up_en = 1;
	gpio_config.pull_down_en = 0;
	if (configure_and_test_gpio(&gpio_config, "Demo 2: Open-Drain Output") != ESP_OK) {
		goto cleanup;
	}

	// Demo 3: Input with pull-up
	gpio_config.mode = H_CP_GPIO_MODE_INPUT;
	gpio_config.pull_up_en = 1;
	gpio_config.pull_down_en = 0;
	if (configure_and_test_gpio(&gpio_config, "Demo 3: Input with Pull-Up") != ESP_OK) {
		goto cleanup;
	}

	// Demo 4: Input with pull-down
	gpio_config.mode = H_CP_GPIO_MODE_INPUT;
	gpio_config.pull_up_en = 0;
	gpio_config.pull_down_en = 1;
	if (configure_and_test_gpio(&gpio_config, "Demo 4: Input with Pull-Down") != ESP_OK) {
		goto cleanup;
	}

	ESP_LOGI(TAG, "All demos completed successfully!");

	// Demo 5: GPIO interrupt forwarding (ISR)
	//
	// Requires host built with CONFIG_ESP_HOSTED_ENABLE_GPIO_EXPANDER_ISR=y.
	// The host sends a consent flag to the slave via GpioConfig RPC.
	//
	// How to trigger: during the 10-second window, briefly connect
	// slave GPIO4 to GND with a jumper wire (or press a button wired to GND).
	// Each edge (falling on connect, rising on release) fires one interrupt.
	//
	// Expected output:
	//   ISR fired! pin=4 level=0 count=1   <- falling edge (GND)
	//   ISR fired! pin=4 level=1 count=2   <- rising edge (release)
	ESP_LOGW(TAG, "---- Demo 5: GPIO Interrupt (ISR forwarding) ----");
	{
		esp_hosted_cp_gpio_config_t isr_cfg = {
			.pin_bit_mask = (1ULL << SLAVE_GPIO_ISR_PIN),
			.mode         = H_CP_GPIO_MODE_INPUT,
			.pull_up_en   = 1,
			.pull_down_en = 0,
			.intr_type    = GPIO_INTR_ANYEDGE,
		};
		ret = esp_hosted_cp_gpio_config(&isr_cfg);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "ISR GPIO config failed: 0x%x (%s) -- is CONFIG_ESP_HOSTED_ENABLE_GPIO_EXPANDER_ISR=y?",
			         ret, esp_err_to_name(ret));
			goto cleanup;
		}

		ret = esp_hosted_cp_gpio_isr_handler_add(SLAVE_GPIO_ISR_PIN, gpio_interrupt_cb, NULL);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "ISR handler add failed: 0x%x", ret);
			goto cleanup;
		}

		ESP_LOGI(TAG, "Waiting 10s for interrupts on slave GPIO%d -- bridge to GND to trigger",
		         SLAVE_GPIO_ISR_PIN);
		for (int i = 0; i < 10; i++) {
			vTaskDelay(pdMS_TO_TICKS(1000));
			ESP_LOGI(TAG, "  [%d/10] interrupts received so far: %d", i + 1, s_isr_count);
		}

		esp_hosted_cp_gpio_isr_handler_remove(SLAVE_GPIO_ISR_PIN);
		ESP_LOGI(TAG, "Demo 5 done. Total interrupts received: %d", s_isr_count);
	}

cleanup:
	esp_hosted_deinit();
}
