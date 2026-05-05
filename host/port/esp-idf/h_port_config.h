/* host/port/esp-idf/h_port_config.h
 * Platform configuration for ESP-IDF — maps Kconfig options to host port defines. */

#ifndef H_PORT_CONFIG_ESPIDF_H
#define H_PORT_CONFIG_ESPIDF_H

#include "esp_idf_version.h"

/* ── Transport — selected by Kconfig at build time ── */
#if defined(CONFIG_ESP_HOSTED_SPI_HOST_INTERFACE)
  #define H_TRANSPORT_IN_USE  H_TRANSPORT_SPI
#elif defined(CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE)
  #define H_TRANSPORT_IN_USE  H_TRANSPORT_SDIO
#elif defined(CONFIG_ESP_HOSTED_UART_HOST_INTERFACE)
  #define H_TRANSPORT_IN_USE  H_TRANSPORT_UART
#else
  #error "No ESP-Hosted transport selected in Kconfig"
#endif

/* ── Platform Identity ── */
#define H_PORT_NAME         "esp-idf"
#define H_PORT_VERSION      "5.3.2"
#define H_PORT_RTOS         "freertos"
#define H_PORT_RTOS_VER     "10.5.1"
#define H_PORT_CHIP         CONFIG_IDF_TARGET
#define H_PORT_BUILD_DATE   __DATE__

/* ── IDF version ── */
#define H_IDF_VERSION_MAJOR 5
#define H_IDF_VERSION_MINOR 3

/* ESP-IDF uses static netif creation (esp_netif_create_default_wifi_*) */
#define H_HOST_USES_STATIC_NETIF 1

/* ── Thread config from Kconfig ── */
#if defined(CONFIG_ESP_HOSTED_DFLT_TASK_STACK)
  #define H_DEFAULT_TASK_STACK  CONFIG_ESP_HOSTED_DFLT_TASK_STACK
#endif
#if defined(CONFIG_ESP_HOSTED_DFLT_TASK_PRIO)
  #define H_DEFAULT_TASK_PRIO   CONFIG_ESP_HOSTED_DFLT_TASK_PRIO
#endif

/* ── Phase 2 feature flags — explicitly 0 for Phase 1 ── */
#define H_FEATURE_BLUETOOTH  0
#define H_FEATURE_OTA        0
#define H_FEATURE_NETSPLIT   0

/* ── ESP-IDF version-gated Wi-Fi features (replacing
 *   port_esp_hosted_host_wifi_config.h in core layer code) ── */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
  #define H_PRESENT_IN_ESP_IDF_5_4_0      1
#else
  #define H_PRESENT_IN_ESP_IDF_5_4_0      0
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
  #define H_WIFI_NEW_RESERVED_FIELD_NAMES 1
  #define H_PRESENT_IN_ESP_IDF_5_5_0      1
#else
  #define H_WIFI_NEW_RESERVED_FIELD_NAMES 0
  #define H_PRESENT_IN_ESP_IDF_5_5_0      0
#endif

#ifdef CONFIG_ESP_HOSTED_DECODE_WIFI_RESERVED_FIELD
  #define H_DECODE_WIFI_RESERVED_FIELD 1
#else
  #define H_DECODE_WIFI_RESERVED_FIELD 0
#endif

#endif /* H_PORT_CONFIG_ESPIDF_H */
