/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "eh_host_mcu_transport.h"
#include "eh_host_port_master_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if EH_HOST_PKT_STATS

/* Enables the `#if ESP_PKT_STATS` counter sites in the bus backend. */
#define ESP_PKT_STATS 1

/* Datapath counters, dumped periodically. Plain u32 increments (debug-only,
 * torn reads acceptable) — mirrors the CP's pkt_stats. */
struct pkt_stats_t {
	uint32_t sta_tx_in_pass;    /* accepted by bus_tx */
	uint32_t sta_tx_trans_in;   /* entered the bus write path */
	uint32_t sta_tx_out;        /* sent to the CP */
	uint32_t sta_tx_out_drop;   /* dropped in the write path */
	uint32_t sta_rx_in;         /* frames read from the CP */
	uint32_t sta_rx_out;        /* delivered to the stack */
	uint32_t tx_credit_stalls;  /* TX aggregates that timed out waiting for CP credit */
#if EH_HOST_USB_RNDIS
	uint32_t usb_rndis_tx_queued;
	uint32_t usb_rndis_tx_sent;
	uint32_t usb_rndis_tx_rejected;
	uint32_t usb_rndis_tx_failed;
	uint32_t usb_rndis_rx_delivered;
	uint32_t usb_rndis_rx_dropped;
#endif
};

extern struct pkt_stats_t pkt_stats;

void eh_host_pkt_stats_init(void);
void eh_host_pkt_stats_deinit(void);

#else

static inline void eh_host_pkt_stats_init(void) {}
static inline void eh_host_pkt_stats_deinit(void) {}

#endif /* EH_HOST_PKT_STATS */

#ifdef __cplusplus
}
#endif
