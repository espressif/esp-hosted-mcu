/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file example_peer_data_transfer.c
 * @brief Peer Data Transfer Demo for Coprocessor - Echo with Verification
 *
 * This demonstrates peer data transfer:
 * - Receive raw bytes from host
 * - Validate received data
 * - Echo back (if echo mode enabled)
 * - Track statistics
 */

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_hosted_peer_data.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "peer_data_transfer";

#ifdef CONFIG_EXAMPLE_PEER_DATA_TRANSFER_ECHO
/* Statistics tracking */
static uint32_t total_received = 0;
static uint32_t total_bytes_received = 0;
static uint32_t data_mismatch_count = 0;

/**
 * @brief Validate received data matches expected pattern
 */
static bool validate_received_data(const uint8_t *data, size_t data_len)
{
    /* For very small packets (< 4 bytes), just verify pattern */
    if (data_len < 4) {
        for (size_t i = 0; i < data_len; i++) {
            uint8_t expected = (i & 0xFF);
            if (data[i] != expected) {
                ESP_LOGE(TAG, "   Data mismatch at offset %zu: expected 0x%02x, got 0x%02x",
                         i, expected, data[i]);
                return false;
            }
        }
        return true;
    }

    /* Extract size from first 4 bytes */
    uint32_t reported_size = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];

    /* Check size matches */
    if (reported_size != data_len) {
        ESP_LOGE(TAG, "   Size mismatch: expected %lu, got %zu", reported_size, data_len);
        return false;
    }

    /* Verify pattern: sequential bytes starting from offset 4 */
    for (size_t i = 4; i < data_len; i++) {
        uint8_t expected = (i & 0xFF);
        if (data[i] != expected) {
            ESP_LOGE(TAG, "   Data mismatch at offset %zu: expected 0x%02x, got 0x%02x",
                     i, expected, data[i]);
            return false;
        }
    }

    return true;
}

/**
 * @brief Print accumulated statistics
 */
static void print_statistics(void)
{
    printf("\n");
    printf("========================================\n");
    printf("SLAVE-SIDE VERIFICATION\n");
    printf("========================================\n");
    printf("Packets received: %lu\n", total_received);
    printf("Bytes received:   %lu\n", total_bytes_received);

    if (data_mismatch_count == 0) {
        printf("Data validation:  ✅ ALL PASSED\n");
    } else {
        printf("Data validation:  ❌ %lu FAILURES\n", data_mismatch_count);
    }
    printf("========================================\n");
}
#endif

/**
 * @brief Callback for receiving custom data from host
 *
 * Validates and echoes back received data.
 *
 * @param data Pointer to received data
 * @param data_len Length of received data
 *
 * @note Keep this FAST - runs in RPC RX thread!
 */
static void example_custom_data_rx_callback(const uint8_t *data, size_t data_len)
{
	ESP_LOGD(TAG, "packet len with %zu received", data_len);
#ifdef CONFIG_EXAMPLE_PEER_DATA_TRANSFER_ECHO
    total_received++;
    total_bytes_received += data_len;

    printf("host --> copro : %zu byte stream received, ", data_len);

    /* Validate data */
    if (validate_received_data(data, data_len)) {
        printf("verification: ✅\n");
    } else {
        data_mismatch_count++;
        printf("verification: ❌\n");
    }

    /* Loopback: Send received data back to host */
    esp_err_t ret = esp_hosted_send_custom_data((uint8_t *)data, data_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to loop back %zu bytes", data_len);
    }

    /* Print statistics after expected number of packets (10 normal + 1 oversized attempt) */
    if (total_received == 10) {
        vTaskDelay(pdMS_TO_TICKS(500)); /* Small delay for last response to be sent */
        print_statistics();
    }
#else
    ESP_LOGI(TAG, "Received %zu bytes (loopback disabled)", data_len);
#endif
}

/**
 * @brief Initialize Custom RPC Demo
 *
 * Registers the simple loopback handler.
 * Call this from app_main().
 *
 * @return ESP_OK on success
 */
esp_err_t example_peer_data_transfer_init(void)
{
#ifdef CONFIG_EXAMPLE_PEER_DATA_TRANSFER_ECHO
    ESP_LOGI(TAG, "Peer data transfer: ECHO mode enabled");
#else
    ESP_LOGI(TAG, "Peer data transfer: RECEIVE only (echo disabled)");
#endif

    /* Register callback for receiving custom data from host */
    esp_err_t ret = esp_hosted_register_rx_callback_custom_data(example_custom_data_rx_callback);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to register callback");
        return ret;
    }

    return ESP_OK;
}

/*
 * ============================================================================
 * USAGE
 * ============================================================================
 *
 * 1. Enable in menuconfig:
 *    Component config → ESP-Hosted config → [*] Enable Custom RPC support
 *    Component config → ESP-Hosted config → [*] Loopback custom RPC messages
 *
 * 2. In your app_main():
 *    #include "example_peer_data_transfer.h"
 *    example_peer_data_transfer_init();
 *
 * 3. Send data from host - it will be echoed back if loopback is enabled
 *
 * ============================================================================
 */
