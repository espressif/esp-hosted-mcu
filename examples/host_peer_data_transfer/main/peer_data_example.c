/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file peer_data_example.c
 * @brief Host-side Peer Data Transfer Example - Send/Receive Raw Data
 *
 * This demonstrates ultra-simple custom RPC:
 * - Send raw bytes to slave
 * - Receive raw bytes from slave (echoed back if loopback enabled)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "esp_hosted.h"

static const char *TAG = "peer_data_example";

/* Maximum payload size for custom RPC (empirically determined) */
#define PEER_DATA_MAX_PAYLOAD_SIZE  8166

/* Statistics tracking */
static uint32_t total_sent = 0;
static uint32_t total_received = 0;
static uint32_t total_bytes_sent = 0;
static uint32_t total_bytes_received = 0;
static uint32_t data_mismatch_count = 0;

/* Expected data for validation */
typedef struct {
    uint32_t size;
    uint8_t first_bytes[16];
} expected_packet_t;

/**
 * @brief Validate received data matches what was sent
 */
static bool validate_received_data(const uint8_t *data, size_t data_len)
{
    /* For very small packets (< 4 bytes), just verify pattern */
    if (data_len < 4) {
        for (size_t i = 0; i < data_len; i++) {
            uint8_t expected = (i & 0xFF);
            if (data[i] != expected) {
                ESP_LOGE(TAG, "   ❌ Data mismatch at offset %zu: expected 0x%02x, got 0x%02x", 
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
        ESP_LOGE(TAG, "   ❌ Size mismatch: expected %lu, got %zu", reported_size, data_len);
        return false;
    }

    /* Verify pattern: sequential bytes starting from offset 4 */
    for (size_t i = 4; i < data_len; i++) {
        uint8_t expected = (i & 0xFF);
        if (data[i] != expected) {
            ESP_LOGE(TAG, "   ❌ Data mismatch at offset %zu: expected 0x%02x, got 0x%02x", 
                     i, expected, data[i]);
            return false;
        }
    }

    return true;
}

/**
 * @brief Callback for receiving custom data from slave
 *
 * This is called when slave sends data back (e.g., in loopback mode).
 */
static void custom_data_rx_callback(const uint8_t *data, size_t data_len)
{
    total_received++;
    total_bytes_received += data_len;

    printf("copro --> host : %zu byte stream received, ", data_len);
    
    /* Validate data */
    if (validate_received_data(data, data_len)) {
        printf("verification: ✅\n");
    } else {
        data_mismatch_count++;
        printf("verification: ❌\n");
    }
}

/**
 * @brief Send data with automatic size checking
 * 
 * Rejects packets larger than PEER_DATA_MAX_PAYLOAD_SIZE.
 * For larger data, user should implement fragmentation at application level.
 */
static esp_err_t send_custom_data_checked(const uint8_t *data, uint32_t size)
{
    if (size > PEER_DATA_MAX_PAYLOAD_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    return esp_hosted_send_custom_data((uint8_t *)data, size);
}

/**
 * @brief Task to test different packet sizes with data validation
 */
static void peer_data_sender_task(void *pvParameters)
{
    /* Test various packet sizes up to maximum */
    const uint32_t test_sizes[] = {
        1,
        10,
        64,
        128,
        256,
        512,
        1024,
        2048,
        4096,
        8166,   /* Maximum working size */
        8200,   /* Should fail (over limit) */
    };

    printf("\n");
    printf("========================================\n");
    printf("Peer Data Transfer Test (max: %d bytes)\n", PEER_DATA_MAX_PAYLOAD_SIZE);
    printf("========================================\n");

    vTaskDelay(pdMS_TO_TICKS(1000));

    for (int i = 0; i < sizeof(test_sizes)/sizeof(test_sizes[0]); i++) {
        uint32_t size = test_sizes[i];
        
        uint8_t *test_data = (uint8_t *)malloc(size);
        if (!test_data) {
            ESP_LOGE(TAG, "❌ Failed to allocate %lu bytes", size);
            continue;
        }

        /* Fill with pattern */
        if (size < 4) {
            /* For small packets: just sequential bytes */
            for (uint32_t j = 0; j < size; j++) {
                test_data[j] = (j & 0xFF);
            }
        } else {
            /* For larger packets: size(4 bytes) + sequential data */
            test_data[0] = (size >> 24) & 0xFF;
            test_data[1] = (size >> 16) & 0xFF;
            test_data[2] = (size >> 8) & 0xFF;
            test_data[3] = size & 0xFF;
            for (uint32_t j = 4; j < size; j++) {
                test_data[j] = (j & 0xFF);
            }
        }

        printf("copro <-- host : %lu byte stream, ", size);

        esp_err_t ret = send_custom_data_checked(test_data, size);

        if (ret == ESP_OK) {
            total_sent++;
            total_bytes_sent += size;
            printf("sent ✅\n");
        } else if (ret == ESP_ERR_INVALID_SIZE) {
            ESP_LOGW(TAG, "   Packet too large: %lu bytes (max: %d)",  size, PEER_DATA_MAX_PAYLOAD_SIZE);
            ESP_LOGI(TAG, "   Implement app level fragmentation for packets > %d bytes", PEER_DATA_MAX_PAYLOAD_SIZE);
        } else {
            printf("failed ❌\n");
        }

        free(test_data);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

    /* Wait for any remaining responses */
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* Print statistics */
    printf("\n");
    printf("========================================\n");
    printf("TEST SUMMARY\n");
    printf("========================================\n");
    printf("Max payload:      %d bytes\n", PEER_DATA_MAX_PAYLOAD_SIZE);
    printf("Packets sent:     %lu\n", total_sent);
    printf("Packets received: %lu\n", total_received);
    printf("Bytes sent:       %lu\n", total_bytes_sent);
    printf("Bytes received:   %lu\n", total_bytes_received);
    
    if (total_sent == total_received && total_bytes_sent == total_bytes_received && data_mismatch_count == 0) {
        printf("Result:           ✅ PASS\n");
    } else {
        printf("Result:           ❌ FAIL\n");
        if (data_mismatch_count > 0) {
            printf("  Validation errors: %lu\n", data_mismatch_count);
        }
    }
    printf("========================================\n");

    vTaskDelete(NULL);
}

void app_main(void)
{
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize ESP-Hosted */
    ret = esp_hosted_init();
    if (ret != ESP_OK) {
        printf("ESP-Hosted init failed: %s\n", esp_err_to_name(ret));
        return;
    }

    ret = esp_hosted_connect_to_slave();
    if (ret != ESP_OK) {
        printf("Connect to slave failed: %s\n", esp_err_to_name(ret));
        return;
    }

    /* Register callback for receiving custom data from slave */
    ret = esp_hosted_register_rx_callback_custom_data(custom_data_rx_callback);
    if (ret != ESP_OK) {
        printf("Register callback failed: %s\n", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "✅ copro --> host (custom_data_rx_callback) Callback registered");

    /* Create task to send data to slave */
    xTaskCreate(peer_data_sender_task, "peer_data_sender", 8192, NULL, 5, NULL);
}
