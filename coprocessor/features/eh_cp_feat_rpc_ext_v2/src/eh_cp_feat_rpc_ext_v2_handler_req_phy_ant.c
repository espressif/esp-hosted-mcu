/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-License-Identifier: Apache-2.0 */

#include "eh_cp_master_config.h"
#if EH_CP_FEAT_RPC_EXT_V2_READY

#if EH_CP_FEAT_PHY_ANT_READY

#include "eh_cp_feat_rpc_ext_v2_priv.h"
#include "eh_cp_transport_gpio_pin_guard.h"
#include "esp_phy.h"
#include "esp_log.h"
#include "esp_private/esp_gpio_reserve.h"

static const char *TAG = "rpc_phy_ant";

#define PHY_ANT_GPIO_SLOTS \
    (sizeof(((esp_phy_ant_gpio_config_t *)0)->gpio_cfg) / sizeof(esp_phy_ant_gpio_t))

/* One ineligible slot rejects the whole request: a partial apply would leave
 * the switch half-wired. */
static esp_err_t ant_gpio_from_wire(const WifiAntGpioConfig *in,
                                    esp_phy_ant_gpio_config_t *out)
{
    if (!in || in->n_gpio_cfgs > PHY_ANT_GPIO_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < in->n_gpio_cfgs; i++) {
        const WifiAntGpio *g = in->gpio_cfgs[i];
        if (!g || !g->gpio_select) {
            continue;
        }
        int num = (int)g->gpio_num;
        if (!GPIO_IS_VALID_OUTPUT_GPIO(num)) {
            ESP_LOGE(TAG, "slot %u: GPIO %d is not an output pad", (unsigned)i, num);
            return ESP_ERR_INVALID_ARG;
        }
        if (!eh_cp_transport_gpio_pin_guard_is_eligible((gpio_num_t)num)) {
            ESP_LOGE(TAG, "slot %u: GPIO %d reserved by transport", (unsigned)i, num);
            return ESP_ERR_INVALID_ARG;
        }
        out->gpio_cfg[i].gpio_select = 1;
        out->gpio_cfg[i].gpio_num = num;
    }
    return ESP_OK;
}

/* Pins of the current config were reserved by gpio_config() on the last apply. */
static esp_err_t check_reserved(const esp_phy_ant_gpio_config_t *old,
                                const esp_phy_ant_gpio_config_t *cfg)
{
    uint64_t own = 0;
    for (size_t i = 0; i < PHY_ANT_GPIO_SLOTS; i++) {
        if (old->gpio_cfg[i].gpio_select) {
            own |= BIT64(old->gpio_cfg[i].gpio_num);
        }
    }
    for (size_t i = 0; i < PHY_ANT_GPIO_SLOTS; i++) {
        if (!cfg->gpio_cfg[i].gpio_select) {
            continue;
        }
        uint64_t pin = BIT64(cfg->gpio_cfg[i].gpio_num);
        if (!(own & pin) && esp_gpio_is_reserved(pin)) {
            ESP_LOGE(TAG, "slot %u: GPIO %u reserved", (unsigned)i,
                     (unsigned)cfg->gpio_cfg[i].gpio_num);
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}

#if EH_CP_FEAT_PHY_ANT_IDF_GPIO_RSV_WORKAROUND
/* IDF tests esp_gpio_is_reserved(gpio_num) with the number used as a mask.
 * Release only what that wrong check would see, for the duration of the call. */
static esp_err_t set_ant_gpio(esp_phy_ant_gpio_config_t *cfg)
{
    uint64_t wrong = 0;
    for (size_t i = 0; i < PHY_ANT_GPIO_SLOTS; i++) {
        if (cfg->gpio_cfg[i].gpio_select) {
            wrong |= (uint64_t)cfg->gpio_cfg[i].gpio_num;
        }
    }
    uint64_t held = 0;
    for (int b = 0; b < 64; b++) {
        if ((wrong & BIT64(b)) && esp_gpio_is_reserved(BIT64(b))) {
            held |= BIT64(b);
        }
    }
    esp_gpio_revoke(held);
    esp_err_t err = esp_phy_set_ant_gpio(cfg);
    esp_gpio_reserve(held);
    return err;
}
#else
#define set_ant_gpio(cfg) esp_phy_set_ant_gpio(cfg)
#endif

esp_err_t req_phy_ant_set_gpio(Rpc *req, Rpc *resp, void *priv_data)
{
    RPC_TEMPLATE(RpcRespWifiSetAntGpio, resp_wifi_set_ant_gpio,
            RpcReqWifiSetAntGpio, req_wifi_set_ant_gpio,
            rpc__resp__wifi_set_ant_gpio__init);

    esp_phy_ant_gpio_config_t cfg = {0};
    esp_phy_ant_gpio_config_t old = {0};
    RPC_RET_FAIL_IF(ant_gpio_from_wire(req_payload->config, &cfg));
    RPC_RET_FAIL_IF(esp_phy_get_ant_gpio(&old));
    RPC_RET_FAIL_IF(check_reserved(&old, &cfg));
    RPC_RET_FAIL_IF(set_ant_gpio(&cfg));
    return ESP_OK;
}

esp_err_t req_phy_ant_get_gpio(Rpc *req, Rpc *resp, void *priv_data)
{
    RPC_TEMPLATE_SIMPLE(RpcRespWifiGetAntGpio, resp_wifi_get_ant_gpio,
            RpcReqWifiGetAntGpio, req_wifi_get_ant_gpio,
            rpc__resp__wifi_get_ant_gpio__init);

    esp_phy_ant_gpio_config_t cfg = {0};
    RPC_RET_FAIL_IF(esp_phy_get_ant_gpio(&cfg));

    /* Freed by rpc__free_unpacked() on the response. */
    WifiAntGpioConfig *out = calloc(1, sizeof(*out));
    WifiAntGpio **slots = calloc(PHY_ANT_GPIO_SLOTS, sizeof(*slots));
    if (!out || !slots) {
        free(out);
        free(slots);
        resp_payload->resp = ESP_ERR_NO_MEM;
        return ESP_OK;
    }
    wifi_ant_gpio_config__init(out);
    out->gpio_cfgs = slots;
    resp_payload->config = out;
    for (size_t i = 0; i < PHY_ANT_GPIO_SLOTS; i++) {
        WifiAntGpio *g = calloc(1, sizeof(*g));
        if (!g) {
            resp_payload->resp = ESP_ERR_NO_MEM;
            return ESP_OK;
        }
        wifi_ant_gpio__init(g);
        g->gpio_select = cfg.gpio_cfg[i].gpio_select;
        g->gpio_num = cfg.gpio_cfg[i].gpio_num;
        slots[i] = g;
        out->n_gpio_cfgs = i + 1;
    }
    return ESP_OK;
}

esp_err_t req_phy_ant_set(Rpc *req, Rpc *resp, void *priv_data)
{
    RPC_TEMPLATE(RpcRespWifiSetAnt, resp_wifi_set_ant,
            RpcReqWifiSetAnt, req_wifi_set_ant,
            rpc__resp__wifi_set_ant__init);

    const WifiAntConfig *in = req_payload->config;
    /* The 4-bit fields would truncate silently. */
    if (!in || in->enabled_ant0 > 0xF || in->enabled_ant1 > 0xF) {
        resp_payload->resp = ESP_ERR_INVALID_ARG;
        return ESP_OK;
    }
    esp_phy_ant_config_t cfg = {
        .rx_ant_mode    = (esp_phy_ant_mode_t)in->rx_ant_mode,
        .rx_ant_default = (esp_phy_ant_t)in->rx_ant_default,
        .tx_ant_mode    = (esp_phy_ant_mode_t)in->tx_ant_mode,
        .enabled_ant0   = in->enabled_ant0,
        .enabled_ant1   = in->enabled_ant1,
    };
    RPC_RET_FAIL_IF(esp_phy_set_ant(&cfg));
    return ESP_OK;
}

esp_err_t req_phy_ant_get(Rpc *req, Rpc *resp, void *priv_data)
{
    RPC_TEMPLATE_SIMPLE(RpcRespWifiGetAnt, resp_wifi_get_ant,
            RpcReqWifiGetAnt, req_wifi_get_ant,
            rpc__resp__wifi_get_ant__init);

    esp_phy_ant_config_t cfg = {0};
    RPC_RET_FAIL_IF(esp_phy_get_ant(&cfg));

    WifiAntConfig *out = calloc(1, sizeof(*out));
    if (!out) {
        resp_payload->resp = ESP_ERR_NO_MEM;
        return ESP_OK;
    }
    wifi_ant_config__init(out);
    out->rx_ant_mode    = cfg.rx_ant_mode;
    out->rx_ant_default = cfg.rx_ant_default;
    out->tx_ant_mode    = cfg.tx_ant_mode;
    out->enabled_ant0   = cfg.enabled_ant0;
    out->enabled_ant1   = cfg.enabled_ant1;
    resp_payload->config = out;
    return ESP_OK;
}

#endif /* EH_CP_FEAT_PHY_ANT_READY */
#endif /* EH_CP_FEAT_RPC_EXT_V2_READY */
