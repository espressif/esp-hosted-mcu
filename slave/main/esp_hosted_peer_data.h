/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ESP_HOSTED_PEER_DATA__H
#define __ESP_HOSTED_PEER_DATA__H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register callback for receiving custom data from host
 *
 * @param callback Function called when data arrives from host
 * @return ESP_OK on success
 *
 * @note Callback runs in RPC RX thread - keep it fast!
 */
esp_err_t esp_hosted_register_rx_callback_custom_data(void (*callback)(const uint8_t *data, size_t data_len));

/**
 * @brief Send custom data to host
 *
 * @param data Pointer to data (user owns memory)
 * @param data_len Length of data
 * @return ESP_OK on success
 */
esp_err_t esp_hosted_send_custom_data(uint8_t *data, uint32_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_HOSTED_PEER_DATA__H */
