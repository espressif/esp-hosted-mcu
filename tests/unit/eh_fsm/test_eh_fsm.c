/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* eh_fsm.h engine: bubbling, entry/exit order, re-entry, unexpected, range. */
#include <assert.h>
#include <string.h>
#include "eh_fsm.h"

enum { EV_GO, EV_UP, EV_SELF, EV_NONE, EV_IGN, EV_COUNT };

static char s_trace[256];
static void t(const char *s) { strcat(s_trace, s); strcat(s_trace, " "); }

extern const eh_fsm_state_t A, P, P1, P2;

static void a_in(eh_fsm_t *m)  { (void)m; t("+A"); }
static void a_out(eh_fsm_t *m) { (void)m; t("-A"); }
static void p_in(eh_fsm_t *m)  { (void)m; t("+P"); }
static void p_out(eh_fsm_t *m) { (void)m; t("-P"); }
static void p1_in(eh_fsm_t *m) { (void)m; t("+P1"); }
static void p1_out(eh_fsm_t *m){ (void)m; t("-P1"); }
static void p2_in(eh_fsm_t *m) { (void)m; t("+P2"); }
static void p2_out(eh_fsm_t *m){ (void)m; t("-P2"); }

static const eh_fsm_state_t *to_p1(eh_fsm_t *m, const void *d) { (void)m; (void)d; return &P1; }
static const eh_fsm_state_t *to_p2(eh_fsm_t *m, const void *d) { (void)m; (void)d; return &P2; }
static const eh_fsm_state_t *to_a(eh_fsm_t *m, const void *d)  { (void)m; (void)d; return &A; }
static const eh_fsm_state_t *self(eh_fsm_t *m, const void *d)  { (void)d; return m->cur; }

static const eh_fsm_fn_t a_on[EV_COUNT]  = { [EV_GO] = to_p1, [EV_SELF] = self };
static const eh_fsm_fn_t p_on[EV_COUNT]  = { [EV_UP] = to_a, [EV_IGN] = eh_fsm_ignore };
static const eh_fsm_fn_t p1_on[EV_COUNT] = { [EV_GO] = to_p2, [EV_SELF] = self };
static const eh_fsm_fn_t p2_on[EV_COUNT] = { [EV_GO] = to_p1 };

const eh_fsm_state_t A  = { "A",  NULL, a_in,  a_out,  a_on };
const eh_fsm_state_t P  = { "P",  NULL, p_in,  p_out,  p_on };
const eh_fsm_state_t P1 = { "P1", &P,   p1_in, p1_out, p1_on };
const eh_fsm_state_t P2 = { "P2", &P,   p2_in, p2_out, p2_on };

#define STEP(ev, want_rc, want_state, want_trace) do { \
    s_trace[0] = 0; \
    assert(eh_fsm_dispatch(&m, ev, NULL) == (want_rc)); \
    assert(m.cur == &(want_state)); \
    if (strcmp(s_trace, want_trace)) { printf("trace '%s' want '%s'\n", s_trace, want_trace); assert(0); } \
} while (0)

int main(void)
{
    eh_fsm_t m;

    eh_fsm_init(&m, "t", &A, EV_COUNT, NULL);
    STEP(EV_GO,   0,  P1, "-A +P +P1 ");      /* top -> nested: parent entered first */
    STEP(EV_GO,   0,  P2, "-P1 +P2 ");        /* siblings: parent untouched */
    STEP(EV_IGN,  0,  P2, "");                /* parent's ignore */
    STEP(EV_NONE, -1, P2, "");                /* unexpected: no change */
    STEP(EV_UP,   0,  A,  "-P2 -P +A ");      /* handled by parent, leaves both */
    STEP(EV_SELF, 0,  A,  "-A +A ");          /* re-entry */
    STEP(EV_GO,   0,  P1, "-A +P +P1 ");
    STEP(EV_SELF, 0,  P1, "-P1 +P1 ");        /* nested re-entry keeps the parent */
    assert(eh_fsm_in(&m, &P) && eh_fsm_in(&m, &P1) && !eh_fsm_in(&m, &A));
    s_trace[0] = 0;
    assert(eh_fsm_dispatch(&m, EV_COUNT, NULL) == -1 && m.cur == &P1);
    assert(eh_fsm_dispatch(&m, -1, NULL) == -1 && m.cur == &P1);
    printf("eh_fsm: all passed\n");
    return 0;
}
