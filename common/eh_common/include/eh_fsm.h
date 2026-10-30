/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Flat state machine: one table [state][event] -> handler, every cell explicit.
 *
 *   static int idle_start(eh_fsm_t *m, const void *d) { ...; return ST_RUN; }
 *   static const eh_fsm_fn_t s_table[ST_COUNT][EV_COUNT] = {
 *       [ST_IDLE] = { [EV_START] = idle_start, [EV_STOP] = eh_fsm_ignore },
 *       [ST_RUN]  = { [EV_START] = eh_fsm_unsupported, [EV_STOP] = run_stop },
 *   };
 *
 * A handler returns the next state or EH_FSM_STAY; returning the current state
 * re-enters it. A transition runs exit(from), sets the state, on_transit(from, to),
 * entry(to): on_transit sees the new state, so it is where the outside is told.
 * Handlers, entry/exit and on_transit must not dispatch.
 */
#ifndef EH_FSM_H
#define EH_FSM_H

#include <stddef.h>
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct eh_fsm eh_fsm_t;
typedef int  (*eh_fsm_fn_t)(eh_fsm_t *m, const void *data);
typedef void (*eh_fsm_act_t)(eh_fsm_t *m);

typedef struct {
    const char   *name;
    eh_fsm_act_t  entry;
    eh_fsm_act_t  exit;
} eh_fsm_state_t;

typedef struct {
    const char            *name;
    int                    n_states;
    int                    n_events;
    const eh_fsm_state_t  *states;      /* [n_states] */
    const eh_fsm_fn_t     *table;       /* [n_states][n_events], flattened */
    const char *const     *ev_names;    /* [n_events] */
    void (*on_transit)(eh_fsm_t *m, int from, int to);     /* optional */
} eh_fsm_def_t;

struct eh_fsm {
    const eh_fsm_def_t *def;
    volatile int        state;          /* single writer (dispatch); read lock-free */
    int                 ev;             /* event being dispatched, for logs */
};

#define EH_FSM_STAY  (-1)
#define EH_FSM_TAG   "eh_fsm"

static inline const char *eh_fsm_state_name(const eh_fsm_t *m, int s)
{
    return (s >= 0 && s < m->def->n_states) ? m->def->states[s].name : "?";
}

static inline const char *eh_fsm_ev_name(const eh_fsm_t *m, int ev)
{
    return (ev >= 0 && ev < m->def->n_events) ? m->def->ev_names[ev] : "?";
}

/* Handled, nothing to do. */
static inline int eh_fsm_ignore(eh_fsm_t *m, const void *data)
{
    (void)data;
    ESP_LOGD(EH_FSM_TAG, "%s: %s: %s ignored", m->def->name,
             eh_fsm_state_name(m, m->state), eh_fsm_ev_name(m, m->ev));
    return EH_FSM_STAY;
}

/* Must not happen in this state: logged, nothing changes. */
static inline int eh_fsm_unsupported(eh_fsm_t *m, const void *data)
{
    (void)data;
    ESP_LOGW(EH_FSM_TAG, "%s: %s: %s unsupported", m->def->name,
             eh_fsm_state_name(m, m->state), eh_fsm_ev_name(m, m->ev));
    return EH_FSM_STAY;
}

/* Sets the initial state without entry actions. -1: a table cell is empty. */
static inline int eh_fsm_init(eh_fsm_t *m, const eh_fsm_def_t *def, int initial)
{
    for (int i = 0; i < def->n_states * def->n_events; i++) {
        if (!def->table[i]) {
            ESP_LOGE(EH_FSM_TAG, "%s: no handler for %s in %s", def->name,
                     def->ev_names[i % def->n_events], def->states[i / def->n_events].name);
            return -1;
        }
    }
    m->def = def;
    m->state = initial;
    m->ev = -1;
    return 0;
}

static inline void eh_fsm_transit(eh_fsm_t *m, int to)
{
    const eh_fsm_def_t *d = m->def;
    int from = m->state;

    if (d->states[from].exit) {
        d->states[from].exit(m);
    }
    m->state = to;
    ESP_LOGD(EH_FSM_TAG, "%s: %s -> %s", d->name, d->states[from].name, d->states[to].name);
    if (d->on_transit) {
        d->on_transit(m, from, to);
    }
    if (d->states[to].entry) {
        d->states[to].entry(m);
    }
}

static inline void eh_fsm_dispatch(eh_fsm_t *m, int ev, const void *data)
{
    const eh_fsm_def_t *d = m->def;

    if (ev < 0 || ev >= d->n_events) {
        ESP_LOGW(EH_FSM_TAG, "%s: event %d out of range", d->name, ev);
        return;
    }
    m->ev = ev;
    int next = d->table[m->state * d->n_events + ev](m, data);

    if (next != EH_FSM_STAY) {
        eh_fsm_transit(m, next);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* EH_FSM_H */
