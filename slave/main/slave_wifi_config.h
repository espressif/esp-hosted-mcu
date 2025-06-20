/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __SLAVE_WIFI_CONFIG_H__
#define __SLAVE_WIFI_CONFIG_H__

#include "esp_idf_version.h"

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 3, 1)
#error ESP-IDF version used is not supported
#endif

/* ESP-IDF 5.5.0: renamed reserved fields to reserved1/reserved2 */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
#define H_WIFI_NEW_RESERVED_FIELD_NAMES 1
#define H_PRESENT_IN_ESP_IDF_5_5_0      1
#else
#define H_WIFI_NEW_RESERVED_FIELD_NAMES 0
#define H_PRESENT_IN_ESP_IDF_5_5_0      0
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
#define H_PRESENT_IN_ESP_IDF_5_4_0      1
#else
#define H_PRESENT_IN_ESP_IDF_5_4_0      0
#endif

/* wifi_ap_config_t::transition_disable only found in
 * IDF v5.3.3 and above, or
 * IDF v5.4.1 and above
 */
#if (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 3, 3)) || (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 4, 1))
#define H_GOT_AP_CONFIG_PARAM_TRANSITION_DISABLE 0
#else
#define H_GOT_AP_CONFIG_PARAM_TRANSITION_DISABLE 1
#endif

#endif
