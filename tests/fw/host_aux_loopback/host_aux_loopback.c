/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Test: send ESP_AUX_OT frames; check each echo is intact and in order. */

#include "sdkconfig.h"

#if CONFIG_EH_TEST_HOST_AUX_LOOPBACK

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "eh_common_caps.h"
#include "eh_common_interface.h"
#include "eh_frame.h"
#include "eh_host_auto_init.h"
#include "eh_host_mcu_transport.h"
#include "eh_host_mcu_transport_channels.h"
#include "eh_host_mcu_transport_init_event.h"

#define MAX_LEN   1400
#define BURST     32
#define BURST_LEN 200
/* No CP handler exists for this aux_type. */
#define AUX_UNKNOWN 0x55

typedef struct {
	uint8_t *buf;
	size_t   len;
} rx_item_t;

static const char TAG[] = "aux_loop";

/* aux_tx() rejects unknown aux_types: build the frame here. */
static void send_unknown_aux(void)
{
	uint16_t len = 1;
	uint8_t *buf = heap_caps_malloc(eh_frame_hdr_size() + len, MALLOC_CAP_DMA);
	interface_buffer_handle_t bh = { 0 };

	if (!buf)
		return;
	buf[eh_frame_hdr_size()] = 0xA5;
	bh.priv_buffer_handle = buf;
	bh.if_type            = ESP_AUX_IF;
	bh.pkt_type           = AUX_UNKNOWN;
	bh.payload            = buf + eh_frame_hdr_size();
	bh.payload_len        = len;
	bh.free_buf_handle    = free;
	eh_frame_encode(buf, &bh, len);
	(void)eh_host_mcu_transport_tx(&bh);
}

static const size_t k_sizes[] = { 1, 64, 512, MAX_LEN };

static QueueHandle_t s_rx_q;
static uint8_t s_tx[MAX_LEN];

static esp_err_t aux_rx(void *h, void *buf, void *buff_to_free, size_t len)
{
	rx_item_t it = { .buf = buf, .len = len };

	(void)h;
	(void)buff_to_free;
	if (xQueueSend(s_rx_q, &it, 0) != pdTRUE)
		free(buf);
	return ESP_OK;
}

static void fill(uint8_t *p, size_t n, uint8_t seed)
{
	for (size_t i = 0; i < n; i++)
		p[i] = (uint8_t)(seed + i * 7);
}

static bool expect_echo(const uint8_t *want, size_t n)
{
	rx_item_t it;
	bool ok;

	if (xQueueReceive(s_rx_q, &it, pdMS_TO_TICKS(5000)) != pdTRUE) {
		ESP_LOGE(TAG, "[aux-loop] FAIL timeout waiting for %u bytes", (unsigned int)n);
		return false;
	}
	ok = (it.len == n) && memcmp(it.buf, want, n) == 0;
	if (!ok)
		ESP_LOGE(TAG, "[aux-loop] FAIL mismatch: got %u bytes, want %u",
		         (unsigned int)it.len, (unsigned int)n);
	free(it.buf);
	return ok;
}

static void fail_out(void)
{
	vTaskDelete(NULL);
}

static void drive_task(void *arg)
{
	eh_host_channel_t *ch;
	uint32_t ext = eh_host_mcu_transport_get_ext_capabilities();
	unsigned int i;

	(void)arg;

	if (!(ext & ESP_EXT_CAP_AUX_IF)) {
		ESP_LOGE(TAG, "[aux-loop] FAIL CP does not advertise AUX (ext caps 0x%08x)",
		         (unsigned int)ext);
		fail_out();
		return;
	}
	ch = eh_host_transport_add_aux_channel(ESP_AUX_OT, aux_rx);
	if (!ch) {
		ESP_LOGE(TAG, "[aux-loop] FAIL add_aux_channel");
		fail_out();
		return;
	}

	/* The CP must drop it and keep going. */
	send_unknown_aux();

	for (i = 0; i < sizeof(k_sizes) / sizeof(k_sizes[0]); i++) {
		size_t n = k_sizes[i];

		fill(s_tx, n, (uint8_t)(i + 1));
		if (eh_host_transport_aux_tx(ESP_AUX_OT, s_tx, n, 0) != ESP_OK) {
			ESP_LOGE(TAG, "[aux-loop] FAIL tx %u bytes", (unsigned int)n);
			fail_out();
			return;
		}
		if (!expect_echo(s_tx, n)) {
			fail_out();
			return;
		}
		ESP_LOGW(TAG, "[aux-loop] ok %u bytes", (unsigned int)n);
	}

	for (i = 0; i < BURST; i++) {
		fill(s_tx, BURST_LEN, (uint8_t)(0x40 + i));
		if (eh_host_transport_aux_tx(ESP_AUX_OT, s_tx, BURST_LEN, 0) != ESP_OK) {
			ESP_LOGE(TAG, "[aux-loop] FAIL burst tx %u", i);
			fail_out();
			return;
		}
	}
	for (i = 0; i < BURST; i++) {
		fill(s_tx, BURST_LEN, (uint8_t)(0x40 + i));
		if (!expect_echo(s_tx, BURST_LEN)) {
			ESP_LOGE(TAG, "[aux-loop] FAIL burst frame %u", i);
			fail_out();
			return;
		}
	}
	ESP_LOGW(TAG, "[aux-loop] burst of %u in order", BURST);
	ESP_LOGW(TAG, "[aux-loop] all echoes intact");
	vTaskDelete(NULL);
}

static esp_err_t loopback_init(void)
{
	s_rx_q = xQueueCreate(BURST + 4, sizeof(rx_item_t));
	if (!s_rx_q)
		return ESP_ERR_NO_MEM;
	if (xTaskCreate(drive_task, "aux_loop", 4096, NULL, 5, NULL) != pdPASS)
		return ESP_FAIL;
	return ESP_OK;
}

static esp_err_t loopback_deinit(void) { return ESP_OK; }

EH_HOST_FEAT_REGISTER(loopback_init, loopback_deinit, "aux_loopback", 399);

#endif /* CONFIG_EH_TEST_HOST_AUX_LOOPBACK */
