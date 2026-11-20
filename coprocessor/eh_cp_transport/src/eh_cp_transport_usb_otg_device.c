/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* USB OTG device transport. CDC ACM is a byte stream. RPC is not a
 * separate USB class: serial frames are queued for recv_task, which
 * reassembles ESP_SERIAL_IF and dispatches to RPC.
 *
 * CONFIG_EH_TRANSPORT_CP_USB_OTG_DEVICE_RNDIS adds one RNDIS function in
 * eh_cp_transport_usb_otg_rndis.c. STA and AP Ethernet frames use that
 * function. RPC, serial, HCI, and flow control stay on CDC-ACM.
 */

#include "eh_cp_master_config.h"
#include "eh_cp_host_ps_state.h"

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"

#include "esp_log.h"
#include "esp_system.h"
#include "eh_log.h"
#include "endian.h"
#include "eh_transport_cp.h"
#include "eh_mempool.h"
#include "eh_interface.h"
#include "eh_transport.h"
#include "eh_caps.h"
#include "eh_tlv_tags.h"
#include "eh_header.h"
#include "eh_common_fw_version.h"
#include "eh_transport_utils.h"
#include "eh_cp_utils.h"
#include "eh_frame.h"
#include "eh_tlv.h"
#include "eh_tlv_defs.h"
#include "eh_tlv_v1.h"
#include "eh_tlv_v2.h"
#include "eh_tlv_v3.h"
#include "eh_check.h"
#include "soc/soc_caps.h"

#if CONFIG_ESP_CONSOLE_USB_CDC
#error "ESP Console USB CDC and the Hosted USB OTG device transport both claim TinyUSB. Disable the USB CDC console."
#endif

#if !CONFIG_TINYUSB_CDC_ENABLED
#error "USB OTG device transport requires TinyUSB CDC (TINYUSB_CDC_ENABLED)."
#endif

#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "eh_cp_transport_usb_otg_priv.h"

#define HOSTED_USB_TX_QUEUE_SIZE   CONFIG_EH_TRANSPORT_CP_USB_OTG_DEVICE_TX_Q_SIZE
#define HOSTED_USB_RX_QUEUE_SIZE   CONFIG_EH_TRANSPORT_CP_USB_OTG_DEVICE_RX_Q_SIZE
/* One block for usb_rx_task, one for recv_task after dequeue. Same shape as
 * SDIO_TX_MEMPOOL_MARGIN, with the USB floor of RX_Q_SIZE + 2. */
#define USB_RX_MEMPOOL_MARGIN      2
#define USB_RX_MEMPOOL_BLOCKS      (HOSTED_USB_RX_QUEUE_SIZE + USB_RX_MEMPOOL_MARGIN)
#define USB_RX_STREAM_SIZE         (USB_RX_MEMPOOL_BLOCKS * BUFFER_SIZE)
#define USB_CDC_CHUNK              512
#define USB_TINYUSB_TASK_STACK     4096
#define USB_TINYUSB_TASK_PRIO      5

static const char TAG[] = "USB_OTG_DRIVER";

static interface_handle_t * h_usb_init(void);
static int32_t h_usb_write(interface_handle_t *handle, interface_buffer_handle_t *buf_handle);
static int h_usb_read(interface_handle_t *if_handle, interface_buffer_handle_t *buf_handle);
static esp_err_t h_usb_reset(interface_handle_t *handle);
static void h_usb_deinit(interface_handle_t *handle);

if_ops_t if_ops = {
	.init = h_usb_init,
	.write = h_usb_write,
	.read = h_usb_read,
	.reset = h_usb_reset,
	.deinit = h_usb_deinit,
};

static interface_handle_t if_handle_g;
static interface_context_t context;

static struct hosted_mempool * buf_mp_tx_g;
static struct hosted_mempool * buf_mp_rx_g;

SemaphoreHandle_t usb_rx_sem;
SemaphoreHandle_t usb_rx_credits;
static SemaphoreHandle_t usb_tx_lock;
static SemaphoreHandle_t usb_cdc_lock;
QueueHandle_t usb_rx_queue[MAX_PRIORITY_QUEUES];
static StreamBufferHandle_t usb_rx_stream;
static uint8_t * usb_scratch_buf;

static TaskHandle_t usb_rx_task_handle;
static volatile bool usb_host_dtr;
/* SET_CONFIGURATION was seen. Detach before that must not reboot-loop. */
static volatile bool usb_configured;

static volatile uint8_t wifi_flow_ctrl;
static SemaphoreHandle_t flow_ctrl_sem;
static TaskHandle_t flow_ctrl_task_handle;
#define TRIGGER_FLOW_CTRL() do { if (flow_ctrl_sem) xSemaphoreGive(flow_ctrl_sem); } while (0)

static void usb_cdc_pull(void);

#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
volatile bool usb_exit_requested;
#endif

#define IS_HOST_POWER_SAVING() (!eh_cp_host_ps_reachable())

static void show_config(void)
{
#if CONFIG_EH_TRANSPORT_CP_USB_OTG_DEVICE_RNDIS
	ESP_LOGI(TAG, "transport[cp]: USB OTG device CDC ACM + RNDIS tx_q=%d rx_q=%d",
		(int)HOSTED_USB_TX_QUEUE_SIZE, (int)HOSTED_USB_RX_QUEUE_SIZE);
#else
	ESP_LOGI(TAG, "transport[cp]: USB OTG device CDC ACM tx_q=%d rx_q=%d",
		(int)HOSTED_USB_TX_QUEUE_SIZE, (int)HOSTED_USB_RX_QUEUE_SIZE);
#endif
}

static inline void h_usb_mempool_create(void)
{
	buf_mp_tx_g = hosted_mempool_create(NULL, 0,
			HOSTED_USB_TX_QUEUE_SIZE, BUFFER_SIZE);
	buf_mp_rx_g = hosted_mempool_create(NULL, 0,
			USB_RX_MEMPOOL_BLOCKS, BUFFER_SIZE);
#if CONFIG_ESP_CACHE_MALLOC
	assert(buf_mp_tx_g);
	assert(buf_mp_rx_g);
#endif
}

static inline void h_usb_mempool_destroy(void)
{
	hosted_mempool_destroy(buf_mp_tx_g);
	hosted_mempool_destroy(buf_mp_rx_g);
	buf_mp_tx_g = NULL;
	buf_mp_rx_g = NULL;
}

static inline void *h_usb_buffer_tx_alloc(size_t nbytes, uint8_t need_memset)
{
	return hosted_mempool_alloc(buf_mp_tx_g, nbytes, need_memset);
}

static inline void h_usb_buffer_tx_free(void *buf)
{
	hosted_mempool_free(buf_mp_tx_g, buf);
}

void *h_usb_buffer_rx_alloc(uint8_t need_memset)
{
	return hosted_mempool_alloc(buf_mp_rx_g, BUFFER_SIZE, need_memset);
}

static inline void h_usb_buffer_rx_free(void *buf)
{
	hosted_mempool_free(buf_mp_rx_g, buf);
}

void usb_rx_credit_release(void)
{
	if (usb_rx_credits)
		xSemaphoreGive(usb_rx_credits);
	usb_cdc_pull();
}

void usb_rx_read_done(void *handle)
{
	h_usb_buffer_rx_free(handle);
	usb_rx_credit_release();
}

static void flow_ctrl_task(void *pvParameters)
{
	(void)pvParameters;

	for (;;) {
		interface_buffer_handle_t buf_handle = {0};

		xSemaphoreTake(flow_ctrl_sem, portMAX_DELAY);
#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
		if (usb_exit_requested) {
			vTaskSuspend(NULL);
			continue;
		}
#endif
		buf_handle.throttle_cmd = wifi_flow_ctrl ?
			EH_FRAME_FLOW_CTRL_ON : EH_FRAME_FLOW_CTRL_OFF;
		ESP_LOGV(TAG, "flow_ctrl %u", buf_handle.throttle_cmd);
		send_to_host_queue(&buf_handle, PRIO_Q_SERIAL);
	}
}

void start_rx_data_throttling_if_needed(void)
{
	uint32_t queue_load;
	uint8_t load_percent;

	if (slv_cfg_g.throttle_high_threshold == 0 || HOSTED_USB_RX_QUEUE_SIZE == 0)
		return;
	if (slv_state_g.current_throttling)
		return;

	queue_load = uxQueueMessagesWaiting(usb_rx_queue[PRIO_Q_OTHERS]);
	load_percent = (uint8_t)((queue_load * 100) / HOSTED_USB_RX_QUEUE_SIZE);
	if (load_percent > slv_cfg_g.throttle_high_threshold) {
		slv_state_g.current_throttling = 1;
		wifi_flow_ctrl = 1;
#if ESP_PKT_STATS
		pkt_stats.sta_flowctrl_on++;
#endif
		TRIGGER_FLOW_CTRL();
	}
}

static void stop_rx_data_throttling_if_needed(void)
{
	uint32_t queue_load;
	uint8_t load_percent;

	if (!slv_state_g.current_throttling || HOSTED_USB_RX_QUEUE_SIZE == 0)
		return;

	queue_load = uxQueueMessagesWaiting(usb_rx_queue[PRIO_Q_OTHERS]);
	load_percent = (uint8_t)((queue_load * 100) / HOSTED_USB_RX_QUEUE_SIZE);
	if (load_percent < slv_cfg_g.throttle_low_threshold) {
		slv_state_g.current_throttling = 0;
		wifi_flow_ctrl = 0;
#if ESP_PKT_STATS
		pkt_stats.sta_flowctrl_off++;
#endif
		TRIGGER_FLOW_CTRL();
	}
}

static void h_usb_frame_init(void)
{
#if EH_CP_HOST_TYPE_MCU
	eh_frame_cfg_t frame_cfg = EH_FRAME_CFG_CP_MCU_USB_DEFAULT;
#else
	eh_frame_cfg_t frame_cfg = EH_FRAME_CFG_CP_LINUX_802_3_USB_DEFAULT;
#endif
	frame_cfg.checksum_enabled = EH_CP_CHECKSUM;
	eh_frame_init(&frame_cfg);
}

static void usb_apply_ps_flags(uint8_t flags)
{
	if (flags & FLAG_POWER_SAVE_STARTED) {
		ESP_LOGI(TAG, "Host informed starting to power sleep");
		if (context.event_handler)
			context.event_handler(ESP_POWER_SAVE_ON);
	} else if (flags & FLAG_POWER_SAVE_STOPPED) {
		ESP_LOGI(TAG, "Host informed that it waken up");
		if (context.event_handler) {
			context.event_handler(ESP_POWER_SAVE_OFF);
			context.event_handler(ESP_OPEN_DATA_PATH);
		}
	}
}

static void usb_hdr_len_flags(const uint8_t *buf, uint16_t *payload_len, uint8_t *flags)
{
	if (eh_frame_hdr_size() == EH_ESP_PAYLOAD_HEADER_V2_OFFSET) {
		const eh_header_v2_t *hdr = (const eh_header_v2_t *)buf;
		*payload_len = le16toh(hdr->len);
		*flags = hdr->flags;
	} else {
		const struct esp_payload_header *hdr = (const struct esp_payload_header *)buf;
		*payload_len = le16toh(hdr->len);
		*flags = hdr->flags;
	}
}

/* Host closed the port or the bus detached. A fresh boot is the next INIT. */
static void usb_restart_on_disconnect(const char *why)
{
#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
	if (usb_exit_requested)
		return;
#endif
	ESP_LOGW(TAG, "USB disconnect (%s), resetting", why);
	esp_restart();
}

static void usb_tinyusb_event_cb(tinyusb_event_t *event, void *arg)
{
	(void)arg;
	if (!event)
		return;
	if (event->id == TINYUSB_EVENT_ATTACHED) {
		usb_configured = true;
	} else if (event->id == TINYUSB_EVENT_DETACHED && usb_configured) {
		usb_restart_on_disconnect("detached");
	}
}

/* Rising DTR means the host opened the CDC port and can receive slave-up.
 * Falling DTR after that is the host closing the port. */
static void usb_on_dtr(bool dtr)
{
	if (!dtr) {
		if (usb_host_dtr)
			usb_restart_on_disconnect("DTR");
		usb_host_dtr = false;
		return;
	}
	if (usb_host_dtr)
		return;
	usb_host_dtr = true;
	ESP_LOGI(TAG, "USB host asserted DTR");
	if (context.event_handler)
		context.event_handler(ESP_OPEN_DATA_PATH);
}

/* Drain TinyUSB only while a pool credit is free and the stream can hold
 * another full frame. Unread FIFO data makes the device NAK, which stalls
 * the host TX instead of dropping a partial frame. The RX callback does not
 * run again until the FIFO is read, so callers re-arm this after a credit
 * or stream space returns. */
static void usb_cdc_pull(void)
{
	uint8_t chunk[USB_CDC_CHUNK];

	if (!usb_rx_stream || !usb_rx_credits || !usb_cdc_lock)
		return;
	if (xSemaphoreTake(usb_cdc_lock, portMAX_DELAY) != pdTRUE)
		return;

	while (uxSemaphoreGetCount(usb_rx_credits) > 0 &&
			xStreamBufferSpacesAvailable(usb_rx_stream) >= BUFFER_SIZE) {
		size_t rx_size = 0;
		size_t sent = 0;

		if (tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, chunk, sizeof(chunk), &rx_size) != ESP_OK ||
				rx_size == 0)
			break;
		while (sent < rx_size) {
			size_t n = xStreamBufferSend(usb_rx_stream, chunk + sent, rx_size - sent, 0);
			if (n == 0) {
				ESP_LOGE(TAG, "rx stream full after credit check, drop %u bytes",
						(unsigned)(rx_size - sent));
				break;
			}
			sent += n;
		}
	}

	xSemaphoreGive(usb_cdc_lock);
}

static void usb_cdc_rx_cb(int itf, cdcacm_event_t *event)
{
	(void)itf;
	(void)event;
	usb_cdc_pull();
}

static void usb_cdc_line_state_cb(int itf, cdcacm_event_t *event)
{
	(void)itf;
	if (!event)
		return;
	usb_on_dtr(event->line_state_changed_data.dtr);
}

static int stream_read_exact(uint8_t *dst, size_t n)
{
	size_t got = 0;

	while (got < n) {
#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
		if (usb_exit_requested)
			return -1;
		size_t r = xStreamBufferReceive(usb_rx_stream, dst + got, n - got, pdMS_TO_TICKS(50));
#else
		size_t r = xStreamBufferReceive(usb_rx_stream, dst + got, n - got, portMAX_DELAY);
#endif
		if (r == 0)
			continue;
		got += r;
	}
	return (int)got;
}

static esp_err_t usb_cdc_send(const uint8_t *data, size_t len)
{
	size_t off = 0;
	unsigned stalls = 0;

	if (!usb_tx_lock || xSemaphoreTake(usb_tx_lock, portMAX_DELAY) != pdTRUE)
		return ESP_ERR_INVALID_STATE;

	while (off < len) {
		size_t n = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, data + off, len - off);
		if (n == 0) {
			esp_err_t ferr = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0,
					pdMS_TO_TICKS(USB_TX_FLUSH_MS));
			if (ferr != ESP_OK && ferr != ESP_ERR_NOT_FINISHED) {
				xSemaphoreGive(usb_tx_lock);
				return ferr;
			}
			if (++stalls > 8) {
				xSemaphoreGive(usb_tx_lock);
				return ESP_ERR_TIMEOUT;
			}
			continue;
		}
		stalls = 0;
		off += n;
	}

	esp_err_t err = tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(USB_TX_FLUSH_MS));
	xSemaphoreGive(usb_tx_lock);
	return err;
}

static void usb_rx_task(void *pvParameters)
{
	(void)pvParameters;

	while (1) {
		interface_buffer_handle_t buf_handle = {0};
		uint8_t *buf = NULL;
		uint16_t payload_len = 0;
		uint8_t flags = 0;
		int total_len;
		uint8_t hdr_sz;

#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
		if (usb_exit_requested) {
			vTaskSuspend(NULL);
			continue;
		}
#endif
		hdr_sz = eh_frame_hdr_size();
		if (hdr_sz == 0) {
			vTaskDelay(pdMS_TO_TICKS(10));
			continue;
		}

		/* Reserve a pool block before consuming the frame. A bad frame gives
		 * the credit back; a queued frame keeps it until usb_rx_read_done. */
#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
		if (xSemaphoreTake(usb_rx_credits, pdMS_TO_TICKS(50)) != pdTRUE)
			continue;
		if (usb_exit_requested) {
			usb_rx_credit_release();
			vTaskSuspend(NULL);
			continue;
		}
#else
		xSemaphoreTake(usb_rx_credits, portMAX_DELAY);
#endif
		if (stream_read_exact(usb_scratch_buf, hdr_sz) < 0) {
			usb_rx_credit_release();
			continue;
		}

		usb_hdr_len_flags(usb_scratch_buf, &payload_len, &flags);
		usb_apply_ps_flags(flags);
		total_len = (int)payload_len + (int)hdr_sz;
		UPDATE_HEADER_RX_PKT_NO((struct esp_payload_header *)usb_scratch_buf);

		if (total_len > BUFFER_SIZE || total_len < (int)hdr_sz) {
			/* Drop the claimed payload so the next header stays aligned. */
			ESP_LOGE(TAG, "incoming data too big: %d", total_len);
			while (payload_len > 0) {
				size_t n = payload_len > BUFFER_SIZE ? BUFFER_SIZE : payload_len;
				if (stream_read_exact(usb_scratch_buf, n) < 0)
					break;
				payload_len = (uint16_t)(payload_len - n);
			}
			usb_rx_credit_release();
			continue;
		}

		if (payload_len > 0 &&
				stream_read_exact(usb_scratch_buf + hdr_sz, payload_len) < 0) {
			usb_rx_credit_release();
			continue;
		}

		buf_handle = (interface_buffer_handle_t){0};
		eh_frame_result_t fres = eh_frame_decode(usb_scratch_buf, (uint16_t)total_len, &buf_handle);
		if (fres != EH_FRAME_OK) {
			if (fres != EH_FRAME_DUMMY)
				ESP_LOGE(TAG, "usb rx: frame_decode err %d, drop", fres);
			usb_rx_credit_release();
			continue;
		}

		uint16_t off = (uint16_t)((const uint8_t *)buf_handle.payload - usb_scratch_buf);
		buf = h_usb_buffer_rx_alloc(MEMSET_NOT_REQUIRED);
		if (!buf) {
			ESP_LOGW(TAG, "usb rx: no rx buffer (pool drained), drop frame");
			usb_rx_credit_release();
			continue;
		}

		memcpy(buf, usb_scratch_buf, (size_t)total_len);
		buf_handle.payload = buf + off;
		buf_handle.free_buf_handle = usb_rx_read_done;
		buf_handle.priv_buffer_handle = buf;

#if ESP_PKT_STATS
		if (buf_handle.if_type == ESP_STA_IF)
			pkt_stats.hs_bus_sta_in++;
#endif
		if (buf_handle.if_type == ESP_SERIAL_IF)
			xQueueSend(usb_rx_queue[PRIO_Q_SERIAL], &buf_handle, portMAX_DELAY);
		else if (buf_handle.if_type == ESP_HCI_IF)
			xQueueSend(usb_rx_queue[PRIO_Q_BT], &buf_handle, portMAX_DELAY);
		else
			xQueueSend(usb_rx_queue[PRIO_Q_OTHERS], &buf_handle, portMAX_DELAY);
		xSemaphoreGive(usb_rx_sem);
		start_rx_data_throttling_if_needed();
		/* Stream space opened; the CDC callback will not re-fire on its own. */
		usb_cdc_pull();
	}
}

static int h_usb_read(interface_handle_t *if_handle, interface_buffer_handle_t *buf_handle)
{
	if (!if_handle || !buf_handle) {
		ESP_LOGE(TAG, "%s: NULL args", __func__);
		return ESP_FAIL;
	}
	if (if_handle->state != ACTIVE)
		return ESP_FAIL;

	if (IS_HOST_POWER_SAVING())
		return ESP_FAIL;

#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
	if (pdTRUE != xSemaphoreTake(usb_rx_sem, pdMS_TO_TICKS(50)))
		return ESP_FAIL;
#else
	xSemaphoreTake(usb_rx_sem, portMAX_DELAY);
#endif

	if (pdFALSE == xQueueReceive(usb_rx_queue[PRIO_Q_SERIAL], buf_handle, 0))
		if (pdFALSE == xQueueReceive(usb_rx_queue[PRIO_Q_BT], buf_handle, 0))
			if (pdFALSE == xQueueReceive(usb_rx_queue[PRIO_Q_OTHERS], buf_handle, 0)) {
				ESP_LOGE(TAG, "%s No element in rx queue", __func__);
				return ESP_FAIL;
			}

	stop_rx_data_throttling_if_needed();
	return buf_handle->payload_len;
}

static int32_t h_usb_write(interface_handle_t *handle, interface_buffer_handle_t *buf_handle)
{
	int32_t total_len = 0;
	uint8_t *sendbuf = NULL;

	if (!handle || !buf_handle) {
		ESP_LOGE(TAG, "Invalid arguments");
		return ESP_FAIL;
	}
	if (handle->state != ACTIVE)
		return ESP_FAIL;
	if (IS_HOST_POWER_SAVING()) {
		ESP_LOGE(TAG, "Host is power saving, skipping write");
		return ESP_FAIL;
	}
	if (!buf_handle->throttle_cmd) {
		if (!buf_handle->payload_len || !buf_handle->payload) {
			ESP_LOGE(TAG, "Invalid arguments, len:%" PRIu16, buf_handle->payload_len);
			return ESP_FAIL;
		}
	}

#if CONFIG_EH_TRANSPORT_CP_USB_OTG_DEVICE_RNDIS
	/* STA and AP payloads are already 802.3 frames. Header-only flow
	 * control, RPC, serial, HCI, and test stay on CDC. */
	if ((buf_handle->if_type == ESP_STA_IF || buf_handle->if_type == ESP_AP_IF) &&
			buf_handle->payload_len && buf_handle->payload) {
		esp_err_t err = usb_rndis_send(buf_handle->payload, buf_handle->payload_len);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "rndis transmit error: %s", esp_err_to_name(err));
			return ESP_FAIL;
		}
#if ESP_PKT_STATS
		if (buf_handle->if_type == ESP_STA_IF)
			pkt_stats.sta_sh_out++;
#endif
		return buf_handle->payload_len;
	}
#endif

	total_len = (int32_t)buf_handle->payload_len + eh_frame_hdr_size();
	sendbuf = h_usb_buffer_tx_alloc((size_t)total_len, MEMSET_NOT_REQUIRED);
	if (sendbuf == NULL) {
		ESP_LOGE(TAG, "send buffer[%" PRIu32 "] malloc fail", (uint32_t)total_len);
		return ESP_FAIL;
	}

	if (buf_handle->payload_len && buf_handle->payload)
		memcpy(sendbuf + eh_frame_hdr_size(), buf_handle->payload, buf_handle->payload_len);

	{
		interface_buffer_handle_t h = {0};
		h.if_type = buf_handle->if_type;
		h.if_num = buf_handle->if_num;
		h.flags = buf_handle->flags;
		h.seq_num = buf_handle->seq_num;
		h.pkt_type = buf_handle->pkt_type;
		h.throttle_cmd = buf_handle->throttle_cmd;
		UPDATE_HEADER_TX_PKT_NO_IBUF(&h);
		eh_frame_encode(sendbuf, &h, buf_handle->payload_len);
	}

	if (usb_cdc_send(sendbuf, (size_t)total_len) != ESP_OK) {
		ESP_LOGE(TAG, "usb transmit error");
		h_usb_buffer_tx_free(sendbuf);
		return ESP_FAIL;
	}

#if ESP_PKT_STATS
	if (buf_handle->if_type == ESP_STA_IF)
		pkt_stats.sta_sh_out++;
	else if (buf_handle->if_type == ESP_SERIAL_IF)
		pkt_stats.serial_tx_total++;
#endif
	h_usb_buffer_tx_free(sendbuf);
	return buf_handle->payload_len;
}

static interface_handle_t * h_usb_init(void)
{
	uint16_t prio_q_idx;

	if (if_handle_g.state >= DEACTIVE)
		return &if_handle_g;

	show_config();
	h_usb_frame_init();
	h_usb_mempool_create();

	usb_rx_sem = xSemaphoreCreateCounting(HOSTED_USB_RX_QUEUE_SIZE * MAX_PRIORITY_QUEUES, 0);
	usb_rx_credits = xSemaphoreCreateCounting(USB_RX_MEMPOOL_BLOCKS, USB_RX_MEMPOOL_BLOCKS);
	usb_tx_lock = xSemaphoreCreateMutex();
	usb_cdc_lock = xSemaphoreCreateMutex();
	flow_ctrl_sem = xSemaphoreCreateBinary();
	usb_rx_stream = xStreamBufferCreate(USB_RX_STREAM_SIZE, 1);
	assert(usb_rx_sem != NULL);
	assert(usb_rx_credits != NULL);
	assert(usb_tx_lock != NULL);
	assert(usb_cdc_lock != NULL);
	assert(flow_ctrl_sem != NULL);
	assert(usb_rx_stream != NULL);

	for (prio_q_idx = 0; prio_q_idx < MAX_PRIORITY_QUEUES; prio_q_idx++) {
		usb_rx_queue[prio_q_idx] = xQueueCreate(HOSTED_USB_RX_QUEUE_SIZE,
				sizeof(interface_buffer_handle_t));
		assert(usb_rx_queue[prio_q_idx] != NULL);
	}

	if (!usb_scratch_buf) {
		usb_scratch_buf = malloc(BUFFER_SIZE);
		assert(usb_scratch_buf);
	}

#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
	usb_exit_requested = false;
#endif
	if (usb_rx_task_handle == NULL)
		assert(xTaskCreate(usb_rx_task, "usb_rx_task",
				EH_CP_TASK_STACK_SIZE, NULL,
				EH_CP_TASK_PRIO_DEFAULT, &usb_rx_task_handle) == pdTRUE);
	if (flow_ctrl_task_handle == NULL)
		assert(xTaskCreate(flow_ctrl_task, "usb_flow_ctrl",
				EH_CP_TASK_STACK_SIZE, NULL,
				EH_CP_TASK_PRIO_DEFAULT, &flow_ctrl_task_handle) == pdTRUE);

	usb_configured = false;

#if CONFIG_EH_TRANSPORT_CP_USB_OTG_DEVICE_RNDIS
	usb_rndis_prepare();
#endif
	const tinyusb_config_t tusb_cfg = {
#if SOC_USB_OTG_PERIPH_NUM > 1
		.port = TINYUSB_PORT_HIGH_SPEED_0,
#else
		.port = TINYUSB_PORT_FULL_SPEED_0,
#endif
		.task = {
			.size = USB_TINYUSB_TASK_STACK,
			.priority = USB_TINYUSB_TASK_PRIO,
			.xCoreID = 0,
		},
		.event_cb = usb_tinyusb_event_cb,
#if CONFIG_EH_TRANSPORT_CP_USB_OTG_DEVICE_RNDIS
		.descriptor = {
			.device = usb_rndis_device_descriptor(),
			.full_speed_config = usb_rndis_fs_config(),
#if TUD_OPT_HIGH_SPEED
			.high_speed_config = usb_rndis_hs_config(),
#endif
		},
#endif
	};
	EH_CHECK_OK(tinyusb_driver_install(&tusb_cfg));
	const tusb_desc_device_t *dev =
		(const tusb_desc_device_t *)tud_descriptor_device_cb();
	ESP_LOGI(TAG, "USB VID:PID %04x:%04x", dev->idVendor, dev->idProduct);

	const tinyusb_config_cdcacm_t acm_cfg = {
		.cdc_port = TINYUSB_CDC_ACM_0,
		.callback_rx = usb_cdc_rx_cb,
		.callback_line_state_changed = usb_cdc_line_state_cb,
	};
	EH_CHECK_OK(tinyusb_cdcacm_init(&acm_cfg));

	memset(&if_handle_g, 0, sizeof(if_handle_g));
	if_handle_g.state = ACTIVE;

	/* Host may already have the port open; the line-state callback only
	 * reports later edges. */
	if (tud_cdc_n_connected(0))
		usb_on_dtr(true);

	eh_cp_utils_log_mem_stats(TAG, "usb_otg_init");
	return &if_handle_g;
}

static void h_usb_deinit(interface_handle_t *handle)
{
	(void)handle;
#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
	if (if_handle_g.state == DEINIT) {
		ESP_LOGW(TAG, "USB already deinitialized");
		return;
	}
	if_handle_g.state = DEINIT;
	usb_exit_requested = true;
	if (usb_rx_sem)
		xSemaphoreGive(usb_rx_sem);
	if (usb_rx_credits)
		xSemaphoreGive(usb_rx_credits);
	if (flow_ctrl_sem)
		xSemaphoreGive(flow_ctrl_sem);

	tinyusb_cdcacm_unregister_callback(TINYUSB_CDC_ACM_0, CDC_EVENT_RX);
	tinyusb_cdcacm_unregister_callback(TINYUSB_CDC_ACM_0, CDC_EVENT_LINE_STATE_CHANGED);

	vTaskDelay(pdMS_TO_TICKS(100));
	if (usb_rx_task_handle) {
		vTaskDelete(usb_rx_task_handle);
		usb_rx_task_handle = NULL;
	}
	if (flow_ctrl_task_handle) {
		vTaskDelete(flow_ctrl_task_handle);
		flow_ctrl_task_handle = NULL;
	}
#if CONFIG_EH_TRANSPORT_CP_USB_OTG_DEVICE_RNDIS
	usb_rndis_rx_stop_task();
#endif
	vTaskDelay(pdMS_TO_TICKS(20));

	for (int i = 0; i < MAX_PRIORITY_QUEUES; i++) {
		if (!usb_rx_queue[i])
			continue;
		interface_buffer_handle_t b;
		while (xQueueReceive(usb_rx_queue[i], &b, 0) == pdTRUE)
			if (b.free_buf_handle)
				b.free_buf_handle(b.priv_buffer_handle);
		vQueueDelete(usb_rx_queue[i]);
		usb_rx_queue[i] = NULL;
	}
	if (usb_rx_sem) {
		vSemaphoreDelete(usb_rx_sem);
		usb_rx_sem = NULL;
	}
	if (usb_rx_credits) {
		vSemaphoreDelete(usb_rx_credits);
		usb_rx_credits = NULL;
	}
	if (usb_tx_lock) {
		vSemaphoreDelete(usb_tx_lock);
		usb_tx_lock = NULL;
	}
	if (usb_cdc_lock) {
		vSemaphoreDelete(usb_cdc_lock);
		usb_cdc_lock = NULL;
	}
	if (flow_ctrl_sem) {
		vSemaphoreDelete(flow_ctrl_sem);
		flow_ctrl_sem = NULL;
	}
	if (usb_rx_stream) {
		vStreamBufferDelete(usb_rx_stream);
		usb_rx_stream = NULL;
	}
	if (usb_scratch_buf) {
		free(usb_scratch_buf);
		usb_scratch_buf = NULL;
	}
	h_usb_mempool_destroy();
	usb_host_dtr = false;

	if (tinyusb_cdcacm_initialized(TINYUSB_CDC_ACM_0))
		tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
	tinyusb_driver_uninstall();
#if CONFIG_EH_TRANSPORT_CP_USB_OTG_DEVICE_RNDIS
	usb_rndis_delete_tx_semaphores();
#endif
#endif
}

static esp_err_t h_usb_reset(interface_handle_t *handle)
{
	(void)handle;
	if (usb_rx_stream && xStreamBufferReset(usb_rx_stream) != pdPASS)
		ESP_LOGW(TAG, "rx stream reset failed (reader blocked)");
	return tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(100));
}

interface_context_t *interface_insert_driver(int (*event_handler)(uint8_t val))
{
	memset(&context, 0, sizeof(context));
	context.type = USB_OTG_DEVICE;
	context.if_ops = &if_ops;
	context.event_handler = event_handler;
	return &context;
}

int interface_remove_driver(void)
{
	memset(&context, 0, sizeof(context));
	return 0;
}

void generate_startup_event(uint8_t cap, uint32_t ext_cap, uint8_t raw_tp_cap,
		const uint32_t feat_caps[8])
{
	interface_buffer_handle_t buf_handle = {0};
	struct esp_priv_event *event = NULL;
	uint16_t len = 0;
	uint32_t total_len = 0;
	int rc;

	buf_handle.payload = h_usb_buffer_tx_alloc(512, MEMSET_NOT_REQUIRED);
	assert(buf_handle.payload);

	event = (struct esp_priv_event *)(buf_handle.payload + eh_frame_hdr_size());
	event->event_type = eh_priv_event_init_wire();

	eh_tlv_builder_t tlv;
	uint16_t budget = MAX_TRANSPORT_BUF_SIZE - eh_frame_hdr_size() - 2;
	eh_tlv_builder_init(&tlv, event->event_data, budget);

#if EH_TLV_V1
	{
		struct eh_fw_version fw_ver = { 0 };
		strlcpy(fw_ver.project_name, PROJECT_NAME, sizeof(fw_ver.project_name));
		fw_ver.major = PROJECT_VERSION_MAJOR_1;
		fw_ver.minor = PROJECT_VERSION_MINOR_1;
		fw_ver.patch = PROJECT_VERSION_PATCH_1;

		rc = eh_tlv_pack_v1(&tlv,
			CONFIG_IDF_FIRMWARE_CHIP_ID, cap, raw_tp_cap,
			&fw_ver, sizeof(fw_ver));
		if (rc) {
			ESP_LOGE(TAG, "TLV v1 overflow");
			goto out;
		}
	}
#endif

#if EH_TLV_V2
	{
		uint32_t fw_version = EH_VERSION_VAL(
			PROJECT_VERSION_MAJOR_1, PROJECT_VERSION_MINOR_1, PROJECT_VERSION_PATCH_1);

		rc = eh_tlv_pack_v2(&tlv,
			CONFIG_IDF_FIRMWARE_CHIP_ID, cap, raw_tp_cap,
			ext_cap, fw_version,
			HOSTED_USB_RX_QUEUE_SIZE, HOSTED_USB_TX_QUEUE_SIZE,
			EH_CP_RPC_WIRE_VERSION);
		if (rc) {
			ESP_LOGE(TAG, "TLV v2 overflow");
			goto out;
		}
	}
#endif

#if EH_TLV_V3
	rc = eh_tlv_pack_v3(&tlv,
		feat_caps, EH_FEAT_CAPS_COUNT,
		ESP_HOSTED_HDR_VERSION_V2, ESP_HOSTED_RPC_VERSION_V3,
		RPC_EP_NAME_REQ, RPC_EP_NAME_EVT);
	if (rc) {
		ESP_LOGE(TAG, "TLV v3 overflow");
		goto out;
	}
#endif

	len = eh_tlv_builder_len(&tlv);
	event->event_len = len;
	len += 2;

	{
		interface_buffer_handle_t h = {0};
		h.if_type = ESP_PRIV_IF;
		h.if_num = 0;
		h.pkt_type = eh_priv_pkt_type_event_wire();
		UPDATE_HEADER_TX_PKT_NO_IBUF(&h);
		eh_frame_encode(buf_handle.payload, &h, len);
	}

	total_len = len + eh_frame_hdr_size();
	if (usb_cdc_send(buf_handle.payload, total_len) != ESP_OK)
		ESP_LOGE(TAG, "startup: usb slave transmit error");

out:
	h_usb_buffer_tx_free(buf_handle.payload);
}
