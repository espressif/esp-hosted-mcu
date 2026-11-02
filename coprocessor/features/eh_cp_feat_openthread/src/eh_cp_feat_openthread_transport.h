/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>

#include "esp_err.h"

/* Spinel over ESP_AUX_OT, for esp_openthread_transport_config_t. */
esp_err_t eh_cp_feat_openthread_transport_init(void);
void      eh_cp_feat_openthread_transport_deinit(void);
esp_err_t eh_cp_feat_openthread_transport_tx(const void *buf, size_t len);
esp_err_t eh_cp_feat_openthread_transport_rx(void *buf, size_t buf_size, size_t *out_len);
