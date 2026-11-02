/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "eh_cp_master_config.h"

#if EH_CP_FEAT_OPENTHREAD_READY

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"

#include "esp_log.h"
#include "esp_openthread_transport.h"

#include "eh_common_interface.h"
#include "eh_cp_core.h"
#include "eh_transport.h"
#include "eh_transport_cp.h"
#include "eh_cp_feat_openthread_transport.h"

/* Below every bus's max payload. The peer's HDLC decoder rejoins chunks. */
#define OT_AUX_CHUNK 1024

static const char TAG[] = "h_ot_aux";

static StreamBufferHandle_t s_rx_ring;
/* Held by the rx callback and by deinit. */
static SemaphoreHandle_t s_rx_lock;

static esp_err_t ot_rx_from_host(void *ctx, void *buf, uint16_t len, void *eb)
{
	esp_err_t ret = ESP_OK;

	(void)ctx;
	(void)eb;

	xSemaphoreTake(s_rx_lock, portMAX_DELAY);
	if (!s_rx_ring) {
		ret = ESP_ERR_INVALID_STATE;
	} else if (xStreamBufferSpacesAvailable(s_rx_ring) < len) {
		/* Drop the whole frame, never a part of it. */
		ESP_LOGW(TAG, "rx ring full, dropped %u bytes", (unsigned int)len);
		ret = ESP_ERR_NO_MEM;
	} else {
		xStreamBufferSend(s_rx_ring, buf, len, 0);
	}
	xSemaphoreGive(s_rx_lock);

	if (ret == ESP_OK)
		esp_openthread_transport_notify_rcp_rx();
	return ret;
}

esp_err_t eh_cp_feat_openthread_transport_tx(const void *buf, size_t len)
{
	const uint8_t *p = buf;

	if (!buf || !len)
		return ESP_ERR_INVALID_ARG;

	while (len) {
		uint16_t n = (len > OT_AUX_CHUNK) ? OT_AUX_CHUNK : (uint16_t)len;
		interface_buffer_handle_t bh;
		uint8_t *copy = malloc(n);

		if (!copy)
			return ESP_ERR_NO_MEM;
		memcpy(copy, p, n);

		memset(&bh, 0, sizeof(bh));
		bh.if_type            = ESP_AUX_IF;
		bh.pkt_type           = ESP_AUX_OT;
		bh.payload            = copy;
		bh.payload_len        = n;
		bh.priv_buffer_handle = copy;
		bh.free_buf_handle    = free;

		if (send_to_host_queue(&bh, PRIO_Q_OTHERS)) {
			free(copy);
			return ESP_FAIL;
		}
		p   += n;
		len -= n;
	}
	return ESP_OK;
}

esp_err_t eh_cp_feat_openthread_transport_rx(void *buf, size_t buf_size, size_t *out_len)
{
	size_t n;

	if (!buf || !buf_size || !out_len)
		return ESP_ERR_INVALID_ARG;
	if (!s_rx_ring)
		return ESP_ERR_INVALID_STATE;

	n = xStreamBufferReceive(s_rx_ring, buf, buf_size, 0);
	if (!n)
		return ESP_ERR_TIMEOUT;

	*out_len = n;

	/* The mainloop reads once per wake: wake it again if bytes remain. */
	if (!xStreamBufferIsEmpty(s_rx_ring))
		esp_openthread_transport_notify_rcp_rx();

	return ESP_OK;
}

esp_err_t eh_cp_feat_openthread_transport_init(void)
{
	if (s_rx_ring)
		return ESP_OK;

	if (!s_rx_lock) {
		s_rx_lock = xSemaphoreCreateMutex();
		if (!s_rx_lock)
			return ESP_ERR_NO_MEM;
	}
	s_rx_ring = xStreamBufferCreate(EH_CP_OT_HOSTED_RX_BUF_SIZE, 1);
	if (!s_rx_ring)
		return ESP_ERR_NO_MEM;

	if (eh_cp_register_aux_rx_cb(ESP_AUX_OT, ot_rx_from_host, NULL) != ESP_OK) {
		vStreamBufferDelete(s_rx_ring);
		s_rx_ring = NULL;
		return ESP_FAIL;
	}
	return ESP_OK;
}

void eh_cp_feat_openthread_transport_deinit(void)
{
	if (!s_rx_ring)
		return;

	eh_cp_unregister_aux_rx_cb(ESP_AUX_OT);
	/* Wait for a running rx callback. */
	xSemaphoreTake(s_rx_lock, portMAX_DELAY);
	vStreamBufferDelete(s_rx_ring);
	s_rx_ring = NULL;
	xSemaphoreGive(s_rx_lock);
}

#endif /* EH_CP_FEAT_OPENTHREAD_READY */
