/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

#include "eh_host_openthread_transport.h"

/* Spinel channel. eh_host_openthread_rcp_start() and _stop() own it. */
esp_err_t eh_host_openthread_transport_init(void);
void      eh_host_openthread_transport_deinit(void);
