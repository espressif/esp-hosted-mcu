/* host/core/include/h_public/h_event.h */
#ifndef H_EVENT_H
#define H_EVENT_H

#include "h_types.h"
#include "h_wifi_types.h"

/**
 * @brief ESP-Hosted Wi-Fi Event IDs
 * Base: H_EVENT_WIFI
 */
#define H_EVENT_WIFI_READY              0   /**< Wi-Fi stack is initialized and ready */
#define H_EVENT_WIFI_SCAN_DONE          1   /**< Scan operation finished. Data: h_wifi_scan_done_event_t */
#define H_EVENT_WIFI_STA_START          2   /**< Station mode started */
#define H_EVENT_WIFI_STA_STOP           3   /**< Station mode stopped */
#define H_EVENT_WIFI_STA_CONNECTED      4   /**< Station connected to AP. Data: h_wifi_event_sta_connected_t */
#define H_EVENT_WIFI_STA_DISCONNECTED   5   /**< Station disconnected. Data: h_wifi_event_sta_disconnected_t */
#define H_EVENT_WIFI_AP_START           6   /**< SoftAP mode started */
#define H_EVENT_WIFI_AP_STOP            7   /**< SoftAP mode stopped */
#define H_EVENT_WIFI_AP_STACONNECTED    8   /**< A station connected to SoftAP. Data: h_wifi_event_ap_staconnected_t */
#define H_EVENT_WIFI_AP_STADISCONNECTED 9   /**< A station disconnected from SoftAP. Data: h_wifi_event_ap_stadisconnected_t */
#define H_EVENT_WIFI_STA_BEACON_TIMEOUT 10  /**< Beacon timeout observed in STA mode */
#define H_EVENT_WIFI_STA_AUTH_TIMEOUT   11  /**< Auth timeout observed in STA mode */

/**
 * @brief ESP-Hosted Framework / Hosted Component Event IDs
 * Base: H_EVENT_HOSTED
 */
#define H_EVENT_HOSTED_CP_INIT           0  /**< Co-processor finished boot-up and handshake. Data: h_fw_version_t (or legacy init struct) */
#define H_EVENT_HOSTED_CP_HEARTBEAT      1  /**< Periodic heartbeat from CP. Data: uint32_t count */
#define H_EVENT_HOSTED_TRANSPORT_FAILURE 2  /**< Physical transport (SPI/SDIO) critical failure */
#define H_EVENT_HOSTED_TRANSPORT_UP      3  /**< Base transport is up and ready for RPC */
#define H_EVENT_HOSTED_TRANSPORT_DOWN    4  /**< Transport is deinitialized or lost */
#define H_EVENT_HOSTED_MEM_MONITOR       5  /**< Memory monitor update from CP */

/**
 * @brief ESP-Hosted IP Layer Event IDs
 * Base: H_EVENT_IP
 */
#define H_EVENT_IP_STA_GOT_IP           0  /**< Station got IPv4 address */
#define H_EVENT_IP_STA_LOST_IP          1  /**< Station lost IPv4 address */
#define H_EVENT_IP_AP_STA_IP_ASSIGNED   2  /**< SoftAP assigned IP to a station */

/* ── Event Registration API (application-facing) ── */
#include "h_wrapper.h"  /* h_event_register / h_event_unregister */

#endif /* H_EVENT_H */
