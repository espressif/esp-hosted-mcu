/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Raw-TP frame sequence: the sender puts a 32-bit LE counter, from 1, in the
 * first 4 payload bytes, the receiver counts the gaps. Payload only, so the
 * wire header is unchanged. 0 means no counter (a sender without it, such as
 * the Linux host): the receiver skips it. */

#ifndef EH_RAW_TP_SEQ_H
#define EH_RAW_TP_SEQ_H

#include <stdint.h>

typedef struct {
	uint32_t next;      /* expected sequence number */
	uint32_t frames;    /* frames received */
	uint32_t lost;      /* frames skipped by the sender's counter */
	uint32_t restarts;  /* counter went backwards: sender restarted */
	uint8_t  synced;
} eh_raw_tp_seq_t;

/* seq: frames sent before this one. */
static inline void eh_raw_tp_seq_put(uint8_t *payload, uint32_t seq)
{
	seq++;
	payload[0] = (uint8_t)seq;
	payload[1] = (uint8_t)(seq >> 8);
	payload[2] = (uint8_t)(seq >> 16);
	payload[3] = (uint8_t)(seq >> 24);
}

static inline void eh_raw_tp_seq_check(eh_raw_tp_seq_t *s, const uint8_t *payload, uint16_t len)
{
	uint32_t seq;

	if (len < 4)
		return;
	seq = (uint32_t)payload[0] | (uint32_t)payload[1] << 8 |
	      (uint32_t)payload[2] << 16 | (uint32_t)payload[3] << 24;
	if (!seq)
		return;
	s->frames++;
	if (!s->synced) {
		s->synced = 1;
	} else if (seq > s->next) {
		s->lost += seq - s->next;
	} else if (seq < s->next) {
		s->restarts++;
	}
	s->next = seq + 1;
}

#endif /* EH_RAW_TP_SEQ_H */
