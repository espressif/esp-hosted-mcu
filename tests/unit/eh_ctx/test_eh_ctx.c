/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* eh_common_ctx.h: an older or newer peer's payload reads as the local struct. */
#include <assert.h>
#include <stdio.h>
#include "eh_common_ctx.h"

int main(void)
{
	eh_ctx_ip_owner_t a;
	const uint8_t older[] = { 2 };              /* owner only */
	const uint8_t same[]  = { 1, 1 };
	const uint8_t newer[] = { 2, 0, 0xAA, 0xBB }; /* fields this build lacks */

	assert(sizeof(eh_ctx_ip_owner_req_t) == 2 && sizeof(eh_ctx_ip_owner_t) == 2);

	assert(eh_ctx_unpack(&a, sizeof(a), sizeof(a.owner), older, sizeof(older)));
	assert(a.owner == 2 && a.pending == 0);

	assert(eh_ctx_unpack(&a, sizeof(a), sizeof(a.owner), same, sizeof(same)));
	assert(a.owner == 1 && a.pending == 1);

	assert(eh_ctx_unpack(&a, sizeof(a), sizeof(a.owner), newer, sizeof(newer)));
	assert(a.owner == 2 && a.pending == 0);

	assert(!eh_ctx_unpack(&a, sizeof(a), sizeof(a.owner), same, 0));
	assert(!eh_ctx_unpack(&a, sizeof(a), sizeof(a.owner), NULL, 2));

	printf("all passed\n");
	return 0;
}
