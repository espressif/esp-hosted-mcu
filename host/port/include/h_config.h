/* host/port/include/h_config.h */
#ifndef H_CONFIG_H
#define H_CONFIG_H

/* ── Transport Selection ── */
#define H_TRANSPORT_SPI     1
#define H_TRANSPORT_SDIO    2
#define H_TRANSPORT_SPI_HD  3
#define H_TRANSPORT_UART    4

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

#endif /* H_CONFIG_H */
