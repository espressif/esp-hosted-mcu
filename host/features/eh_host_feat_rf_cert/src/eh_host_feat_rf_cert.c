/* SPDX-License-Identifier: Apache-2.0 */
/* RF-cert host-feature lifecycle (RPC mechanics live in the ext). */

#include <stddef.h>
#include <stdint.h>

#include "eh_host_phy_cert.h"
#include "eh_host_feat_rf_cert_priv.h"

#include "eh_host_auto_init.h"

static eh_host_feat_rf_cert_state_t s_state;

int eh_host_feat_rf_cert_init(void)
{
    if (s_state.initialised) {
        return 0;
    }
    s_state.initialised = true;
    return 0;
}

int eh_host_feat_rf_cert_deinit(void)
{
    if (!s_state.initialised) {
        return 0;
    }
    s_state.initialised = false;
    return 0;
}

EH_HOST_FEAT_REGISTER(eh_host_feat_rf_cert_init, eh_host_feat_rf_cert_deinit, "rf_cert", 300);
