/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "eh_cp_master_config.h"
#include "esp_event.h"

#if EH_CP_FEAT_WIFI_READY
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "esp_private/wifi.h"
#include "esp_check.h"
#include "eh_cp.h"
#include "eh_cp_event.h"
#include "eh_cp_feat_wifi_event.h"
#include "eh_cp_feat_wifi.h"
#include "eh_cp_feat_wifi_event_publisher.h"
#include "eh_check.h"
#include "eh_cp_core.h"
#include "eh_cp_feat_wifi_sta_fsm.h"

ESP_EVENT_DEFINE_BASE(EH_CP_FEAT_WIFI_EVENT);
ESP_EVENT_DEFINE_BASE(EH_CP_WIFI_FLUSH_EVENT);

/* Event-loop hops of a station report: IDF event -> station FSM -> RPC event,
 * and in network split -> nw_split status -> RPC event. */
#define REPORT_HOPS 3

static SemaphoreHandle_t s_flush_mtx;
static SemaphoreHandle_t s_flush_mark;
static bool s_flush_on;

static const char* TAG = "ehcp_event_client";

#define MAC2STR(a)                  (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR                      "%02x:%02x:%02x:%02x:%02x:%02x"

/* This feature is the only caller for the station; other STA registrations
 * (IDF's default netif glue on STA_CONNECTED) are swallowed. */
esp_err_t __real_esp_wifi_internal_reg_rxcb(wifi_interface_t ifx, wifi_rxcb_t fn);
esp_err_t __wrap_esp_wifi_internal_reg_rxcb(wifi_interface_t ifx, wifi_rxcb_t fn);
esp_err_t __wrap_esp_wifi_internal_reg_rxcb(wifi_interface_t ifx, wifi_rxcb_t fn)
{
	if (ifx == WIFI_IF_STA) {
		return ESP_OK;
	}
	return __real_esp_wifi_internal_reg_rxcb(ifx, fn);
}

/* The driver keeps the pointer it was given; the registry may change while the
 * link is up (network split), so look it up per frame. */
static esp_err_t wifi_rx(eh_if_type_t if_type, void *buf, uint16_t len, void *eb)
{
	eh_cp_traffic_cb_t cb = eh_cp_get_traffic_tx_cb(if_type);

	if (!cb) {
		esp_wifi_internal_free_rx_buffer(eb);
		return ESP_OK;
	}
	return cb(buf, len, eb);
}

static esp_err_t sta_rx(void *buf, uint16_t len, void *eb)
{
	return wifi_rx(ESP_STA_IF, buf, len, eb);
}

static esp_err_t ap_rx(void *buf, uint16_t len, void *eb)
{
	return wifi_rx(ESP_AP_IF, buf, len, eb);
}

void eh_cp_feat_wifi_link(wifi_interface_t ifx, bool up)
{
	wifi_rxcb_t rx = ifx == WIFI_IF_STA ? sta_rx : ap_rx;

	(void)__real_esp_wifi_internal_reg_rxcb(ifx, up ? rx : NULL);
}

/* WiFi Event Handlers */
static void station_event_handler(void *arg, esp_event_base_t event_base,
		int32_t event_id, void *event_data)
{
	ESP_LOGD(TAG, "WIFI_EVENT rx on CP: event_id=%ld", (long)event_id);

	eh_cp_sta_fsm_wifi_event(event_id, event_data);
}

/* event handler for starting softap */
static void softap_event_handler(void *arg, esp_event_base_t event_base,
		int32_t event_id, void *event_data)
{
	/* Please make sure that this callback function is as small as possible */
	if (event_id == WIFI_EVENT_AP_STACONNECTED) {

		wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
		ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
				MAC2STR(event->mac), event->aid);
		esp_event_post(
			EH_CP_FEAT_WIFI_EVENT,
			EH_CP_FEAT_WIFI_EVT_STA_CONNECTED_TO_ESP_SOFTAP,
			event, sizeof(wifi_event_ap_staconnected_t),
			EH_CP_TIMEOUT_IN_MSEC(EH_CP_EVT_DFLT_TIMEOUT));

	} else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {

			wifi_event_ap_stadisconnected_t *event =
			(wifi_event_ap_stadisconnected_t *) event_data;

			ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
				MAC2STR(event->mac), event->aid);
			esp_event_post(
				EH_CP_FEAT_WIFI_EVENT,
				EH_CP_FEAT_WIFI_EVT_STA_DISCONNECTED_FROM_ESP_SOFTAP,
				event, sizeof(wifi_event_ap_stadisconnected_t),
				EH_CP_TIMEOUT_IN_MSEC(EH_CP_EVT_DFLT_TIMEOUT));
	} else {
		eh_cp_ap_fsm_wifi_event(event_id);     /* AP_START / AP_STOP */
	}
}

/* event handler for scan list of available APs */
static void ap_scan_list_event_handler(void *arg, esp_event_base_t event_base,
		int32_t event_id, void *event_data)
{
	/* Please make sure that this callback function is as small as possible */
	if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_SCAN_DONE)) {
		esp_event_post(
			EH_CP_FEAT_WIFI_EVENT,
			EH_CP_FEAT_WIFI_EVT_SCAN_DONE,
			event_data, sizeof(wifi_event_sta_scan_done_t),
			EH_CP_TIMEOUT_IN_MSEC(EH_CP_EVT_DFLT_TIMEOUT));
	}
}

/* register station connect/disconnect events */
static void eh_cp_wifi_event_publisher_start_station_events(void)
{
	int ret;
	ret = esp_event_handler_register(WIFI_EVENT,
				WIFI_EVENT_STA_CONNECTED, &station_event_handler, NULL);
	ret |= esp_event_handler_register(WIFI_EVENT,
				WIFI_EVENT_STA_DISCONNECTED, &station_event_handler, NULL);
	ret |= esp_event_handler_register(WIFI_EVENT,
				WIFI_EVENT_STA_START, &station_event_handler, NULL);
	ret |= esp_event_handler_register(WIFI_EVENT,
				WIFI_EVENT_STA_STOP, &station_event_handler, NULL);

	if (ret) {
		ESP_LOGW(TAG, "Wi-Fi station events not registered on default event loop");
	}

}

static void eh_cp_wifi_event_publisher_stop_station_events(void)
{
	esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &station_event_handler);
	esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &station_event_handler);
	esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_START, &station_event_handler);
	esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_STOP, &station_event_handler);
}

static void eh_cp_wifi_event_publisher_start_softap_events(void)
{
	int ret;
	ret = esp_event_handler_register(WIFI_EVENT,
				WIFI_EVENT_AP_STACONNECTED, &softap_event_handler, NULL);
	ret |= esp_event_handler_register(WIFI_EVENT,
				WIFI_EVENT_AP_STADISCONNECTED, &softap_event_handler, NULL);
	ret |= esp_event_handler_register(WIFI_EVENT,
				WIFI_EVENT_AP_START, &softap_event_handler, NULL);
	ret |= esp_event_handler_register(WIFI_EVENT,
				WIFI_EVENT_AP_STOP, &softap_event_handler, NULL);

	if (ret) {
		ESP_LOGW(TAG, "Wi-Fi SoftAP events not registered on default event loop");
	}
}

static void eh_cp_wifi_event_publisher_stop_softap_events(void)
{
	esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &softap_event_handler);
	esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &softap_event_handler);
	esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_START, &softap_event_handler);
	esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_AP_STOP, &softap_event_handler);
}

static void eh_cp_wifi_event_publisher_start_ap_scan_list_events(void)
{
	esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &ap_scan_list_event_handler, NULL);
}

static void eh_cp_wifi_event_publisher_stop_ap_scan_list_events(void)
{
	esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &ap_scan_list_event_handler);
}

/* The host (re)connected (boot, wake, CP re-handshake): its view of the station
 * comes from the FSM; network split follows with the lease. */
static void on_host_connected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
	(void)arg;
	(void)base;
	(void)id;
	(void)data;
	eh_cp_feat_wifi_resync_host();
}

static void on_flush_mark(void *arg, esp_event_base_t base, int32_t id, void *data)
{
	(void)arg;
	(void)base;
	(void)id;
	(void)data;
	xSemaphoreGive(s_flush_mark);
}

void eh_cp_feat_wifi_reports_flush(void)
{
	if (!s_flush_on || strcmp(pcTaskGetName(NULL), "sys_evt") == 0) {
		return;
	}
	xSemaphoreTake(s_flush_mtx, portMAX_DELAY);
	for (int hop = 0; hop < REPORT_HOPS; hop++) {
		(void)xSemaphoreTake(s_flush_mark, 0);       /* a mark from a timed-out round */
		if (esp_event_post(EH_CP_WIFI_FLUSH_EVENT, 0, NULL, 0, pdMS_TO_TICKS(10)) != ESP_OK ||
		    xSemaphoreTake(s_flush_mark, pdMS_TO_TICKS(100)) != pdTRUE) {
			ESP_LOGW(TAG, "event loop busy: reply may overtake a report");
			break;
		}
	}
	xSemaphoreGive(s_flush_mtx);
}

/* Initialize event trigger */
esp_err_t eh_cp_wifi_event_publisher_init(void)
{
	ESP_LOGI(TAG, "Initializing event trigger");

	if (!s_flush_mark) {
		s_flush_mtx = xSemaphoreCreateMutex();
		s_flush_mark = xSemaphoreCreateBinary();
		if (!s_flush_mtx || !s_flush_mark) {
			return ESP_ERR_NO_MEM;
		}
	}
	if (esp_event_handler_register(EH_CP_WIFI_FLUSH_EVENT, 0, on_flush_mark, NULL) != ESP_OK) {
		return ESP_FAIL;
	}
	s_flush_on = true;

	/* Handlers first: the FSM then syncs to a radio already running. */
	eh_cp_wifi_event_publisher_start_station_events();
	eh_cp_wifi_event_publisher_start_softap_events();
	eh_cp_wifi_event_publisher_start_ap_scan_list_events();
	esp_err_t rc = eh_cp_sta_fsm_init();
	if (rc == ESP_OK) {
		rc = eh_cp_ap_fsm_init();
	}
	if (rc != ESP_OK) {
		return rc;
	}
	rc = esp_event_handler_register(EH_CP_EVENT, EH_CP_EVT_ESP_INIT, on_host_connected, NULL);
	if (rc != ESP_OK) {
		return rc;
	}

	ESP_LOGI(TAG, "Event trigger initialized");
	return ESP_OK;
}

/* Deinitialize event trigger */
esp_err_t eh_cp_wifi_event_publisher_deinit(void)
{
	ESP_LOGI(TAG, "Deinitializing event trigger");

	/* Unregister WiFi event handlers */
	eh_cp_wifi_event_publisher_stop_station_events();
	eh_cp_wifi_event_publisher_stop_softap_events();
	eh_cp_wifi_event_publisher_stop_ap_scan_list_events();
	(void)esp_event_handler_unregister(EH_CP_EVENT, EH_CP_EVT_ESP_INIT, on_host_connected);
	s_flush_on = false;
	(void)esp_event_handler_unregister(EH_CP_WIFI_FLUSH_EVENT, 0, on_flush_mark);
	eh_cp_ap_fsm_deinit();
	eh_cp_sta_fsm_deinit();

	ESP_LOGI(TAG, "Event trigger deinitialized");
	return ESP_OK;
}
#endif /* EH_CP_FEAT_WIFI_READY */
