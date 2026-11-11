/* SPDX-License-Identifier: Apache-2.0 */
/* CP PHY antenna selection — host API over RPC. Mirrors esp_phy_ant_*. */

#ifndef EH_HOST_PHY_ANT_H_
#define EH_HOST_PHY_ANT_H_

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EH_HOST_PHY_ANT_GPIO_SLOTS 4

typedef enum {
    EH_HOST_PHY_ANT_MODE_ANT0 = 0,
    EH_HOST_PHY_ANT_MODE_ANT1,
    EH_HOST_PHY_ANT_MODE_AUTO,
} eh_host_phy_ant_mode_t;

typedef enum {
    EH_HOST_PHY_ANT_ANT0 = 0,
    EH_HOST_PHY_ANT_ANT1,
} eh_host_phy_ant_t;

/* gpio_num is a coprocessor GPIO; pins owned by the transport are refused. */
typedef struct {
    bool    gpio_select;
    uint8_t gpio_num;
} eh_host_phy_ant_gpio_t;

typedef struct {
    eh_host_phy_ant_gpio_t gpio_cfg[EH_HOST_PHY_ANT_GPIO_SLOTS];
} eh_host_phy_ant_gpio_config_t;

/* enabled_ant0/1: 4-bit ANT_SEL pattern over gpio_cfg[0..3]. */
typedef struct {
    eh_host_phy_ant_mode_t rx_ant_mode;
    eh_host_phy_ant_t      rx_ant_default;
    eh_host_phy_ant_mode_t tx_ant_mode;
    uint8_t                enabled_ant0;
    uint8_t                enabled_ant1;
} eh_host_phy_ant_config_t;

int eh_host_feat_phy_ant_init(void);
int eh_host_feat_phy_ant_deinit(void);

esp_err_t eh_host_phy_ant_set_gpio(const eh_host_phy_ant_gpio_config_t *cfg);
esp_err_t eh_host_phy_ant_get_gpio(eh_host_phy_ant_gpio_config_t *cfg);
esp_err_t eh_host_phy_ant_set(const eh_host_phy_ant_config_t *cfg);
esp_err_t eh_host_phy_ant_get(eh_host_phy_ant_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_PHY_ANT_H_ */
