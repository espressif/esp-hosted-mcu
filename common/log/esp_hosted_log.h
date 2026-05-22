/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ESP_HOSTED_LOG_H
#define __ESP_HOSTED_LOG_H

#ifdef ESP_PLATFORM
#include "esp_log.h"
#else
#include "h_wrapper.h"
#include <stdio.h>

/* Map ESP-style log macros to universal H_LOG macros */
#define ESP_LOGE(tag, fmt, ...) H_LOGE(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) H_LOGW(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) H_LOGI(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) H_LOGD(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) H_LOGV(tag, fmt, ##__VA_ARGS__)

#define ESP_LOG_BUFFER_HEXDUMP(tag, buf, len, level) do {     (void)level;     H_HEXLOGD(tag, buf, len, 16); } while(0)

#ifndef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL 3
#endif
#define ESP_LOG_ERROR   1
#define ESP_LOG_WARN    2
#define ESP_LOG_INFO    3
#define ESP_LOG_DEBUG   4
#define ESP_LOG_VERBOSE 5
#endif

#define ESP_PRIV_HEXDUMP(tag1, tag2, buff, buf_len, display_len, curr_level)  \
  if ( 3 >= curr_level) {                                       \
    int len_to_print = 0;                                                     \
    len_to_print = display_len<buf_len? display_len: buf_len;                 \
    ESP_LOG_I(tag1, "%s: buf_len[%d], print_len[%d]",   \
        tag2, (int)buf_len, (int)len_to_print);                               \
    ESP_LOG_BUFFER_HEXDUMP(tag2, buff, len_to_print, curr_level);             \
  }

#define ESP_HEXLOGE(tag2, buff, buf_len, display_len) ESP_LOGE(tag2, "HEXDUMP", "...")
#define ESP_HEXLOGW(tag2, buff, buf_len, display_len) ESP_LOGW(tag2, "HEXDUMP", "...")
#define ESP_HEXLOGI(tag2, buff, buf_len, display_len) ESP_LOGI(tag2, "HEXDUMP", "...")
#define ESP_HEXLOGD(tag2, buff, buf_len, display_len) ESP_LOGD(tag2, "HEXDUMP", "...")
#define ESP_HEXLOGV(tag2, buff, buf_len, display_len) ESP_LOGV(tag2, "HEXDUMP", "...")

#endif
