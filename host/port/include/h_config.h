/* host/port/include/h_config.h */
#ifndef H_CONFIG_H
#define H_CONFIG_H

/* ── Transport Selection ── */
#ifndef H_TRANSPORT_NONE
#define H_TRANSPORT_NONE    0
#endif
#ifndef H_TRANSPORT_SDIO
#define H_TRANSPORT_SDIO    1
#endif
#ifndef H_TRANSPORT_SPI_HD
#define H_TRANSPORT_SPI_HD  2
#endif
#ifndef H_TRANSPORT_SPI
#define H_TRANSPORT_SPI     3
#endif
#ifndef H_TRANSPORT_UART
#define H_TRANSPORT_UART    4
#endif

/* ── Port-Specific Overrides ── */
#include "h_port_config.h"

#ifndef H_TRANSPORT_IN_USE
#error "H_TRANSPORT_IN_USE must be defined by port layer (h_port_config.h)"
#endif

/* ── Default Thread Config ──
 * Overridden by each port's h_port_config.h if needed. */
#ifndef H_DEFAULT_TASK_STACK
#define H_DEFAULT_TASK_STACK  4096
#endif
#ifndef H_DEFAULT_TASK_PRIO
#define H_DEFAULT_TASK_PRIO   5
#endif

/* ── Feature Flags (Phase 1) ──
 * Disabled by default; enabled per-feature in future phases. */
#ifndef H_FEATURE_DPP
#define H_FEATURE_DPP 0
#endif
#ifndef H_FEATURE_ENTERPRISE
#define H_FEATURE_ENTERPRISE 0
#endif

/* ── ESP-IDF Version-Gated Wi-Fi Features ──
 * Port layer overrides via h_port_config.h.  When a port does not
 * override, the default is "feature absent / version not present". */
#ifndef H_PRESENT_IN_ESP_IDF_5_4_0
#define H_PRESENT_IN_ESP_IDF_5_4_0  0
#endif
#ifndef H_PRESENT_IN_ESP_IDF_5_5_0
#define H_PRESENT_IN_ESP_IDF_5_5_0  0
#endif
#ifndef H_DECODE_WIFI_RESERVED_FIELD
#define H_DECODE_WIFI_RESERVED_FIELD  0
#endif
#ifndef H_WIFI_NEW_RESERVED_FIELD_NAMES
#define H_WIFI_NEW_RESERVED_FIELD_NAMES  0
#endif

/* ── RPC Queue Limits ──
 * Port layer can override these via h_port_config.h */
#ifndef H_MAX_SYNC_RPC_REQUESTS
#define H_MAX_SYNC_RPC_REQUESTS  5
#endif
#ifndef H_MAX_ASYNC_RPC_REQUESTS
#define H_MAX_ASYNC_RPC_REQUESTS 5
#endif

#endif /* H_CONFIG_H */
