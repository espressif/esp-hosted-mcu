/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"

#ifdef CONFIG_EXAMPLE_PEER_DATA_TRANSFER

/**
 * @file example_peer_data_transfer.c
 * @brief Slave-side Custom RPC Example
 *
 * Receives messages from host and echoes back with response message IDs.
 * Demonstrates callback registration for custom message IDs.
 *
 * Request/Response Mapping:
 * - MSG_ID_CAT → MSG_ID_MEOW (echo same data)
 * - MSG_ID_DOG → MSG_ID_WOOF (echo same data)
 * - MSG_ID_HUMAN → MSG_ID_HELLO (echo same data)
 * - MSG_ID_GHOST → No handler (tests max handler limit)
 */

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "slave_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Example Message IDs - use any uint32_t except 0xFFFFFFFF */
#define MSG_ID_CAT      1   /**< Request: small data */
#define MSG_ID_MEOW     2   /**< Response: echo small data */
#define MSG_ID_DOG      3   /**< Request: medium data */
#define MSG_ID_WOOF     4   /**< Response: echo medium data */
#define MSG_ID_HUMAN    5   /**< Request: large data */
#define MSG_ID_HELLO    6   /**< Response: echo large data */
#define MSG_ID_GHOST    99  /**< Test: exceeds max configured handlers */

static const char *TAG = "peer_data_transfer";



/**
 * @brief Callback for MSG_ID_CAT - echo with MEOW response
 */
static void cat_callback(uint32_t msg_id, const uint8_t *data, size_t data_len)
{
    printf("slave <-- host: CAT (%zu bytes)\n", data_len);

    /* Echo back with MSG_ID_MEOW */
    esp_err_t ret = esp_hosted_send_custom_data(MSG_ID_MEOW, data, data_len);
    if (ret == ESP_OK) {
        printf("slave --> host: MEOW (%zu bytes)\n", data_len);
    } else {
        printf("slave --> host: Failed to send MEOW\n");
    }
}

/**
 * @brief Callback for MSG_ID_DOG - echo with WOOF response
 */
static void dog_callback(uint32_t msg_id, const uint8_t *data, size_t data_len)
{
    printf("slave <-- host: DOG (%zu bytes)\n", data_len);

    /* Echo back with MSG_ID_WOOF */
    esp_err_t ret = esp_hosted_send_custom_data(MSG_ID_WOOF, data, data_len);
    if (ret == ESP_OK) {
        printf("slave --> host: WOOF (%zu bytes)\n", data_len);
    } else {
        printf("slave --> host: Failed to send WOOF\n");
    }
}

/**
 * @brief Callback for MSG_ID_HUMAN - echo with HELLO response
 */
static void human_callback(uint32_t msg_id, const uint8_t *data, size_t data_len)
{
    printf("slave <-- host: HUMAN (%zu bytes)\n", data_len);

    /* Echo back with MSG_ID_HELLO */
    esp_err_t ret = esp_hosted_send_custom_data(MSG_ID_HELLO, data, data_len);
    if (ret == ESP_OK) {
        printf("slave --> host: HELLO (%zu bytes)\n", data_len);
    } else {
        printf("slave --> host: Failed to send HELLO\n");
    }
}

/**
 * @brief Initialize Peer Data Transfer Example
 *
 * Registers handlers for all message IDs.
 * Call this from app_main().
 *
 * @return ESP_OK on success
 */
esp_err_t example_peer_data_transfer_init(void)
{
    ESP_LOGI(TAG, "Peer Data Transfer Example: Echo mode");

    /* Register request callbacks */
    esp_err_t ret;

    ret = esp_hosted_register_custom_callback(MSG_ID_CAT, cat_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register CAT callback");
        return ret;
    }

    ret = esp_hosted_register_custom_callback(MSG_ID_DOG, dog_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register DOG callback");
        return ret;
    }

    ret = esp_hosted_register_custom_callback(MSG_ID_HUMAN, human_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register HUMAN callback");
        return ret;
    }

    ESP_LOGI(TAG, "✅ Registered: CAT→MEOW, DOG→WOOF, HUMAN→HELLO");
    return ESP_OK;
}

/*
 * ============================================================================
 * USAGE
 * ============================================================================
 *
 * 1. Enable in menuconfig:
 *    Example Configuration → Additional higher layer examples to run →
 *    Select Examples to run → [*] Peer Data Transfer Example
 *
 * 2. In your app_main():
 *    #include "example_peer_data_transfer.h"
 *    example_peer_data_transfer_init();
 *
 * 3. Send data from host - slave will echo back with response message IDs
 *
 * ============================================================================
 */

#endif /* CONFIG_EXAMPLE_PEER_DATA_TRANSFER */
