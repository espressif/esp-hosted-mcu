/* SPDX-License-Identifier: Apache-2.0 */
/* Periodic dump of the host-side transport packet counters (Kconfig-gated). */
#include "eh_host_pkt_stats.h"

#if EH_HOST_PKT_STATS

#include "esp_log.h"
#include "esp_timer.h"

#define TAG "eh_pkt_stats"

struct pkt_stats_t pkt_stats;

static esp_timer_handle_t s_stats_timer;

static void pkt_stats_timer_func(void *arg)
{
	(void)arg;
	ESP_LOGI(TAG, "STA TX: in[%lu] trans[%lu] out[%lu] drop[%lu] credit_stall[%lu] "
	         "RX: in[%lu] out[%lu]",
	         (unsigned long)pkt_stats.sta_tx_in_pass,
	         (unsigned long)pkt_stats.sta_tx_trans_in,
	         (unsigned long)pkt_stats.sta_tx_out,
	         (unsigned long)pkt_stats.sta_tx_out_drop,
	         (unsigned long)pkt_stats.tx_credit_stalls,
	         (unsigned long)pkt_stats.sta_rx_in,
	         (unsigned long)pkt_stats.sta_rx_out);
}

void eh_host_pkt_stats_init(void)
{
	if (s_stats_timer)
		return;
	const esp_timer_create_args_t args = {
		.callback = pkt_stats_timer_func,
		.name = "eh_pkt_stats",
	};
	if (esp_timer_create(&args, &s_stats_timer) != ESP_OK) {
		ESP_LOGE(TAG, "stats timer create failed");
		return;
	}
	esp_timer_start_periodic(s_stats_timer,
	    (uint64_t)EH_HOST_PKT_STATS_INTERVAL_SEC * 1000000ULL);
}

void eh_host_pkt_stats_deinit(void)
{
	if (!s_stats_timer)
		return;
	esp_timer_stop(s_stats_timer);
	esp_timer_delete(s_stats_timer);
	s_stats_timer = NULL;
}

#endif /* EH_HOST_PKT_STATS */
