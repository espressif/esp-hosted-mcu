/* host/port/include/h_port_config.h (TEMPLATE)
 * Each port copy/pastes this and fills in platform-specific values.
 * This template file is NOT compiled — it serves as documentation. */
#ifndef H_PORT_CONFIG_H
#define H_PORT_CONFIG_H

/* MUST define transport in use */
#define H_TRANSPORT_IN_USE  H_TRANSPORT_SPI

/* Platform identity (for diagnostic logging) */
#define H_PORT_NAME         "unknown"
#define H_PORT_VERSION      "0.0.0"
#define H_PORT_RTOS         "none"
#define H_PORT_RTOS_VER     "0.0.0"
#define H_PORT_CHIP         "unknown"
#define H_PORT_BUILD_DATE   __DATE__

/* Port-specific thread tuning (override H_DEFAULT_*) */
/* #define H_DEFAULT_TASK_STACK  8192 */
/* #define H_DEFAULT_TASK_PRIO   10 */

/* Phase 2 feature flags — explicitly 0 for Phase 1 */
#define H_FEATURE_BLUETOOTH  0
#define H_FEATURE_OTA        0
#define H_FEATURE_NETSPLIT   0

#endif /* H_PORT_CONFIG_H */
