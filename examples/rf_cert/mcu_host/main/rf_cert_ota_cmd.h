/* SPDX-License-Identifier: Apache-2.0 */
/* phy_cert_cp_ota, registered by this example rather than by the CLI feature:
 * it needs a staged image and a partition to hold it, which are application
 * concerns. */

#ifndef RF_CERT_OTA_CMD_H_
#define RF_CERT_OTA_CMD_H_

#ifdef __cplusplus
extern "C" {
#endif

void rf_cert_ota_register(void);

/* One line naming the command, printed after the CLI feature's overview. */
void rf_cert_ota_print_hint(void);

#ifdef __cplusplus
}
#endif

#endif /* RF_CERT_OTA_CMD_H_ */
