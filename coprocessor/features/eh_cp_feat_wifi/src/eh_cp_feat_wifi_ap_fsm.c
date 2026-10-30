/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* The CP soft-AP: OFF / ON from IDF's AP_START / AP_STOP. ON links the AP RX to
 * core and is published to the host; SYNC takes an AP the app started before the
 * feature. */
#include "eh_cp_master_config.h"

#if EH_CP_FEAT_WIFI_READY
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "eh_cp.h"
#include "eh_cp_event.h"
#include "eh_cp_feat_wifi.h"
#include "eh_cp_feat_wifi_event.h"
#include "eh_fsm.h"
#include "eh_cp_feat_wifi_sta_fsm.h"

static const char *TAG = "ehcp_ap_fsm";

enum { ST_OFF, ST_ON, ST_COUNT };
enum { EV_STARTED, EV_STOPPED, EV_SYNC, EV_DEINIT, EV_COUNT };

static eh_fsm_t s_fsm;
static SemaphoreHandle_t s_mtx;     /* created once, kept across deinit */

static void publish(int32_t id)
{
	if (esp_event_post(EH_CP_FEAT_WIFI_EVENT, id, NULL, 0,
			EH_CP_TIMEOUT_IN_MSEC(EH_CP_EVT_DFLT_TIMEOUT)) != ESP_OK) {
		ESP_LOGE(TAG, "AP event %d lost", (int)id);
	}
}

static void ap_on_entry(eh_fsm_t *m)
{
	(void)m;
	eh_cp_feat_wifi_link(WIFI_IF_AP, true);
}

static void ap_on_exit(eh_fsm_t *m)
{
	(void)m;
	eh_cp_feat_wifi_link(WIFI_IF_AP, false);
}

static int go_on(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	return ST_ON;
}

static int go_off(eh_fsm_t *m, const void *d)
{
	(void)m;
	(void)d;
	return ST_OFF;
}

/* After exit unlinked the AP RX; a DEINIT publishes nothing (the feature goes). */
static void ap_edges(eh_fsm_t *m, int from, int to)
{
	if (to == ST_ON) {
		publish(EH_CP_FEAT_WIFI_EVT_SOFTAP_STARTED);
	} else if (from == ST_ON && m->ev != EV_DEINIT) {
		ESP_LOGI(TAG, "softap stopped");
		publish(EH_CP_FEAT_WIFI_EVT_SOFTAP_STOPPED);
	}
}

#define I eh_fsm_ignore

_Static_assert(EV_COUNT == 4, "one column per event");

static const eh_fsm_fn_t s_table[ST_COUNT][EV_COUNT] = {
/*              STARTED  STOPPED  SYNC    DEINIT */
[ST_OFF] = {    go_on,   I,       go_on,  I      },
[ST_ON]  = {    I,       go_off,  I,      go_off },
};

#undef I

static const eh_fsm_state_t s_states[ST_COUNT] = {
	[ST_OFF] = { "OFF" }, [ST_ON] = { "ON", ap_on_entry, ap_on_exit },
};

static const char *const s_ev_names[EV_COUNT] = { "STARTED", "STOPPED", "SYNC", "DEINIT" };

static const eh_fsm_def_t s_def = {
	.name = "cp_ap", .n_states = ST_COUNT, .n_events = EV_COUNT,
	.states = s_states, .table = &s_table[0][0], .ev_names = s_ev_names,
	.on_transit = ap_edges,
};

static void dispatch(int ev)
{
	if (!s_mtx) {
		return;
	}
	xSemaphoreTake(s_mtx, portMAX_DELAY);
	if (s_fsm.def) {
		eh_fsm_dispatch(&s_fsm, ev, NULL);
	}
	xSemaphoreGive(s_mtx);
}

void eh_cp_ap_fsm_wifi_event(int32_t id)
{
	if (id == WIFI_EVENT_AP_START) {
		dispatch(EV_STARTED);
	} else if (id == WIFI_EVENT_AP_STOP) {
		dispatch(EV_STOPPED);
	}
}

esp_err_t eh_cp_ap_fsm_init(void)
{
	wifi_mode_t mode;
	int8_t pwr;

	if (!s_mtx && !(s_mtx = xSemaphoreCreateMutex())) {
		return ESP_ERR_NO_MEM;
	}
	xSemaphoreTake(s_mtx, portMAX_DELAY);
	if (!s_fsm.def) {
		(void)eh_fsm_init(&s_fsm, &s_def, ST_OFF);
	}
	xSemaphoreGive(s_mtx);
	/* IDF has no is-started query; get_max_tx_power returns NOT_STARTED until then. */
	if (esp_wifi_get_mode(&mode) == ESP_OK && (mode & WIFI_MODE_AP) &&
	    esp_wifi_get_max_tx_power(&pwr) == ESP_OK) {
		dispatch(EV_SYNC);
	}
	return s_fsm.def ? ESP_OK : ESP_FAIL;
}

void eh_cp_ap_fsm_deinit(void)
{
	if (!s_mtx) {
		return;
	}
	xSemaphoreTake(s_mtx, portMAX_DELAY);
	if (s_fsm.def) {
		eh_fsm_dispatch(&s_fsm, EV_DEINIT, NULL);
		s_fsm.def = NULL;
	}
	xSemaphoreGive(s_mtx);
}

bool eh_cp_feat_wifi_is_softap_started(void)
{
	return s_fsm.def && s_fsm.state == ST_ON;
}
#endif /* EH_CP_FEAT_WIFI_READY */
