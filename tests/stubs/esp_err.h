/* Stub for Linux mock build */
#ifndef ESP_ERR_H
#define ESP_ERR_H

#include <stdint.h>

typedef int esp_err_t;

#define ESP_OK               0
#define ESP_FAIL             -1
#define ESP_ERR_NO_MEM       0x101
#define ESP_ERR_INVALID_ARG  0x102
#define ESP_ERR_NOT_ALLOWED  0x103
#define ESP_ERR_INVALID_STATE 0x104

#endif
