/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: GPL-2.0-only OR Apache-2.0
 */

#ifndef EH_COMMON_CTX_H
#define EH_COMMON_CTX_H

/* Req_SetContext / Resp_SetContext / Event_Context payloads: `type` picks the
 * struct, `data` is that struct packed, multi-byte fields little-endian.
 * Wire rule: types and fields are only appended, never changed or reused. */

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif

typedef enum {
	EH_CTX_NONE              = 0,
	EH_CTX_NW_SPLIT_IP_OWNER = 1,
} eh_ctx_type_t;

/* EH_CTX_NW_SPLIT_IP_OWNER request. owner: ESP_EXT_CAP_NW_SPLIT_DHCP_CP / _HOST. */
typedef struct __attribute__((packed)) {
	uint8_t iface;
	uint8_t owner;
} eh_ctx_ip_owner_req_t;

/* EH_CTX_NW_SPLIT_IP_OWNER response and event. pending: applies at the next disconnect. */
typedef struct __attribute__((packed)) {
	uint8_t owner;
	uint8_t pending;
} eh_ctx_ip_owner_t;

/* A peer's payload into the local struct: an older peer's missing fields read 0,
 * a newer peer's extra fields are dropped; shorter than min_len is refused. */
static inline bool eh_ctx_unpack(void *dst, size_t dst_len, size_t min_len,
				 const uint8_t *src, size_t src_len)
{
	if (!src || src_len < min_len) {
		return false;
	}
	memset(dst, 0, dst_len);
	memcpy(dst, src, src_len < dst_len ? src_len : dst_len);
	return true;
}

#endif /* EH_COMMON_CTX_H */
