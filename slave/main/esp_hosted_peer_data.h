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
 * @brief Send custom data to host
 *
 * @param msg_id Message ID to send (any uint32_t except 0xFFFFFFFF)
 * @param data Data buffer to send
 * @param data_len Length of data
 * @return ESP_OK on success
 */
esp_err_t esp_hosted_send_custom_data(uint32_t msg_id, const uint8_t *data, size_t data_len);

/**
 * @brief Register callback for receiving custom data from host
 *
 * @param msg_id Message ID to listen for (any uint32_t except 0xFFFFFFFF)
 * @param callback Function called when data with this msg_id is received (NULL to deregister)
 * @return ESP_OK on success
 *
 * @note Callback runs in RPC RX thread - keep it fast!
 */
esp_err_t esp_hosted_register_custom_callback(uint32_t msg_id,
    void (*callback)(uint32_t msg_id, const uint8_t *data, size_t data_len));

#ifdef __cplusplus
}
#endif

#endif /* __ESP_HOSTED_PEER_DATA__H */
