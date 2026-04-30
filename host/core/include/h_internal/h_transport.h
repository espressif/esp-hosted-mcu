/* host/core/include/h_internal/h_transport.h */
#ifndef H_TRANSPORT_H
#define H_TRANSPORT_H

#include "h_types.h"
#include <stdint.h>

/* Frame types from common/transport/esp_hosted_header.h */
#define ESP_STA_IF     1
#define ESP_AP_IF      2
#define ESP_SERIAL_IF  3
#define ESP_HCI_IF     4
#define ESP_PRIV_IF    5
#define ESP_TEST_IF    6
#define ESP_MAX_IF     7

/* Transport state */
typedef enum {
    TRANSPORT_INACTIVE,
    TRANSPORT_RX_ACTIVE,
    TRANSPORT_ACTIVE,
    TRANSPORT_PAYLOAD_AVAIL,
} h_transport_state_t;

/* Opaque transport channel */
typedef void *h_transport_handle_t;

#endif /* H_TRANSPORT_H */
