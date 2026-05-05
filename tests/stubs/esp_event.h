/* Stub for Linux mock build */
#ifndef ESP_EVENT_H
#define ESP_EVENT_H

#include <stdint.h>

typedef const char* esp_event_base_t;

#define ESP_EVENT_DECLARE_BASE(id) extern esp_event_base_t id
#define ESP_EVENT_DEFINE_BASE(id)  esp_event_base_t id = #id

#endif
