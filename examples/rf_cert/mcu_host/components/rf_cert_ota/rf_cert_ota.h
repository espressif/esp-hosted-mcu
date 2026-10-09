/* SPDX-License-Identifier: Apache-2.0 */
/* Coprocessor OTA for the RF cert example, kept apart from the PHY test
 * console so disabling it removes the whole subject. */

#ifndef RF_CERT_OTA_H_
#define RF_CERT_OTA_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stream the staged image to the coprocessor and activate it.
 * Does not return when it sends: the host restarts to resync with the
 * rebooting coprocessor. ESP_OK means it deliberately skipped; any other
 * value is a failure. */
esp_err_t rf_cert_ota_perform(void);

#ifdef __cplusplus
}
#endif

#endif /* RF_CERT_OTA_H_ */
