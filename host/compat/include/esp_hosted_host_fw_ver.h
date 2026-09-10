/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Upstream-mcu compat: esp_hosted host fw version macros.
 * Redirects to eh_common_fw_version.h, which is generated from
 * idf_component.yml by tools/check_fw_versions.py.
 */
#ifndef __ESP_HOSTED_HOST_FW_VERSION_H__
#define __ESP_HOSTED_HOST_FW_VERSION_H__

#include "eh_common_fw_version.h"

#define ESP_HOSTED_VERSION_MAJOR_1      PROJECT_VERSION_MAJOR_1
#define ESP_HOSTED_VERSION_MINOR_1      PROJECT_VERSION_MINOR_1
#define ESP_HOSTED_VERSION_PATCH_1      PROJECT_VERSION_PATCH_1

#define ESP_HOSTED_VERSION_VAL          EH_VERSION_VAL
#define ESP_HOSTED_VERSION_MAJOR        EH_VERSION_MAJOR
#define ESP_HOSTED_VERSION_MINOR        EH_VERSION_MINOR
#define ESP_HOSTED_VERSION_PATCH        EH_VERSION_PATCH
#define ESP_HOSTED_VERSION_PRINTF_ARGS  EH_VERSION_PRINTF_ARGS
#define ESP_HOSTED_VERSION_PRINTF_FMT   EH_VERSION_PRINTF_FMT

#endif
