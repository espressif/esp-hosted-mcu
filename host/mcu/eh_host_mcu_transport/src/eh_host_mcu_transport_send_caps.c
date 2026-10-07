/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "eh_tlv.h"
#include "eh_tlv_tags.h"
#include "eh_common_interface.h"
#include "eh_host_mcu_transport_init_event.h"
#include "eh_host_mcu_transport_send_caps.h"

#define EH_HOST_PRIV_EVENT_INIT  0x22u

int eh_host_transport_build_host_caps_pkt(uint8_t *out, size_t out_size,
                                          uint8_t host_cap,
                                          uint8_t firmware_chip_id,
                                          uint8_t raw_tp_direction,
                                          uint8_t low_threshold,
                                          uint8_t high_threshold)
{
    if (!out || out_size < EH_HOST_CAPS_PKT_MAX_SIZE) return -1;

    uint8_t *p = out;
    uint8_t  evt_len = 0;

    *p++ = EH_HOST_PRIV_EVENT_INIT;
    uint8_t *evt_len_ptr = p++;

    *p++ = EH_HOST_PRIV_HOST_CAPABILITIES;         evt_len++;
    *p++ = 1;                                       evt_len++;
    *p++ = host_cap;                                evt_len++;

    *p++ = EH_HOST_PRIV_RCVD_ESP_FIRMWARE_CHIP_ID; evt_len++;
    *p++ = 1;                                       evt_len++;
    *p++ = firmware_chip_id;                        evt_len++;

    *p++ = EH_HOST_PRIV_SLV_CONFIG_TEST_RAW_TP;    evt_len++;
    *p++ = 1;                                       evt_len++;
    *p++ = raw_tp_direction;                        evt_len++;

    *p++ = EH_HOST_PRIV_SLV_CONFIG_THROTTLE_HIGH;  evt_len++;
    *p++ = 1;                                       evt_len++;
    *p++ = high_threshold;                          evt_len++;

    *p++ = EH_HOST_PRIV_SLV_CONFIG_THROTTLE_LOW;   evt_len++;
    *p++ = 1;                                       evt_len++;
    *p++ = low_threshold;                           evt_len++;

    /* Only when the CP reported a different size (0x1C). A CP that reports
     * nothing keeps its own default, which is always >= ours. */
    uint32_t cp_size = eh_host_mcu_transport_get_cp_transfer_size();
    if (cp_size && cp_size != ESP_TRANSPORT_HOST_MAX_BUF_SIZE) {
        eh_tlv_builder_t b;
        eh_tlv_builder_init(&b, p, EH_TLV_SIZE(4));
        eh_tlv_add_u32_le(&b, EH_HOST_PRIV_SLV_CONFIG_SET_TRANSFER_SIZE,
                          ESP_TRANSPORT_HOST_MAX_BUF_SIZE);
        p += b.pos;
        evt_len += b.pos;
    }

    /* Echo RPC_VERSION (0x1A) only when the CP advertised it on the
     * inbound init event.  Older upstream CP firmware (esp-hosted /
     * esp-hosted-mcu pre-rc1) does not parse this tag in the host->slave
     * direction and may not silently skip it. */
    if (eh_host_mcu_transport_peer_advertised_rpc_version()) {
        *p++ = EH_PRIV_RPC_VERSION;                evt_len++;
        *p++ = 1;                                   evt_len++;
        *p++ = ESP_HOSTED_RPC_VERSION_V2;           evt_len++;
    }

    *evt_len_ptr = evt_len;
    return (int)(p - out);   /* total bytes written, including evt hdr */
}
