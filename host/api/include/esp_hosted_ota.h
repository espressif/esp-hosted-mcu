/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* APIs to do OTA updates of the co-processor
 *
 * Note: This API is platform dependent
 *
 * Add additional APIs as required based on how the OTA binary is to
 * be fetched.
 *
 * Source for the API should be in host/port/<platform>/...
 *
 * Procedure used by APIs to do OTA update:
 * 1. Fetch and prepare OTA binary
 * 2. Call rpc_ota_begin() to start OTA
 * 3. Repeatedly call rpc_ota_write() with a continuous chunk of OTA data
 * 4. Call rpc_ota_end()
 *
 */

#ifndef __ESP_HOSTED_OTA_H__
#define __ESP_HOSTED_OTA_H__

#ifdef ESP_PLATFORM
// OTA API for ESP-IDF
#include "esp_err.h"

// TODO: Make these configurable in sdkconfig
#define OTA_CHUNK_SIZE                                       2048
#define OTA_CHUNK_DELAY_MS                                   10

#if OTA_FROM_WEB_URL
/* Fetch OTA image from a web server per HTTP (image_url) */
esp_err_t esp_hosted_slave_ota_http(const char* image_url);
/* Fetch OTA image from a web server (image_url) per HTTPS */
esp_err_t esp_hosted_slave_ota_https(const char* image_url, const char* https_cert);
#else
/* Fetch OTA image from a file (image_url) */
esp_err_t esp_hosted_slave_ota_image_file(const char* image_url);
#endif

#endif /*__ESP_HOSTED_OTA_H__*/
