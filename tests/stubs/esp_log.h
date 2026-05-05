/* Stub for Linux mock build */
#ifndef ESP_LOG_H
#define ESP_LOG_H

#include <stdio.h>

typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE,
} esp_log_level_t;

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#define ESP_LOG_LEVEL_LOCAL(level, tag, format, ...) /* noop */
#define ESP_LOGI(tag, format, ...) printf("[%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) printf("[%s] WARN: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, format, ...) printf("[%s] ERROR: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) /* noop */
#define ESP_LOGV(tag, format, ...) /* noop */
#define ESP_EARLY_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)

#define ESP_LOG_BUFFER_HEXDUMP(tag, buffer, buff_len, level) /* noop */

#endif
