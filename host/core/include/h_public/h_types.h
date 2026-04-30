/*
 * ESP-Hosted Host Core — Platform-Independent Base Types
 *
 * This header defines the error code system, common types, and Wi-Fi mode/enum
 * replacements for all ESP-IDF-specific types. It is the first header included
 * by every core layer source file and is available to applications.
 */

#ifndef H_TYPES_H
#define H_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── Error Codes ──
 * POSIX errno semantics: zero = success, negative = error.
 * Port layer translates platform-native codes (pdTRUE/-EAGAIN/...) into these.
 * Core layer MUST NOT check platform-native return values directly. */
typedef int h_err_t;

#define H_OK                0
#define H_FAIL             -1
#define H_ERR_INVALID_ARG  -2
#define H_ERR_NO_MEM       -3
#define H_ERR_TIMEOUT      -4
#define H_ERR_NOT_SUP      -5
#define H_ERR_INVALID_STATE -6
#define H_ERR_BUSY         -7

/* ── MAC Address ── */
typedef uint8_t h_mac_addr_t[6];

/* ── Opaque Handles ──
 * Core layer never dereferences these; port implementations cast to their
 * platform-specific types internally. */
typedef void* h_thread_t;
typedef void* h_mutex_t;
typedef void* h_queue_t;
typedef void* h_semaphore_t;
typedef void* h_timer_t;

/* ── Wi-Fi Modes ── */
typedef enum {
    H_WIFI_MODE_NULL = 0,
    H_WIFI_MODE_STA,
    H_WIFI_MODE_AP,
    H_WIFI_MODE_APSTA,
    H_WIFI_MODE_MAX
} h_wifi_mode_t;

/* ── Wi-Fi Interface ── */
typedef enum {
    H_WIFI_IF_STA = 0,
    H_WIFI_IF_AP,
    H_WIFI_IF_NAN,
    H_WIFI_IF_MAX
} h_wifi_interface_t;

/* ── Wi-Fi Bandwidth ── */
typedef enum {
    H_WIFI_BW_20MHZ = 0,
    H_WIFI_BW_40MHZ,
    H_WIFI_BW_HT20 = H_WIFI_BW_20MHZ,  /* alias */
    H_WIFI_BW_HT40 = H_WIFI_BW_40MHZ,
} h_wifi_bandwidth_t;

/* ── Wi-Fi Power Save ── */
typedef enum {
    H_WIFI_PS_NONE = 0,
    H_WIFI_PS_MIN_MODEM,
    H_WIFI_PS_MAX_MODEM,
} h_wifi_ps_type_t;

/* ── Event Base ── */
typedef enum {
    H_EVENT_WIFI = 0,
    H_EVENT_IP,
    H_EVENT_HOSTED,
    H_EVENT_MAX
} h_event_base_t;

/* ── Event Handler Signature ── */
typedef void (*h_event_handler_t)(void *event_data, size_t event_data_size,
                                  void *user_ctx);

#endif /* H_TYPES_H */
