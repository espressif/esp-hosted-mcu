/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Minimal esp_err.h for building common/ code on the build machine. */

#pragma once

typedef int esp_err_t;

#define ESP_OK                0
#define ESP_FAIL              -1
#define ESP_ERR_INVALID_ARG   0x102
#define ESP_ERR_INVALID_STATE 0x103
