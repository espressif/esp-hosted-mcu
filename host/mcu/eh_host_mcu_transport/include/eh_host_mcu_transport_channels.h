/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Host-side per-iface channel registry. MCU only. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*eh_host_channel_rx_fn_t)(void *h, void *buf,
                                             void *buff_to_free, size_t len);
typedef esp_err_t (*eh_host_channel_tx_fn_t)(void *h, void *buf, size_t len);

typedef struct eh_host_channel_s {
    void                    *api_chan;
    uint8_t                  if_type;
    uint8_t                  aux_type;   /* ESP_AUX_IF channels only */
    uint8_t                  secure;
    eh_host_channel_tx_fn_t  tx;
    eh_host_channel_rx_fn_t  rx;
} eh_host_channel_t;

eh_host_channel_t *eh_host_transport_add_channel(void *api_chan,
                                                  uint8_t if_type,
                                                  uint8_t secure,
                                                  eh_host_channel_tx_fn_t *tx_out,
                                                  eh_host_channel_rx_fn_t rx);

int eh_host_transport_remove_channel(eh_host_channel_t *channel);

eh_host_channel_t *eh_host_transport_get_channel(uint8_t if_type);

/* Generic TX. payload is unframed; we frame + bus_tx with a copy. */
esp_err_t eh_host_transport_tx(uint8_t if_type, uint8_t if_num,
                                const uint8_t *payload, uint16_t len,
                                uint8_t flags);

/* ESP_AUX_IF channel, by aux_type. Send with eh_host_transport_aux_tx(). */
eh_host_channel_t *eh_host_transport_add_aux_channel(uint8_t aux_type,
                                                      eh_host_channel_rx_fn_t rx);

esp_err_t eh_host_transport_aux_tx(uint8_t aux_type,
                                    const uint8_t *payload, uint16_t len,
                                    uint8_t flags);

#define EH_FRAME_HEADROOM_V1 12  /* sizeof(struct esp_payload_header) */

/* HCI inbound-handler registration (impl in eh_host_mcu_hci.c). The BT
 * feature pushes its handler down here (leaf-safe: the bus never calls up);
 * user is bound alongside the handler and handed back on each delivery.
 * The byte-pipe internals (tx_pack / rx_deliver) are transport-private —
 * see src/eh_host_mcu_hci_internal.h. */
typedef void (*eh_host_mcu_hci_rx_handler_fn)(const uint8_t *frame, uint16_t len,
                                              void *user);
void eh_host_mcu_hci_set_rx_handler(eh_host_mcu_hci_rx_handler_fn fn, void *user);


#ifdef __cplusplus
}
#endif
