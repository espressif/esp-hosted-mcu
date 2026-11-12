/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "eh_host_feat_rpc_ext_v2.h"
#include "eh_host_feat_rpc_ext_v2_types.h"
#include "eh_host_feat_rpc.h"
#include "gen_v2.h"

#include "eh_host_phy_ant.h"

#include "eh_host_port_master_config.h"
#if EH_HOST_AUTO_FEAT_INIT
#include "eh_host_auto_init.h"
#endif

#if EH_HOST_FEAT_PHY_ANT_READY

_Static_assert(EH_HOST_PHY_ANT_GPIO_SLOTS == EH_RPC_PHY_ANT_GPIO_SLOTS,
               "API and wire slot counts differ");

int eh_host_feat_phy_ant_init(void)
{
    return 0;
}

int eh_host_feat_phy_ant_deinit(void)
{
    return 0;
}

/* Sends req (ownership passes) and returns the response, or NULL on transport failure. */
static eh_rpc_ctrl_cmd_t *request(int32_t msg_id, eh_rpc_ctrl_cmd_t *req)
{
    eh_rpc_ctrl_cmd_t *r = NULL;
    if (eh_host_feat_rpc_request_sync(msg_id, req, (void **)&r) != 0) {
        return NULL;
    }
    return r;
}

esp_err_t eh_host_phy_ant_set_gpio(const eh_host_phy_ant_gpio_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < EH_HOST_PHY_ANT_GPIO_SLOTS; i++) {
        req->u.phy_ant_gpio.slot[i].gpio_select = cfg->gpio_cfg[i].gpio_select;
        req->u.phy_ant_gpio.slot[i].gpio_num    = cfg->gpio_cfg[i].gpio_num;
    }
    eh_rpc_ctrl_cmd_t *r = request(RPC_ID__Req_WifiSetAntGpio, req);
    if (!r) {
        return ESP_FAIL;
    }
    esp_err_t status = (esp_err_t)r->resp_event_status;
    eh_rpc_ctrl_cmd_free(r);
    return status;
}

esp_err_t eh_host_phy_ant_get_gpio(eh_host_phy_ant_gpio_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) {
        return ESP_ERR_NO_MEM;
    }
    eh_rpc_ctrl_cmd_t *r = request(RPC_ID__Req_WifiGetAntGpio, req);
    if (!r) {
        return ESP_FAIL;
    }
    esp_err_t status = (esp_err_t)r->resp_event_status;
    if (status == ESP_OK) {
        for (size_t i = 0; i < EH_HOST_PHY_ANT_GPIO_SLOTS; i++) {
            cfg->gpio_cfg[i].gpio_select = r->u.phy_ant_gpio.slot[i].gpio_select != 0;
            cfg->gpio_cfg[i].gpio_num    = (uint8_t)r->u.phy_ant_gpio.slot[i].gpio_num;
        }
    }
    eh_rpc_ctrl_cmd_free(r);
    return status;
}

esp_err_t eh_host_phy_ant_set(const eh_host_phy_ant_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) {
        return ESP_ERR_NO_MEM;
    }
    req->u.phy_ant_cfg.rx_ant_mode    = (int32_t)cfg->rx_ant_mode;
    req->u.phy_ant_cfg.rx_ant_default = (int32_t)cfg->rx_ant_default;
    req->u.phy_ant_cfg.tx_ant_mode    = (int32_t)cfg->tx_ant_mode;
    req->u.phy_ant_cfg.enabled_ant0   = cfg->enabled_ant0;
    req->u.phy_ant_cfg.enabled_ant1   = cfg->enabled_ant1;
    eh_rpc_ctrl_cmd_t *r = request(RPC_ID__Req_WifiSetAnt, req);
    if (!r) {
        return ESP_FAIL;
    }
    esp_err_t status = (esp_err_t)r->resp_event_status;
    eh_rpc_ctrl_cmd_free(r);
    return status;
}

esp_err_t eh_host_phy_ant_get(eh_host_phy_ant_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    eh_rpc_ctrl_cmd_t *req = eh_rpc_ctrl_cmd_alloc();
    if (!req) {
        return ESP_ERR_NO_MEM;
    }
    eh_rpc_ctrl_cmd_t *r = request(RPC_ID__Req_WifiGetAnt, req);
    if (!r) {
        return ESP_FAIL;
    }
    esp_err_t status = (esp_err_t)r->resp_event_status;
    if (status == ESP_OK) {
        cfg->rx_ant_mode    = (eh_host_phy_ant_mode_t)r->u.phy_ant_cfg.rx_ant_mode;
        cfg->rx_ant_default = (eh_host_phy_ant_t)r->u.phy_ant_cfg.rx_ant_default;
        cfg->tx_ant_mode    = (eh_host_phy_ant_mode_t)r->u.phy_ant_cfg.tx_ant_mode;
        cfg->enabled_ant0   = (uint8_t)r->u.phy_ant_cfg.enabled_ant0;
        cfg->enabled_ant1   = (uint8_t)r->u.phy_ant_cfg.enabled_ant1;
    }
    eh_rpc_ctrl_cmd_free(r);
    return status;
}

#if EH_HOST_AUTO_FEAT_INIT
EH_HOST_FEAT_REGISTER(eh_host_feat_phy_ant_init, eh_host_feat_phy_ant_deinit, "phy_ant", 300);
#endif

#endif /* EH_HOST_FEAT_PHY_ANT_READY */
