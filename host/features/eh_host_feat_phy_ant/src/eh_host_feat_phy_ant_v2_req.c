/* SPDX-License-Identifier: Apache-2.0 */
#include "rpc_ext_v2_pack_priv.h"

#if EH_HOST_FEAT_PHY_ANT_READY

static int compose_req_set_ant_gpio(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                                    alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqWifiSetAntGpio, req_wifi_set_ant_gpio,
                  rpc__req__wifi_set_ant_gpio__init);
    WifiAntGpioConfig *cfg = rpc_ext_v2_tracked_calloc(trk, sizeof(*cfg));
    WifiAntGpio **slots = rpc_ext_v2_tracked_calloc(trk,
                              EH_RPC_PHY_ANT_GPIO_SLOTS * sizeof(*slots));
    if (!cfg || !slots) return -1;
    wifi_ant_gpio_config__init(cfg);
    for (size_t i = 0; i < EH_RPC_PHY_ANT_GPIO_SLOTS; i++) {
        WifiAntGpio *g = rpc_ext_v2_tracked_calloc(trk, sizeof(*g));
        if (!g) return -1;
        wifi_ant_gpio__init(g);
        g->gpio_select = c->u.phy_ant_gpio.slot[i].gpio_select;
        g->gpio_num    = c->u.phy_ant_gpio.slot[i].gpio_num;
        slots[i] = g;
    }
    cfg->n_gpio_cfgs = EH_RPC_PHY_ANT_GPIO_SLOTS;
    cfg->gpio_cfgs   = slots;
    p->config = cfg;
    return 0;
}

static int compose_req_set_ant(Rpc *rpc, const eh_rpc_ctrl_cmd_t *c,
                               alloc_track_t *trk)
{
    ALLOC_PAYLOAD(RpcReqWifiSetAnt, req_wifi_set_ant, rpc__req__wifi_set_ant__init);
    WifiAntConfig *cfg = rpc_ext_v2_tracked_calloc(trk, sizeof(*cfg));
    if (!cfg) return -1;
    wifi_ant_config__init(cfg);
    cfg->rx_ant_mode    = c->u.phy_ant_cfg.rx_ant_mode;
    cfg->rx_ant_default = c->u.phy_ant_cfg.rx_ant_default;
    cfg->tx_ant_mode    = c->u.phy_ant_cfg.tx_ant_mode;
    cfg->enabled_ant0   = c->u.phy_ant_cfg.enabled_ant0;
    cfg->enabled_ant1   = c->u.phy_ant_cfg.enabled_ant1;
    p->config = cfg;
    return 0;
}

COMPOSE_REQ_EMPTY(compose_req_get_ant_gpio, RpcReqWifiGetAntGpio,
                  req_wifi_get_ant_gpio, rpc__req__wifi_get_ant_gpio__init)
COMPOSE_REQ_EMPTY(compose_req_get_ant, RpcReqWifiGetAnt,
                  req_wifi_get_ant, rpc__req__wifi_get_ant__init)

compose_fn rpc_ext_v2_pick_req_phy_ant(int32_t msg_id)
{
    switch (msg_id) {
    case RPC_ID__Req_WifiSetAntGpio: return compose_req_set_ant_gpio;
    case RPC_ID__Req_WifiGetAntGpio: return compose_req_get_ant_gpio;
    case RPC_ID__Req_WifiSetAnt:     return compose_req_set_ant;
    case RPC_ID__Req_WifiGetAnt:     return compose_req_get_ant;
    default:                         return NULL;
    }
}

#endif /* EH_HOST_FEAT_PHY_ANT_READY */
