/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Test: echo ESP_AUX_OT frames. First send one unknown aux_type (host drop). */

#include "sdkconfig.h"

#if CONFIG_EH_TEST_CP_AUX_LOOPBACK

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "eh_common_interface.h"
#include "eh_cp_core.h"
#include "eh_transport.h"
#include "eh_transport_cp.h"

#define AUX_UNKNOWN 0x55

static const char TAG[] = "aux_loop";

static int send_aux(uint8_t aux_type, const void *data, uint16_t len)
{
	interface_buffer_handle_t bh;
	uint8_t *copy = malloc(len);

	if (!copy)
		return -1;
	memcpy(copy, data, len);

	memset(&bh, 0, sizeof(bh));
	bh.if_type            = ESP_AUX_IF;
	bh.pkt_type           = aux_type;
	bh.payload            = copy;
	bh.payload_len        = len;
	bh.priv_buffer_handle = copy;
	bh.free_buf_handle    = free;

	if (send_to_host_queue(&bh, PRIO_Q_OTHERS)) {
		free(copy);
		return -1;
	}
	return 0;
}

static esp_err_t echo_rx(void *ctx, void *buf, uint16_t len, void *eb)
{
	static bool s_probe_sent;

	(void)ctx;
	(void)eb;

	if (!s_probe_sent) {
		static const char probe[] = "aux-unknown";
		s_probe_sent = true;
		send_aux(AUX_UNKNOWN, probe, sizeof(probe));
	}
	if (send_aux(ESP_AUX_OT, buf, len))
		ESP_LOGE(TAG, "[aux-loop] echo tx failed (%u bytes)", (unsigned int)len);
	return ESP_OK;
}

static esp_err_t loopback_init(void)
{
	esp_err_t ret = eh_cp_register_aux_rx_cb(ESP_AUX_OT, echo_rx, NULL);

	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "[aux-loop] register failed: %d", ret);
		return ret;
	}
	ESP_LOGW(TAG, "[aux-loop] ready");
	return ESP_OK;
}

static esp_err_t loopback_deinit(void)
{
	return eh_cp_unregister_aux_rx_cb(ESP_AUX_OT);
}

EH_CP_FEAT_REGISTER(loopback_init, loopback_deinit, "aux_loopback",
                    tskNO_AFFINITY, 399);

#endif /* CONFIG_EH_TEST_CP_AUX_LOOPBACK */
