/* host/core/include/h_public/h_event.h */
#ifndef H_EVENT_H
#define H_EVENT_H

#include "h_types.h"
#include "h_wifi_types.h"

/* ── Wi-Fi Event IDs ──
 * Sub-IDs for H_EVENT_WIFI base. Replaces WIFI_EVENT_* in ESP-IDF. */
#define H_EVENT_WIFI_READY              0
#define H_EVENT_WIFI_SCAN_DONE          1
#define H_EVENT_WIFI_STA_START          2
#define H_EVENT_WIFI_STA_STOP           3
#define H_EVENT_WIFI_STA_CONNECTED      4
#define H_EVENT_WIFI_STA_DISCONNECTED   5
#define H_EVENT_WIFI_AP_START           6
#define H_EVENT_WIFI_AP_STOP            7
#define H_EVENT_WIFI_AP_STACONNECTED    8
#define H_EVENT_WIFI_AP_STADISCONNECTED 9
#define H_EVENT_WIFI_STA_BEACON_TIMEOUT 10
#define H_EVENT_WIFI_STA_AUTH_TIMEOUT   11

/* ── IP Event IDs ── */
#define H_EVENT_IP_STA_GOT_IP           0
#define H_EVENT_IP_STA_LOST_IP          1
#define H_EVENT_IP_AP_STA_IP_ASSIGNED   2

/* ── Event Registration API (application-facing) ── */
#include "h_wrapper.h"  /* h_event_register / h_event_unregister */

#endif /* H_EVENT_H */
