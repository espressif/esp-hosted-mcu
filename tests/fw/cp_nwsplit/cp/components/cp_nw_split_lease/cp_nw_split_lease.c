/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

/* Test-only: changes or drops the CP lease while the station stays associated,
 * which the esp-emu DHCP server never does (fixed address, 24 h lease), or
 * drops and restores the link from CP code. Run by the "lease" console command. */

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "eh_cp.h"
#include "eh_cp_core.h"
#include "eh_cp_feat_wifi_event.h"

static const char *TAG = "cp_nw_split_lease";

#define BIT_DISC BIT0
#define BIT_GOT  BIT1
static EventGroupHandle_t s_ev;

typedef enum {
	LEASE_CHANGE, LEASE_LOSE, LEASE_DEINIT_INIT, LEASE_RECONNECT, LEASE_DROP, LEASE_CP_CONNECT,
	LEASE_SWITCH, LEASE_CP_CONNECT_APSTA
} lease_action_t;

static struct {
	lease_action_t action;
	int hold_s;
	int cycles;
} s_req;

/* Waits for the CP station FSM's own disconnect publish (it is then IDLE), so
 * the local connect is not taken for one on a live link. The same config is set
 * again, as a CP-side "set credentials" command does. */
static void reconnect_cycles(void)
{
	wifi_config_t cfg = {0};

	for (int c = 1; c <= s_req.cycles; c++) {
		xEventGroupClearBits(s_ev, BIT_DISC | BIT_GOT);
		ESP_LOGI(TAG, "[lease] reconnect %d disconnect rc=%d", c, (int)esp_wifi_disconnect());
		xEventGroupWaitBits(s_ev, BIT_DISC, pdTRUE, pdTRUE, pdMS_TO_TICKS(10000));
		esp_wifi_get_config(WIFI_IF_STA, &cfg);
		esp_wifi_set_config(WIFI_IF_STA, &cfg);
		ESP_LOGI(TAG, "[lease] reconnect %d connect rc=%d", c, (int)esp_wifi_connect());
		EventBits_t b = xEventGroupWaitBits(s_ev, BIT_GOT, pdTRUE, pdTRUE, pdMS_TO_TICKS(30000));
		ESP_LOGI(TAG, "[lease] reconnect %d %s", c, (b & BIT_GOT) ? "got ip" : "no ip");
		vTaskDelay(pdMS_TO_TICKS(s_req.hold_s * 1000));
	}
}

static void lease_task(void *arg)
{
	(void)arg;
	esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	esp_netif_ip_info_t ip = {0};

	switch (s_req.action) {
	case LEASE_CHANGE:
		esp_netif_dhcpc_stop(n);
		esp_netif_str_to_ip4("192.168.4.3", &ip.ip);
		esp_netif_str_to_ip4("255.255.255.0", &ip.netmask);
		esp_netif_str_to_ip4("192.168.4.1", &ip.gw);
		ESP_LOGI(TAG, "[lease] change rc=%d", (int)esp_netif_set_ip_info(n, &ip));
		break;
	case LEASE_LOSE:
		ESP_LOGI(TAG, "[lease] lose rc=%d", (int)esp_netif_dhcpc_stop(n));
		vTaskDelay(pdMS_TO_TICKS(s_req.hold_s * 1000));
		ESP_LOGI(TAG, "[lease] restore rc=%d", (int)esp_netif_dhcpc_start(n));
		break;
	case LEASE_DEINIT_INIT:
		ESP_LOGI(TAG, "[lease] deinit rc=%d", (int)eh_cp_deinit());
		vTaskDelay(pdMS_TO_TICKS(s_req.hold_s * 1000));
		ESP_LOGI(TAG, "[lease] init rc=%d", (int)eh_cp_init());
		break;
	case LEASE_RECONNECT:
		reconnect_cycles();
		break;
	case LEASE_DROP:            /* a drop the host app did not ask for; no CP reconnect */
		ESP_LOGI(TAG, "[lease] drop rc=%d", (int)esp_wifi_disconnect());
		break;
	case LEASE_CP_CONNECT:      /* CP code takes the radio: start + connect, stored config */
		ESP_LOGI(TAG, "[lease] cp_connect start rc=%d", (int)esp_wifi_start());
		vTaskDelay(pdMS_TO_TICKS(500));
		ESP_LOGI(TAG, "[lease] cp_connect connect rc=%d", (int)esp_wifi_connect());
		break;
	case LEASE_SWITCH: {        /* CP code moves its link to another config: the AP's BSSID pinned */
		wifi_config_t cfg = {0};
		wifi_ap_record_t ap = {0};

		esp_wifi_get_config(WIFI_IF_STA, &cfg);
		if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
			memcpy(cfg.sta.bssid, ap.bssid, sizeof(cfg.sta.bssid));
			cfg.sta.bssid_set = true;
		}
		ESP_LOGI(TAG, "[lease] switch set_config rc=%d", (int)esp_wifi_set_config(WIFI_IF_STA, &cfg));
		ESP_LOGI(TAG, "[lease] switch connect rc=%d", (int)esp_wifi_connect());
		break;
	}
	case LEASE_CP_CONNECT_APSTA:    /* as cp_connect, keeping a soft-AP the host runs */
		ESP_LOGI(TAG, "[lease] cp_connect_apsta mode rc=%d", (int)esp_wifi_set_mode(WIFI_MODE_APSTA));
		ESP_LOGI(TAG, "[lease] cp_connect_apsta start rc=%d", (int)esp_wifi_start());
		vTaskDelay(pdMS_TO_TICKS(500));
		ESP_LOGI(TAG, "[lease] cp_connect_apsta connect rc=%d", (int)esp_wifi_connect());
		break;
	}
	vTaskDelete(NULL);
}

/* lease <change|lose|deinit_init|reconnect|drop|cp_connect|switch|cp_connect_apsta> [hold_s] [cycles] */
static int lease_cmd(int argc, char *argv[])
{
	static const char *const names[] = { "change", "lose", "deinit_init", "reconnect", "drop",
	                                     "cp_connect", "switch", "cp_connect_apsta" };

	if (argc < 2)
		return 1;
	for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
		if (!strcmp(argv[1], names[i])) {
			s_req.action = (lease_action_t)i;
			s_req.hold_s = argc > 2 ? atoi(argv[2]) : 20;
			s_req.cycles = argc > 3 ? atoi(argv[3]) : 1;
			return xTaskCreate(lease_task, "cp_lease", 4096, NULL, 5, NULL) == pdPASS ? 0 : 1;
		}
	}
	return 1;
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
	(void)arg; (void)base; (void)id; (void)data;
	xEventGroupSetBits(s_ev, BIT_GOT);
}

static void on_disc(void *arg, esp_event_base_t base, int32_t id, void *data)
{
	(void)arg; (void)base; (void)id; (void)data;
	xEventGroupSetBits(s_ev, BIT_DISC);
}

/* Register once: this runs from the feature walk, which a CP init repeats. */
esp_err_t cp_nw_split_lease_init(void);
esp_err_t cp_nw_split_lease_init(void)
{
	static bool done;
	const esp_console_cmd_t cmd = {
		.command = "lease",
		.help = "TEST: lease <change|lose|deinit_init|reconnect|drop|cp_connect|switch|cp_connect_apsta> [hold_s] [cycles]",
		.func = lease_cmd,
	};

	if (done)
		return ESP_OK;
	done = true;
	s_ev = xEventGroupCreate();
	esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL);
	esp_event_handler_register(EH_CP_FEAT_WIFI_EVENT, EH_CP_FEAT_WIFI_EVT_STA_DISCONNECTED_FROM_AP,
	                           on_disc, NULL);
	esp_console_cmd_register(&cmd);
	return ESP_OK;
}

static esp_err_t cp_lease_noop(void) { return ESP_OK; }

EH_CP_FEAT_REGISTER(cp_nw_split_lease_init, cp_lease_noop, "cp_nw_split_lease",
                    tskNO_AFFINITY, 399);
