/* Stub for Linux mock build */
#ifndef ESP_IDF_VERSION_H
#define ESP_IDF_VERSION_H

#define ESP_IDF_VERSION_MAJOR 5
#define ESP_IDF_VERSION_MINOR 3
#define ESP_IDF_VERSION_PATCH 0

#define ESP_IDF_VERSION_VAL(major, minor, patch) \
    ((major << 16) | (minor << 8) | (patch))

#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(5, 3, 0)

#endif
