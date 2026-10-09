/* SPDX-License-Identifier: Apache-2.0 */
/* CP OTA host API — synchronous begin/write/end/activate over RPC. */

#ifndef EH_HOST_CP_OTA_H_
#define EH_HOST_CP_OTA_H_

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "eh_host_feat_rpc_io_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t eh_host_cp_ota_begin(void);

#define EH_HOST_CP_OTA_PROTO_OVERHEAD   16u
#define EH_HOST_CP_OTA_CHUNK_MAX \
    (EH_HOST_RPC_SERIAL_MAX_PAYLOAD_BYTES - EH_HOST_CP_OTA_PROTO_OVERHEAD)

/* ota_data_len up to EH_HOST_CP_OTA_CHUNK_MAX; buffer is copied. */
esp_err_t eh_host_cp_ota_write(const uint8_t *ota_data, uint32_t ota_data_len);

esp_err_t eh_host_cp_ota_end(void);

/* Mark new image pending-boot; caller resets the CP to apply. */
esp_err_t eh_host_cp_ota_activate(void);

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_CP_OTA_H_ */
