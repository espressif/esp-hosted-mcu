/* SPDX-License-Identifier: Apache-2.0 */
/* Internal header for eh_host_feat_rf_cert. */

#ifndef EH_HOST_FEAT_RF_CERT_PRIV_H_
#define EH_HOST_FEAT_RF_CERT_PRIV_H_

#include <stdbool.h>

#include "eh_host_phy_cert.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool initialised;
} eh_host_feat_rf_cert_state_t;

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_FEAT_RF_CERT_PRIV_H_ */
