/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef __ESP_HOSTED_HOST_FW_VERSION_H__
#define __ESP_HOSTED_HOST_FW_VERSION_H__
#define ESP_HOSTED_VERSION_MAJOR_1 2
#define ESP_HOSTED_VERSION_MINOR_1 6
#define ESP_HOSTED_VERSION_PATCH_1 1

/**
 * Macro to convert version number into an integer
 */
#define ESP_HOSTED_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | (patch))

#endif
