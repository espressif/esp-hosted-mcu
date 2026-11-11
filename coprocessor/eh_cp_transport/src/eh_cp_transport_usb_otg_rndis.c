/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* RNDIS function for the USB OTG device transport. STA and AP Ethernet
 * frames use this function. RPC, serial, HCI, and flow control stay on
 * CDC-ACM in eh_cp_transport_usb_otg_device.c.
 */

#include "eh_cp_transport_usb_otg_priv.h"

#include <assert.h>
#include <string.h>

#include "freertos/task.h"

#include "esp_log.h"
#include "eh_interface.h"
#include "eh_mempool.h"
#include "eh_cp_utils.h"

#if !CONFIG_TINYUSB_NET_MODE_ECM_RNDIS
#error "USB RNDIS requires TinyUSB network mode ECM/RNDIS. Set CONFIG_TINYUSB_NET_MODE_ECM_RNDIS=y if a saved sdkconfig still has Network mode None."
#endif
/* esp_tinyusb's default configuration descriptor emits NCM, not RNDIS.
 * String indexes below match descriptor_str_default with CDC plus the
 * ECM/RNDIS net strings and no MSC, MTP, or vendor class. */
#if CONFIG_TINYUSB_MSC_ENABLED || CONFIG_TINYUSB_MTP_ENABLED || \
		CONFIG_TINYUSB_VENDOR_COUNT || CONFIG_TINYUSB_NET_MODE_NCM
#error "USB OTG RNDIS descriptor only supports CDC-ACM plus RNDIS. Disable TinyUSB MSC, MTP, Vendor, and NCM."
#endif
#if CONFIG_TINYUSB_CDC_COUNT > 1
#error "USB OTG RNDIS descriptor provides one CDC-ACM interface (TINYUSB_CDC_COUNT must be 1)."
#endif
#include "class/net/net_device.h"
#include "device/usbd_pvt.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_wifi.h"

/* 0x4000 | CDC (bit 0) | RNDIS (bit 7). The stock TinyUSB PID does not
 * change when RNDIS is added, so a host that cached CDC-only would not
 * re-read the configuration. */
#define USB_RNDIS_PID          0x4081
#define USB_STRID_CDC          4
#define USB_STRID_NET          5
#define USB_ETH_HDR_LEN        14
#define USB_ETH_SRC_OFF        6
#define USB_RNDIS_CFG_LEN      (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_RNDIS_DESC_LEN)

enum {
	ITF_NUM_RNDIS = 0,
	ITF_NUM_RNDIS_DATA,
	ITF_NUM_CDC,
	ITF_NUM_CDC_DATA,
	ITF_NUM_TOTAL,
};

enum {
	EPNUM_CDC_NOTIF = 1,
	EPNUM_CDC_DATA,
	EPNUM_RNDIS_NOTIF,
	EPNUM_RNDIS_DATA,
};

uint8_t tud_network_mac_address[6];

static const tusb_desc_device_t usb_rndis_dev_desc = {
	.bLength = sizeof(tusb_desc_device_t),
	.bDescriptorType = TUSB_DESC_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = TUSB_CLASS_MISC,
	.bDeviceSubClass = MISC_SUBCLASS_COMMON,
	.bDeviceProtocol = MISC_PROTOCOL_IAD,
	.bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
	.idVendor = TINYUSB_ESPRESSIF_VID,
	.idProduct = USB_RNDIS_PID,
	.bcdDevice = CONFIG_TINYUSB_DESC_BCD_DEVICE,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1,
};

static const uint8_t usb_rndis_fs_cfg[] = {
	TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, USB_RNDIS_CFG_LEN,
			TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
	TUD_RNDIS_DESCRIPTOR(ITF_NUM_RNDIS, USB_STRID_NET, 0x80 | EPNUM_RNDIS_NOTIF, 8,
			EPNUM_RNDIS_DATA, 0x80 | EPNUM_RNDIS_DATA, 64),
	TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, USB_STRID_CDC, 0x80 | EPNUM_CDC_NOTIF, 8,
			EPNUM_CDC_DATA, 0x80 | EPNUM_CDC_DATA, 64),
};

#if TUD_OPT_HIGH_SPEED
static const uint8_t usb_rndis_hs_cfg[] = {
	TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, USB_RNDIS_CFG_LEN,
			TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
	TUD_RNDIS_DESCRIPTOR(ITF_NUM_RNDIS, USB_STRID_NET, 0x80 | EPNUM_RNDIS_NOTIF, 64,
			EPNUM_RNDIS_DATA, 0x80 | EPNUM_RNDIS_DATA, 512),
	TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, USB_STRID_CDC, 0x80 | EPNUM_CDC_NOTIF, 64,
			EPNUM_CDC_DATA, 0x80 | EPNUM_CDC_DATA, 512),
};
#endif

static const char TAG[] = "USB_OTG_DRIVER";

/* Two copies of TinyUSB's single RNDIS RX buffer. The USB callback fills a
 * free slot and returns so the next bulk OUT can be armed. usb_rndis_rx
 * moves the slot into the pool and the priority queue. A full pair leaves
 * the frame in TinyUSB's buffer and does not renew OUT, so the host NAKs. */
#define USB_RNDIS_RX_SLOTS 2

typedef struct {
	uint16_t len;
	bool busy;
	uint8_t data[BUFFER_SIZE];
} usb_rndis_rx_slot_t;

static portMUX_TYPE usb_rndis_rx_mux = portMUX_INITIALIZER_UNLOCKED;
static usb_rndis_rx_slot_t usb_rndis_rx_slot[USB_RNDIS_RX_SLOTS];
static TaskHandle_t usb_rndis_rx_task_handle;
static volatile bool usb_rndis_rx_stop;
#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
static SemaphoreHandle_t usb_rndis_rx_parked;
#endif
static const uint8_t *usb_rndis_rx_held;
static uint16_t usb_rndis_rx_held_len;
static bool usb_rndis_rx_hold;

static uint8_t usb_rndis_ap_mac[6];
static bool usb_rndis_ap_mac_valid;
static bool usb_rndis_ap_mac_hooked;

static void usb_rndis_ap_mac_store(const uint8_t *mac, bool valid)
{
	portENTER_CRITICAL(&usb_rndis_rx_mux);
	if (valid && mac)
		memcpy(usb_rndis_ap_mac, mac, sizeof(usb_rndis_ap_mac));
	usb_rndis_ap_mac_valid = valid;
	portEXIT_CRITICAL(&usb_rndis_rx_mux);
}

static void usb_rndis_ap_mac_refresh(void)
{
	uint8_t mac[6];

	if (esp_wifi_get_mac(WIFI_IF_AP, mac) == ESP_OK)
		usb_rndis_ap_mac_store(mac, true);
}

static void usb_rndis_ap_mac_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
	(void)arg;
	(void)base;
	(void)data;
	if (id == WIFI_EVENT_AP_STOP)
		usb_rndis_ap_mac_store(NULL, false);
	else
		usb_rndis_ap_mac_refresh();
}

/* One RNDIS function carries the active interface. STA and SoftAP
 * follow esp_wifi_get_mode. WIFI_MODE_AP is SoftAP. STA and NULL are
 * STA. WIFI_MODE_APSTA is not a supported route. */
static uint8_t usb_rndis_if_type(const uint8_t *frame)
{
	wifi_mode_t mode = WIFI_MODE_STA;
	uint8_t mac[6];
	bool valid;

	if (esp_wifi_get_mode(&mode) != ESP_OK || mode != WIFI_MODE_APSTA) {
		return mode == WIFI_MODE_AP ? ESP_AP_IF : ESP_STA_IF;
	}

	portENTER_CRITICAL(&usb_rndis_rx_mux);
	valid = usb_rndis_ap_mac_valid;
	if (valid)
		memcpy(mac, usb_rndis_ap_mac, sizeof(mac));
	portEXIT_CRITICAL(&usb_rndis_rx_mux);
	if (!valid) {
		usb_rndis_ap_mac_refresh();
		portENTER_CRITICAL(&usb_rndis_rx_mux);
		valid = usb_rndis_ap_mac_valid;
		if (valid)
			memcpy(mac, usb_rndis_ap_mac, sizeof(mac));
		portEXIT_CRITICAL(&usb_rndis_rx_mux);
	}
	if (valid && memcmp(frame + USB_ETH_SRC_OFF, mac, sizeof(mac)) == 0)
		return ESP_AP_IF;
	return ESP_STA_IF;
}

/* Only the TinyUSB task calls this. The slot stays free until the copy
 * finishes, then busy publishes len and data to the worker. */
static int usb_rndis_rx_claim(const uint8_t *src, uint16_t len)
{
	int slot = -1;

	portENTER_CRITICAL(&usb_rndis_rx_mux);
	for (int i = 0; i < USB_RNDIS_RX_SLOTS; i++) {
		if (!usb_rndis_rx_slot[i].busy) {
			slot = i;
			break;
		}
	}
	portEXIT_CRITICAL(&usb_rndis_rx_mux);
	if (slot < 0)
		return -1;

	memcpy(usb_rndis_rx_slot[slot].data, src, len);
	portENTER_CRITICAL(&usb_rndis_rx_mux);
	usb_rndis_rx_slot[slot].len = len;
	usb_rndis_rx_slot[slot].busy = true;
	portEXIT_CRITICAL(&usb_rndis_rx_mux);
	if (usb_rndis_rx_task_handle)
		xTaskNotifyGive(usb_rndis_rx_task_handle);
	return slot;
}

static int usb_rndis_rx_peek(uint16_t *len)
{
	int slot = -1;

	portENTER_CRITICAL(&usb_rndis_rx_mux);
	for (int i = 0; i < USB_RNDIS_RX_SLOTS; i++) {
		if (usb_rndis_rx_slot[i].busy) {
			slot = i;
			*len = usb_rndis_rx_slot[i].len;
			break;
		}
	}
	portEXIT_CRITICAL(&usb_rndis_rx_mux);
	return slot;
}

static void usb_rndis_rx_release(int slot)
{
	portENTER_CRITICAL(&usb_rndis_rx_mux);
	usb_rndis_rx_slot[slot].busy = false;
	portEXIT_CRITICAL(&usb_rndis_rx_mux);
}

static void usb_rndis_rx_drop_held(void)
{
	portENTER_CRITICAL(&usb_rndis_rx_mux);
	usb_rndis_rx_hold = false;
	usb_rndis_rx_held = NULL;
	usb_rndis_rx_held_len = 0;
	portEXIT_CRITICAL(&usb_rndis_rx_mux);
	if (usb_rndis_rx_task_handle)
		xTaskNotifyGive(usb_rndis_rx_task_handle);
}

static void usb_rndis_rx_submit_held(void *ctx)
{
	const uint8_t *src;
	uint16_t len;
	bool hold;

	(void)ctx;
	portENTER_CRITICAL(&usb_rndis_rx_mux);
	hold = usb_rndis_rx_hold;
	src = usb_rndis_rx_held;
	len = usb_rndis_rx_held_len;
	portEXIT_CRITICAL(&usb_rndis_rx_mux);
	if (!hold || !src || !len)
		return;
	if (usb_rndis_rx_claim(src, len) < 0)
		return;
	portENTER_CRITICAL(&usb_rndis_rx_mux);
	usb_rndis_rx_hold = false;
	usb_rndis_rx_held = NULL;
	usb_rndis_rx_held_len = 0;
	portEXIT_CRITICAL(&usb_rndis_rx_mux);
	tud_network_recv_renew();
}

static void usb_rndis_rx_task(void *arg)
{
	(void)arg;

	while (!usb_rndis_rx_stop) {
		uint16_t len = 0;
		int slot = usb_rndis_rx_peek(&len);
		uint8_t *buf;
		interface_buffer_handle_t buf_handle;
		bool hold;

		if (slot < 0) {
			ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
			continue;
		}
		if (!usb_rx_credits ||
				xSemaphoreTake(usb_rx_credits, pdMS_TO_TICKS(50)) != pdTRUE)
			continue;
		buf = h_usb_buffer_rx_alloc(MEMSET_NOT_REQUIRED);
		if (!buf) {
			usb_rx_credit_release();
			continue;
		}
		if (len > BUFFER_SIZE)
			len = BUFFER_SIZE;
		memcpy(buf, usb_rndis_rx_slot[slot].data, len);
		buf_handle = (interface_buffer_handle_t){0};
		buf_handle.if_type = usb_rndis_if_type(buf);
		buf_handle.payload = buf;
		buf_handle.payload_len = len;
		buf_handle.free_buf_handle = usb_rx_read_done;
		buf_handle.priv_buffer_handle = buf;
#if ESP_PKT_STATS
		if (buf_handle.if_type == ESP_STA_IF)
			pkt_stats.hs_bus_sta_in++;
#endif
		while (!usb_rndis_rx_stop && usb_rx_queue[PRIO_Q_OTHERS]) {
			if (xQueueSend(usb_rx_queue[PRIO_Q_OTHERS], &buf_handle,
					pdMS_TO_TICKS(50)) == pdTRUE) {
				xSemaphoreGive(usb_rx_sem);
				start_rx_data_throttling_if_needed();
				buf_handle.payload = NULL;
				break;
			}
		}
		if (buf_handle.payload)
			usb_rx_read_done(buf);
		usb_rndis_rx_release(slot);
		portENTER_CRITICAL(&usb_rndis_rx_mux);
		hold = usb_rndis_rx_hold;
		portEXIT_CRITICAL(&usb_rndis_rx_mux);
		if (hold && !usb_rndis_rx_stop)
			usbd_defer_func(usb_rndis_rx_submit_held, NULL, false);
	}

	/* Deinit deletes this task once it is parked here. */
#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
	if (usb_rndis_rx_parked)
		xSemaphoreGive(usb_rndis_rx_parked);
#endif
	for (;;)
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
void usb_rndis_rx_stop_task(void)
{
	usb_rndis_rx_stop = true;
	usb_rndis_rx_drop_held();
	if (usb_rndis_ap_mac_hooked) {
		esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_START,
				usb_rndis_ap_mac_event);
		esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STOP,
				usb_rndis_ap_mac_event);
		usb_rndis_ap_mac_hooked = false;
	}
	if (usb_rndis_rx_task_handle && usb_rndis_rx_parked)
		xSemaphoreTake(usb_rndis_rx_parked, portMAX_DELAY);
	if (usb_rndis_rx_task_handle) {
		vTaskDelete(usb_rndis_rx_task_handle);
		usb_rndis_rx_task_handle = NULL;
	}
	if (usb_rndis_rx_parked) {
		vSemaphoreDelete(usb_rndis_rx_parked);
		usb_rndis_rx_parked = NULL;
	}
}
#endif

static SemaphoreHandle_t usb_rndis_tx_lock;
static SemaphoreHandle_t usb_rndis_tx_busy;
static SemaphoreHandle_t usb_rndis_tx_done;

typedef struct {
	const uint8_t *buf;
	uint16_t len;
	esp_err_t result;
	bool pending;
} usb_rndis_tx_t;

static usb_rndis_tx_t usb_rndis_tx;

/* usb_rndis_tx_busy excludes the CP TX task from the TinyUSB task. The
 * sender gives it for one attempt and takes it back before returning, so
 * it is empty while no send is in progress. The copy into TinyUSB's single
 * RNDIS TX buffer runs on the TinyUSB task. */

static void usb_rndis_tx_finish(esp_err_t err)
{
	usb_rndis_tx.pending = false;
	usb_rndis_tx.result = err;
	if (usb_rndis_tx_busy)
		xSemaphoreGive(usb_rndis_tx_busy);
	if (usb_rndis_tx_done)
		xSemaphoreGive(usb_rndis_tx_done);
}

/* Submit the staged frame when the bulk IN is free. Otherwise leave it
 * pending: the NET transfer callback calls this again when that IN, or
 * its zero-length packet, completes. */
static void usb_rndis_tx_try(void)
{
	if (!usb_rndis_tx_busy || xSemaphoreTake(usb_rndis_tx_busy, 0) != pdTRUE)
		return;
	if (!usb_rndis_tx.pending || !usb_rndis_tx.buf) {
		xSemaphoreGive(usb_rndis_tx_busy);
		return;
	}
	if (!tud_network_can_xmit(usb_rndis_tx.len)) {
		xSemaphoreGive(usb_rndis_tx_busy);
		return;
	}
	tud_network_xmit(&usb_rndis_tx, usb_rndis_tx.len);
	usb_rndis_tx_finish(ESP_OK);
}

static void usb_rndis_tx_defer(void *ctx)
{
	(void)ctx;
	usb_rndis_tx_try();
}

/* A bus reset or unplug does not complete the bulk IN. Fail the waiter
 * instead of leaving it blocked until USB_TX_FLUSH_MS. */
static void usb_rndis_tx_abort(esp_err_t err)
{
	if (!usb_rndis_tx_busy || xSemaphoreTake(usb_rndis_tx_busy, 0) != pdTRUE)
		return;
	if (!usb_rndis_tx.pending) {
		xSemaphoreGive(usb_rndis_tx_busy);
		return;
	}
	usb_rndis_tx.buf = NULL;
	usb_rndis_tx_finish(err);
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
	const usb_rndis_tx_t *pkt = ref;
	uint16_t len = arg;

	if (!pkt || !pkt->buf)
		return 0;
	if (len > pkt->len)
		len = pkt->len;
	memcpy(dst, pkt->buf, len);
	return len;
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
	if (usb_rndis_rx_stop)
		return false;
#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
	if (usb_exit_requested)
		return false;
#endif
	if (!src || size < USB_ETH_HDR_LEN || size > BUFFER_SIZE || size > CFG_TUD_NET_MTU) {
		ESP_LOGW(TAG, "rndis rx drop len %u", (unsigned)size);
		return false;
	}
	if (usb_rndis_rx_claim(src, size) >= 0)
		return false;

	/* Both bounce slots are still occupied. Leave this frame in TinyUSB's
	 * RX buffer. Renewing OUT would overwrite it. */
	portENTER_CRITICAL(&usb_rndis_rx_mux);
	usb_rndis_rx_held = src;
	usb_rndis_rx_held_len = size;
	usb_rndis_rx_hold = true;
	portEXIT_CRITICAL(&usb_rndis_rx_mux);
	return true;
}

void tud_network_init_cb(void)
{
}

static void usb_rndis_mac_init(void)
{
	if (esp_read_mac(tud_network_mac_address, ESP_MAC_WIFI_STA) != ESP_OK) {
		memset(tud_network_mac_address, 0, sizeof(tud_network_mac_address));
		tud_network_mac_address[0] = 0x02;
		ESP_LOGW(TAG, "rndis MAC fallback to locally administered");
	}
}

esp_err_t usb_rndis_send(const uint8_t *frame, uint16_t len)
{
	const TickType_t timeout = pdMS_TO_TICKS(USB_TX_FLUSH_MS);
	esp_err_t err;

	if (len > CFG_TUD_NET_MTU) {
		ESP_LOGE(TAG, "rndis frame %u exceeds MTU %u",
				(unsigned)len, (unsigned)CFG_TUD_NET_MTU);
		return ESP_ERR_INVALID_SIZE;
	}
	if (!usb_rndis_tx_lock || !usb_rndis_tx_busy || !usb_rndis_tx_done ||
			xSemaphoreTake(usb_rndis_tx_lock, portMAX_DELAY) != pdTRUE)
		return ESP_ERR_INVALID_STATE;

	usb_rndis_tx.buf = frame;
	usb_rndis_tx.len = len;
	usb_rndis_tx.result = ESP_ERR_TIMEOUT;
	usb_rndis_tx.pending = true;

	/* Drain a token left when a completion raced a previous timeout. */
	xSemaphoreTake(usb_rndis_tx_done, 0);
	xSemaphoreGive(usb_rndis_tx_busy);
	usbd_defer_func(usb_rndis_tx_defer, NULL, false);

	if (xSemaphoreTake(usb_rndis_tx_done, timeout) == pdTRUE) {
		xSemaphoreTake(usb_rndis_tx_busy, portMAX_DELAY);
		err = usb_rndis_tx.result;
		usb_rndis_tx.pending = false;
		usb_rndis_tx.buf = NULL;
	} else {
		/* The bulk IN did not free in time. Take the lock so a completion
		 * cannot copy this buffer after we drop it. If the copy already
		 * finished, result is ESP_OK and the frame was submitted. */
		xSemaphoreTake(usb_rndis_tx_busy, portMAX_DELAY);
		if (usb_rndis_tx.result == ESP_OK) {
			err = ESP_OK;
		} else {
			usb_rndis_tx.pending = false;
			usb_rndis_tx.buf = NULL;
			usb_rndis_tx.result = ESP_ERR_TIMEOUT;
			err = ESP_ERR_TIMEOUT;
		}
		xSemaphoreTake(usb_rndis_tx_done, 0);
	}
	xSemaphoreGive(usb_rndis_tx_lock);
	return err;
}

/* TinyUSB tries application drivers before the built-in table. Claiming
 * the RNDIS interface here lets the bulk IN completion submit the next
 * frame. init does not call netd_init, and reset does not call netd_reset,
 * so the built-in NET driver still runs those once. reset fails a frame
 * that was waiting for the bulk IN and drops a bulk OUT frame held in
 * TinyUSB's RX buffer. */
static void usb_rndis_net_init(void)
{
}

static bool usb_rndis_net_deinit(void)
{
	usb_rndis_rx_drop_held();
	usb_rndis_tx_abort(ESP_ERR_INVALID_STATE);
	return netd_deinit();
}

static void usb_rndis_net_reset(uint8_t rhport)
{
	(void)rhport;
	usb_rndis_rx_drop_held();
	usb_rndis_tx_abort(ESP_ERR_INVALID_STATE);
}

static uint16_t usb_rndis_net_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc,
		uint16_t max_len)
{
	if (!itf_desc || itf_desc->bInterfaceNumber != ITF_NUM_RNDIS)
		return 0;
	return netd_open(rhport, itf_desc, max_len);
}

static bool usb_rndis_net_control_xfer_cb(uint8_t rhport, uint8_t stage,
		tusb_control_request_t const *request)
{
	return netd_control_xfer_cb(rhport, stage, request);
}

static bool usb_rndis_net_xfer_cb(uint8_t rhport, uint8_t ep_addr,
		xfer_result_t result, uint32_t xferred_bytes)
{
	bool ok = netd_xfer_cb(rhport, ep_addr, result, xferred_bytes);

	usb_rndis_tx_try();
	return ok;
}

static const usbd_class_driver_t usb_rndis_net_driver = {
	.name = "EH-RNDIS",
	.init = usb_rndis_net_init,
	.deinit = usb_rndis_net_deinit,
	.reset = usb_rndis_net_reset,
	.open = usb_rndis_net_open,
	.control_xfer_cb = usb_rndis_net_control_xfer_cb,
	.xfer_cb = usb_rndis_net_xfer_cb,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
	if (driver_count)
		*driver_count = 1;
	return &usb_rndis_net_driver;
}

const tusb_desc_device_t *usb_rndis_device_descriptor(void)
{
	return &usb_rndis_dev_desc;
}

const uint8_t *usb_rndis_fs_config(void)
{
	return usb_rndis_fs_cfg;
}

#if TUD_OPT_HIGH_SPEED
const uint8_t *usb_rndis_hs_config(void)
{
	return usb_rndis_hs_cfg;
}
#endif

void usb_rndis_prepare(void)
{
	usb_rndis_mac_init();
	usb_rndis_rx_stop = false;
	usb_rndis_rx_hold = false;
	usb_rndis_rx_held = NULL;
	usb_rndis_rx_held_len = 0;
	memset(usb_rndis_rx_slot, 0, sizeof(usb_rndis_rx_slot));
	{
		esp_err_t hook = esp_event_handler_register(WIFI_EVENT,
				WIFI_EVENT_AP_START, usb_rndis_ap_mac_event, NULL);
		if (hook == ESP_OK)
			hook = esp_event_handler_register(WIFI_EVENT,
					WIFI_EVENT_AP_STOP, usb_rndis_ap_mac_event, NULL);
		if (hook == ESP_OK) {
			usb_rndis_ap_mac_hooked = true;
		} else {
			esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_START,
					usb_rndis_ap_mac_event);
			esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STOP,
					usb_rndis_ap_mac_event);
			ESP_LOGW(TAG, "rndis AP MAC event hook failed: %s",
					esp_err_to_name(hook));
		}
	}
#if EH_CP_FEAT_HOST_PS_UNLOAD_BUS_WHILE_SLEEPING
	if (!usb_rndis_rx_parked)
		usb_rndis_rx_parked = xSemaphoreCreateBinary();
	assert(usb_rndis_rx_parked != NULL);
#endif
	if (usb_rndis_rx_task_handle == NULL)
		assert(xTaskCreate(usb_rndis_rx_task, "usb_rndis_rx",
				EH_CP_TASK_STACK_SIZE, NULL,
				EH_CP_TASK_PRIO_DEFAULT,
				&usb_rndis_rx_task_handle) == pdTRUE);
	usb_rndis_tx_lock = xSemaphoreCreateMutex();
	usb_rndis_tx_busy = xSemaphoreCreateBinary();
	usb_rndis_tx_done = xSemaphoreCreateBinary();
	assert(usb_rndis_tx_lock != NULL);
	assert(usb_rndis_tx_busy != NULL);
	assert(usb_rndis_tx_done != NULL);
}

void usb_rndis_delete_tx_semaphores(void)
{
	if (usb_rndis_tx_lock) {
		vSemaphoreDelete(usb_rndis_tx_lock);
		usb_rndis_tx_lock = NULL;
	}
	if (usb_rndis_tx_busy) {
		vSemaphoreDelete(usb_rndis_tx_busy);
		usb_rndis_tx_busy = NULL;
	}
	if (usb_rndis_tx_done) {
		vSemaphoreDelete(usb_rndis_tx_done);
		usb_rndis_tx_done = NULL;
	}
}
