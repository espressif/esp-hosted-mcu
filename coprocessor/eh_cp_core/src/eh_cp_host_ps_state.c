/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Inputs come from transport callbacks, some in ISR context: no logging here. */
#define LOG_LOCAL_LEVEL ESP_LOG_NONE
#include "esp_log.h"

#include "eh_cp_host_ps_state.h"

#if EH_CP_FEAT_HOST_PS_READY

#include "freertos/FreeRTOS.h"

#define GO(st) static int go_##st(eh_fsm_t *m, const void *d) { (void)m; (void)d; return EH_HOST_PS_##st; }
GO(AWAKE) GO(ANNOUNCED) GO(ASLEEP) GO(WAKING)
#undef GO

#define I eh_fsm_ignore
_Static_assert(EH_HOST_PS_EV_COUNT == 4, "one column per event");

static const eh_fsm_fn_t s_table[4][EH_HOST_PS_EV_COUNT] = {
/*                          SLEEP          AWAKE     DOWN       WAKE */
[EH_HOST_PS_AWAKE]     = {  go_ANNOUNCED,  I,        I,         I         },
[EH_HOST_PS_ANNOUNCED] = {  I,             go_AWAKE, go_ASLEEP, I         },
[EH_HOST_PS_ASLEEP]    = {  I,             go_AWAKE, I,         go_WAKING },
[EH_HOST_PS_WAKING]    = {  I,             go_AWAKE, go_ASLEEP, I         },
};
#undef I

static const eh_fsm_state_t s_states[4] = {
	[EH_HOST_PS_AWAKE] = { "AWAKE" }, [EH_HOST_PS_ANNOUNCED] = { "ANNOUNCED" },
	[EH_HOST_PS_ASLEEP] = { "ASLEEP" }, [EH_HOST_PS_WAKING] = { "WAKING" },
};
static const char *const s_ev_names[EH_HOST_PS_EV_COUNT] = { "SLEEP", "AWAKE", "DOWN", "WAKE" };
static const eh_fsm_def_t s_def = {
	.name = "host_ps", .n_states = 4, .n_events = EH_HOST_PS_EV_COUNT,
	.states = s_states, .table = &s_table[0][0], .ev_names = s_ev_names,
};

/* Static: the phase is valid before any feature init. */
eh_fsm_t g_eh_cp_host_ps_fsm = { .def = &s_def, .state = EH_HOST_PS_AWAKE, .ev = -1 };

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

bool eh_cp_host_ps_input(eh_cp_host_ps_ev_t ev)
{
	int from, to;

	portENTER_CRITICAL_SAFE(&s_mux);
	from = g_eh_cp_host_ps_fsm.state;
	eh_fsm_dispatch(&g_eh_cp_host_ps_fsm, ev, NULL);
	to = g_eh_cp_host_ps_fsm.state;
	portEXIT_CRITICAL_SAFE(&s_mux);
	return to != from;
}

static const eh_cp_host_ps_ops_t *s_host_ps_ops;

void eh_cp_host_ps_register_ops(const eh_cp_host_ps_ops_t *ops)
{
	s_host_ps_ops = ops;
}

const eh_cp_host_ps_ops_t *eh_cp_host_ps_ops(void)
{
	return s_host_ps_ops;
}

#endif /* EH_CP_FEAT_HOST_PS_READY */
