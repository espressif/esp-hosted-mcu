/* host/port/include/h_port_config.h
 * ESP-IDF port configuration — bridges Kconfig options to h_port macros.
 *
 * H_TRANSPORT_IN_USE is derived from ESP-IDF sdkconfig so the core layer
 * contract validation and conditional compilation stay in sync with the
 * user's menuconfig selection. */
#ifndef H_PORT_CONFIG_H
#define H_PORT_CONFIG_H

/* When sdkconfig.h is available (ESP-IDF builds), use Kconfig-derived config.
 * Otherwise fall back to the Linux mock port config. */
#if __has_include("sdkconfig.h")
#include <sdkconfig.h>
#endif

/* ── Transport selection from Kconfig ── */
#if defined(CONFIG_ESP_HOSTED_SPI_HOST_INTERFACE)
#define H_TRANSPORT_IN_USE  H_TRANSPORT_SPI
#elif defined(CONFIG_ESP_HOSTED_SPI_HD_HOST_INTERFACE)
#define H_TRANSPORT_IN_USE  H_TRANSPORT_SPI_HD
#elif defined(CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE)
#define H_TRANSPORT_IN_USE  H_TRANSPORT_SDIO
#elif defined(CONFIG_ESP_HOSTED_UART_HOST_INTERFACE)
#define H_TRANSPORT_IN_USE  H_TRANSPORT_UART
#elif !defined(H_TRANSPORT_IN_USE)
/* No transport selected via Kconfig and H_TRANSPORT_IN_USE not yet defined.
 * Fall back to Linux mock port config. */
#include "../linux/h_port_config.h"
#define H_PORT_USING_LINUX_FALLBACK
#endif

#ifndef H_PORT_USING_LINUX_FALLBACK

/* Platform identity (for diagnostic logging) */
#define H_PORT_NAME         "esp-idf"
#define H_PORT_VERSION      IDF_VER
#define H_PORT_RTOS         "FreeRTOS"
#define H_PORT_RTOS_VER     "10.5.1"
#define H_PORT_CHIP         CONFIG_IDF_TARGET
#define H_PORT_BUILD_DATE   __DATE__

/* Port-specific thread tuning (override H_DEFAULT_*) */
/* #define H_DEFAULT_TASK_STACK  8192 */
/* #define H_DEFAULT_TASK_PRIO   10 */

/* Phase 2 feature flags — explicitly 0 for Phase 1 */
#define H_FEATURE_BLUETOOTH  0
#define H_FEATURE_OTA        0
#define H_FEATURE_NETSPLIT   0

#undef H_HOST_USES_STATIC_NETIF
#define H_HOST_USES_STATIC_NETIF 1

#endif /* !H_PORT_USING_LINUX_FALLBACK */

#endif /* H_PORT_CONFIG_H */
