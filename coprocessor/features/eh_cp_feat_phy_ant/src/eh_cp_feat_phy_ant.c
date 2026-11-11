/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-License-Identifier: Apache-2.0 */

#include "eh_cp_master_config.h"
#include "eh_cp_feat_phy_ant.h"
#include "eh_cp_core.h"
#include "eh_common_caps.h"
#include "esp_log.h"

static const char *TAG = "feat_phy_ant";

esp_err_t eh_cp_feat_phy_ant_init(void)
{
    eh_cp_add_feat_cap_bits(0, EH_EXT_CAP_PHY_ANT);
    ESP_LOGI(TAG, "PHY antenna feature init ok");
    return ESP_OK;
}

esp_err_t eh_cp_feat_phy_ant_deinit(void)
{
    eh_cp_clear_feat_cap_bits(0, EH_EXT_CAP_PHY_ANT);
    ESP_LOGI(TAG, "PHY antenna feature deinit ok");
    return ESP_OK;
}

#if EH_CP_FEAT_PHY_ANT_AUTO_INIT
EH_CP_FEAT_REGISTER(eh_cp_feat_phy_ant_init,
                   eh_cp_feat_phy_ant_deinit,
                   "feat_phy_ant", tskNO_AFFINITY, 240);
#endif
