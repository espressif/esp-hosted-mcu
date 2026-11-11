/* SPDX-License-Identifier: Apache-2.0 */
#include "rpc_ext_v2_decode_priv.h"

#if EH_HOST_FEAT_PHY_ANT_READY

int rpc_ext_v2_parse_resp_phy_ant(const Rpc *rpc, eh_rpc_ctrl_cmd_t *c)
{
    switch (rpc->msg_id) {
    case RPC_ID__Resp_WifiSetAntGpio:
        if (!rpc->resp_wifi_set_ant_gpio) return -1;
        c->resp_event_status = rpc->resp_wifi_set_ant_gpio->resp;
        return 0;
    case RPC_ID__Resp_WifiGetAntGpio: {
        const RpcRespWifiGetAntGpio *r = rpc->resp_wifi_get_ant_gpio;
        if (!r) return -1;
        c->resp_event_status = r->resp;
        if (r->resp || !r->config) return 0;
        size_t n = r->config->n_gpio_cfgs;
        if (n > EH_RPC_PHY_ANT_GPIO_SLOTS) n = EH_RPC_PHY_ANT_GPIO_SLOTS;
        for (size_t i = 0; i < n; i++) {
            const WifiAntGpio *g = r->config->gpio_cfgs[i];
            if (!g) continue;
            c->u.phy_ant_gpio.slot[i].gpio_select = g->gpio_select;
            c->u.phy_ant_gpio.slot[i].gpio_num    = g->gpio_num;
        }
        return 0;
    }
    case RPC_ID__Resp_WifiSetAnt:
        if (!rpc->resp_wifi_set_ant) return -1;
        c->resp_event_status = rpc->resp_wifi_set_ant->resp;
        return 0;
    case RPC_ID__Resp_WifiGetAnt: {
        const RpcRespWifiGetAnt *r = rpc->resp_wifi_get_ant;
        if (!r) return -1;
        c->resp_event_status = r->resp;
        if (r->resp || !r->config) return 0;
        c->u.phy_ant_cfg.rx_ant_mode    = r->config->rx_ant_mode;
        c->u.phy_ant_cfg.rx_ant_default = r->config->rx_ant_default;
        c->u.phy_ant_cfg.tx_ant_mode    = r->config->tx_ant_mode;
        c->u.phy_ant_cfg.enabled_ant0   = r->config->enabled_ant0;
        c->u.phy_ant_cfg.enabled_ant1   = r->config->enabled_ant1;
        return 0;
    }
    default:
        return 0;
    }
}

#endif /* EH_HOST_FEAT_PHY_ANT_READY */
