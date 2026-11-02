/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#pragma once

#include "esp_err.h"

/* Call after esp_openthread_start(): the Thread status task reads the
 * OpenThread instance. */
esp_err_t app_demo_start(void);

/* Call after the console starts. */
esp_err_t app_demo_register_commands(void);
