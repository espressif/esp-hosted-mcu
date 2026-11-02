/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eh_host_port_master_config.h"
#include "eh_host_port_openthread.h"

#if EH_HOST_TYPE_MCU && EH_HOST_FEAT_OPENTHREAD_READY

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_openthread_transport.h"

#include "eh_common_caps.h"
#include "eh_common_interface.h"
#include "eh_host_mcu_transport_channels.h"
#include "eh_host_mcu_transport_init_event.h"
#include "eh_host_openthread.h"
#include "eh_host_openthread_priv.h"
#include "eh_host_port_sync.h"

/* Below every bus's max payload. The peer's HDLC decoder rejoins chunks. */
#define OT_AUX_CHUNK 1024

typedef struct {
	uint8_t *mem;       /* owned; freed when read */
	uint8_t *data;
	size_t   len;
} ot_rx_frame_t;

static const char TAG[] = "eh_ot_aux";

static eh_host_channel_t     *s_chan;
static eh_host_port_queue_t  *s_rx_q;
/* Held by the rx callback and by deinit. */
static eh_host_port_mutex_t  *s_rx_lock;
/* Frame being read; OpenThread task only. */
static ot_rx_frame_t          s_cur;
static size_t                 s_cur_off;

static esp_err_t ot_rx_from_cp(void *h, void *buf, void *buff_to_free, size_t len)
{
	ot_rx_frame_t f = { .mem = buff_to_free, .data = buf, .len = len };
	int queued = 0;

	(void)h;

	eh_host_port_mutex_lock(s_rx_lock);
	if (s_rx_q && eh_host_port_queue_send(s_rx_q, &f, 0) == EH_HOST_PORT_OK)
		queued = 1;
	eh_host_port_mutex_unlock(s_rx_lock);

	if (!queued) {
		/* Drop the whole frame, never a part of it. */
		ESP_LOGW(TAG, "rx queue full, dropped %u bytes", (unsigned int)len);
		free(buff_to_free);
		return ESP_OK;
	}
	esp_openthread_transport_notify_host_rx();
	return ESP_OK;
}

esp_err_t eh_host_openthread_transport_tx(const void *buf, size_t len)
{
	const uint8_t *p = buf;

	if (!buf || !len)
		return ESP_ERR_INVALID_ARG;
	if (eh_host_openthread_rcp_start() != ESP_OK)
		return ESP_FAIL;

	while (len) {
		uint16_t n = (len > OT_AUX_CHUNK) ? OT_AUX_CHUNK : (uint16_t)len;
		esp_err_t ret = eh_host_transport_aux_tx(ESP_AUX_OT, p, n, 0);

		if (ret != ESP_OK)
			return ret;
		p   += n;
		len -= n;
	}
	return ESP_OK;
}

esp_err_t eh_host_openthread_transport_rx(void *buf, size_t buf_size, size_t *out_len)
{
	size_t n;

	if (!buf || !buf_size || !out_len)
		return ESP_ERR_INVALID_ARG;
	if (!s_rx_q)
		return ESP_ERR_TIMEOUT;

	if (!s_cur.mem) {
		if (eh_host_port_queue_receive(s_rx_q, &s_cur, 0) != EH_HOST_PORT_OK)
			return ESP_ERR_TIMEOUT;
		s_cur_off = 0;
	}

	/* Byte stream into the HDLC decoder: the rest goes out on the next call. */
	n = s_cur.len - s_cur_off;
	if (n > buf_size)
		n = buf_size;
	memcpy(buf, s_cur.data + s_cur_off, n);
	s_cur_off += n;
	if (s_cur_off == s_cur.len) {
		free(s_cur.mem);
		s_cur.mem = NULL;
	}
	*out_len = n;
	return ESP_OK;
}

esp_err_t eh_host_openthread_transport_init(void)
{
	if (s_rx_q)
		return ESP_OK;

	if (!(eh_host_mcu_transport_get_ext_capabilities() & ESP_EXT_CAP_AUX_OT)) {
		ESP_LOGE(TAG, "co-processor does not run OpenThread over the ESP-Hosted "
		         "transport; enable CONFIG_ESP_HOSTED_CP_FEAT_OPENTHREAD and "
		         "CONFIG_OPENTHREAD_RCP_CUSTOM on it");
		return ESP_ERR_NOT_SUPPORTED;
	}

	if (!s_rx_lock) {
		s_rx_lock = eh_host_port_mutex_create();
		if (!s_rx_lock)
			return ESP_ERR_NO_MEM;
	}
	s_rx_q = eh_host_port_queue_create(H_OT_HOSTED_RX_QUEUE_LEN,
	                                   sizeof(ot_rx_frame_t));
	if (!s_rx_q)
		return ESP_ERR_NO_MEM;

	s_chan = eh_host_transport_add_aux_channel(ESP_AUX_OT, ot_rx_from_cp);
	if (!s_chan) {
		eh_host_port_queue_destroy(s_rx_q);
		s_rx_q = NULL;
		return ESP_FAIL;
	}
	return ESP_OK;
}

void eh_host_openthread_transport_deinit(void)
{
	ot_rx_frame_t f;

	if (!s_rx_q)
		return;

	if (s_chan) {
		(void)eh_host_transport_remove_channel(s_chan);
		s_chan = NULL;
	}
	/* Wait for a running rx callback. */
	eh_host_port_mutex_lock(s_rx_lock);
	while (eh_host_port_queue_receive(s_rx_q, &f, 0) == EH_HOST_PORT_OK)
		free(f.mem);
	eh_host_port_queue_destroy(s_rx_q);
	s_rx_q = NULL;
	eh_host_port_mutex_unlock(s_rx_lock);
	free(s_cur.mem);
	s_cur.mem = NULL;
}

#endif /* EH_HOST_TYPE_MCU && EH_HOST_FEAT_OPENTHREAD_READY */
