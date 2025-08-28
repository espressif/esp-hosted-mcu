/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_hosted.h"

static const char* TAG = "gpio_expander_host_example";

// GPIO pin on the slave to be controlled
#define SLAVE_GPIO_PIN 2

void app_main(void)
{
    esp_err_t ret = ESP_OK;

    // Initialize ESP-Hosted transport
    ret = esp_hosted_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-Hosted transport initialization failed: 0x%x", ret);
        return;
    }

	ret = esp_hosted_connect_to_slave();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Connect to slave failed: %s", esp_err_to_name(ret));
		return;
	}
    ESP_LOGI(TAG, "ESP-Hosted transport initialized successfully.");

    // --- Step 1: Configure GPIO as an output ---
    ESP_LOGI(TAG, "Configuring slave GPIO %d as output", SLAVE_GPIO_PIN);
    esp_hosted_cp_gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << SLAVE_GPIO_PIN),
        .mode = 2, // Corresponds to GPIO_MODE_OUTPUT
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = 0 // Corresponds to GPIO_INTR_DISABLE
    };

    ret = esp_hosted_cp_gpio_config(&output_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d as output: 0x%x", SLAVE_GPIO_PIN, ret);
        ESP_LOGE(TAG, "Check if slave has enabled `CONFIG_ESP_HOSTED_ENABLE_GPIO_EXPANDER`", SLAVE_GPIO_PIN, ret);
        goto cleanup;
    }
    ESP_LOGI(TAG, "GPIO %d configured as output.", SLAVE_GPIO_PIN);

    // --- Step 2: Toggle the GPIO output ---
    for (int i = 0; i < 5; i++) {
        ESP_LOGI(TAG, "Setting GPIO %d HIGH", SLAVE_GPIO_PIN);
        ret = esp_hosted_cp_gpio_set_level(SLAVE_GPIO_PIN, 1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set GPIO level to HIGH: 0x%x", ret);
            goto cleanup;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "Setting GPIO %d LOW", SLAVE_GPIO_PIN);
        ret = esp_hosted_cp_gpio_set_level(SLAVE_GPIO_PIN, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set GPIO level to LOW: 0x%x", ret);
            goto cleanup;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // --- Step 3: Configure GPIO as an input ---
    ESP_LOGI(TAG, "Configuring slave GPIO %d as input with pull-up", SLAVE_GPIO_PIN);
    esp_hosted_cp_gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << SLAVE_GPIO_PIN),
        .mode = 1, // Corresponds to GPIO_MODE_INPUT
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = 0
    };

    ret = esp_hosted_cp_gpio_config(&input_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d as input: 0x%x", SLAVE_GPIO_PIN, ret);
        goto cleanup;
    }
    ESP_LOGI(TAG, "GPIO %d configured as input.", SLAVE_GPIO_PIN);
    vTaskDelay(pdMS_TO_TICKS(100)); // Small delay for pull-up to take effect

    // --- Step 4: Read the GPIO input level ---
    int level = esp_hosted_cp_gpio_get_level(SLAVE_GPIO_PIN);
    if (level < 0) {
        ESP_LOGE(TAG, "Failed to get GPIO level");
    } else {
        ESP_LOGI(TAG, "GPIO %d level is: %d", SLAVE_GPIO_PIN, level);
        if (level == 1) {
            ESP_LOGI(TAG, "Success! Input level is HIGH as expected due to pull-up.");
        } else {
            ESP_LOGW(TAG, "Input level is not HIGH. Check for external pull-downs or shorts.");
        }
    }

cleanup:
    // De-initialize ESP-Hosted transport
    esp_hosted_deinit();
    ESP_LOGI(TAG, "Example finished.");
}
