/* SPDX-License-Identifier: Apache-2.0 */
/* Host->slave init priv-pkt builder. Caller transmits result via bus. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "eh_tlv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* host -> CP init packet, worst case:
 *
 *    2  event header (0x22 EH_HOST_PRIV_EVENT_INIT, length)
 *    3  0x44 HOST_CAPABILITIES           \
 *    3  0x45 RCVD_ESP_FIRMWARE_CHIP_ID    |
 *    3  0x46 SLV_CONFIG_TEST_RAW_TP       |- always sent
 *    3  0x47 SLV_CONFIG_THROTTLE_HIGH     |
 *    3  0x48 SLV_CONFIG_THROTTLE_LOW     /
 *    6  0x49 SLV_CONFIG_SET_TRANSFER_SIZE  only when the CP reports a
 *                                          size different from ours
 *    3  0x1A RPC_VERSION                   only when the CP advertised it;
 *                                          older CP parsers reject unknown tags
 *   --
 *   26  EH_HOST_CAPS_PKT_MAX_SIZE
 *
 * EH_TLV_SIZE(n) is tag + length + n value bytes.
 */
#define EH_HOST_CAPS_PKT_MAX_SIZE \
    (2u + 5u * EH_TLV_SIZE(1) + EH_TLV_SIZE(4) + EH_TLV_SIZE(1))

/* Builds the host->CP init packet into @out; the caller sends it.
 * @out_size must be at least EH_HOST_CAPS_PKT_MAX_SIZE.  Returns bytes
 * written, or -1 on error.
 */
int eh_host_transport_build_host_caps_pkt(uint8_t *out, size_t out_size,
                                          uint8_t host_cap,
                                          uint8_t firmware_chip_id,
                                          uint8_t raw_tp_direction,
                                          uint8_t low_threshold,
                                          uint8_t high_threshold);

#ifdef __cplusplus
}
#endif
