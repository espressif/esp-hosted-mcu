/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Peer Data Transfer host-feature: lifecycle + Event_CustomRpc handlers. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "eh_host_feat_peer_data.h"
#include "eh_host_peer_data.h"

#include "eh_host_port_master_config.h"
#if EH_HOST_AUTO_FEAT_INIT
#include "eh_host_auto_init.h"
#endif

static struct {
    bool initialised;
} s_state;

esp_err_t eh_host_feat_peer_data_init(void)
{
    if (s_state.initialised) return ESP_OK;
    if (eh_host_peer_data_register_event_handlers() != ESP_OK) {
        return ESP_FAIL;
    }
    s_state.initialised = true;
    return ESP_OK;
}

esp_err_t eh_host_feat_peer_data_deinit(void)
{
    if (!s_state.initialised) return ESP_OK;
    eh_host_peer_data_unregister_event_handlers();
    s_state.initialised = false;
    return ESP_OK;
}

#if EH_HOST_AUTO_FEAT_INIT
EH_HOST_FEAT_REGISTER(eh_host_feat_peer_data_init, eh_host_feat_peer_data_deinit, "peer_data", 80);
#endif
