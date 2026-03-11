/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __H_SLAVE_EXT_COEX_H__
#define __H_SLAVE_EXT_COEX_H__

#include <stdbool.h>
#include "sdkconfig.h"
#include "esp_err.h"

#include "esp_hosted_rpc.pb-c.h"

#ifdef CONFIG_ESP_HOSTED_CP_EXT_COEX
    #ifdef CONFIG_BT_ENABLED
        #error "External Coexistence RPC handlers are enabled but Bluetooth is also enabled. For proper external coexistence, CONFIG_BT_ENABLED should be disabled."
    #endif
    #ifdef CONFIG_IDF_TARGET_ESP32
        #error "External Coexistence RPC handlers are enabled but the target is ESP32. For proper external coexistence, the ESP-IDF target should not be ESP32."
    #endif
    #ifndef CONFIG_ESP_COEX_EXTERNAL_COEXIST_ENABLE
        #error "External Coexistence RPC handlers are enabled but CONFIG_ESP_COEX_EXTERNAL_COEXIST_ENABLE is disabled. For proper external coexistence, it should be enabled (Component config -> Wi-Fi -> External coexistence)."
    #endif
	#ifndef CONFIG_ESP_COEX_ENABLED
		#error "External Coexistence RPC handlers are enabled but CONFIG_ESP_COEX_ENABLED is disabled, Incompatible config/slave"
	#endif

#endif

#if defined(CONFIG_ESP_HOSTED_CP_EXT_COEX)
	#define H_EXT_COEX_SUPPORT (1)
	esp_err_t req_ext_coex(Rpc *req, Rpc *resp, void *priv_data);
#else
	#define H_EXT_COEX_SUPPORT (0)
#endif

#endif
