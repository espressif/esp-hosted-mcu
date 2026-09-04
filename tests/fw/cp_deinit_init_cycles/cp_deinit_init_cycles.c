/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

/* Test-only: runs deinit/init cycles on its own task and prints the heap each
 * cycle. Injected into the scratch build copy, so no example carries it. */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_console.h"
#include "eh_cp.h"
#include "eh_cp_core.h"

#define CYCLES        CONFIG_EH_TEST_CP_DEINIT_INIT_CYCLES
#define SETTLE_MS     100
#define SETTLE_POLLS  20

static const char *TAG = "cp_deinit_init_cycles";

#if CONFIG_HEAP_TRACING
#include "esp_heap_trace.h"
#define TRACE_RECORDS 600
static heap_trace_record_t s_trace[TRACE_RECORDS];
#else
#define heap_trace_start(m)  ESP_OK
#define heap_trace_stop()    ESP_OK
#endif

#if CONFIG_HEAP_TRACING
/* The trace window is one init and its matching deinit, so anything still
 * outstanding when deinit returns was allocated by that init. */
static int trace_bytes(unsigned *recs_out, unsigned *ovf_out)
{
	heap_trace_summary_t sum = {0};
	int bytes = 0;
	unsigned outstanding = 0;

	heap_trace_summary(&sum);
	size_t n = heap_trace_get_count();
	for (size_t i = 0; i < n; i++) {
		heap_trace_record_t rec;
		if (heap_trace_get(i, &rec) != ESP_OK || rec.freed || !rec.address)
			continue;
		bytes += (int)rec.size;
		outstanding++;
	}
	*recs_out = outstanding;
	*ovf_out  = (unsigned)sum.has_overflowed;
	return bytes;
}

/* Addresses only; the test resolves them against the CP elf. */
static void report_records(void)
{
	size_t n = heap_trace_get_count();

	for (size_t i = 0; i < n; i++) {
		heap_trace_record_t rec;
		if (heap_trace_get(i, &rec) != ESP_OK || rec.freed || !rec.address)
			continue;
		char stk[CONFIG_HEAP_TRACING_STACK_DEPTH * 12 + 1];
		int w = 0;
		for (int f = 0; f < CONFIG_HEAP_TRACING_STACK_DEPTH; f++) {
			if (!rec.alloced_by[f])
				break;
			w += snprintf(stk + w, sizeof(stk) - w, "%p ", rec.alloced_by[f]);
		}
		ESP_LOGW(TAG, "[cycles] rec %u B callers %s", (unsigned)rec.size, stk);
	}
}
#else
static int trace_bytes(unsigned *r, unsigned *o) { *r = 0; *o = 0; return 0; }
static void report_records(void) { }
#endif

/* eh_cp_init() returns before auto_feat_init_task finishes, so sample on the
 * same signal deinit waits for, not on the init return. */
static void wait_settled(void)
{
	if (g_auto_feat_init_done_eg) {
		xEventGroupWaitBits(g_auto_feat_init_done_eg, EH_CP_FEAT_INIT_DONE_BIT,
		                    pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
	}
	vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
}

__attribute__((unused))
static void cp_cycles_task(void *arg)
{
	(void)arg;
	vTaskDelay(pdMS_TO_TICKS(8000));   /* let boot allocations settle */

#if CONFIG_HEAP_TRACING
	ESP_ERROR_CHECK(heap_trace_init_standalone(s_trace, TRACE_RECORDS));
#endif
	/* Start deinited, so each cycle is one init and its matching deinit. */
	esp_err_t d0 = eh_cp_deinit();
	vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
	ESP_LOGI(TAG, "[cycles] priming deinit=%d free=%u", (int)d0,
	         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

	for (int i = 1; i <= CYCLES; i++) {
		unsigned recs = 0, ovf = 0;

		ESP_ERROR_CHECK(heap_trace_start(HEAP_TRACE_LEAKS));
		esp_err_t r = eh_cp_init();
		wait_settled();
		esp_err_t d = eh_cp_deinit();

		/* Deferred frees land after deinit returns, so poll until the count
		 * stops moving; a number that never settles shows as polls == max. */
		int borrowed = -1, polls = 0;
		for (; polls < SETTLE_POLLS; polls++) {
			vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
			int now = trace_bytes(&recs, &ovf);
			if (now == borrowed)
				break;
			borrowed = now;
		}
		ESP_LOGI(TAG, "[cycles] cycle=%d init=%d deinit=%d bytes=%d "
		              "recs=%u ovf=%u polls=%d free=%u",
		         i, (int)r, (int)d, borrowed, recs, ovf, polls,
		         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
	}

	/* Run verdict: outstanding after the LAST deinit is what was never given
	 * back. Per-cycle counts also list blocks freed just after deinit. */
	ESP_ERROR_CHECK(heap_trace_stop());
	{
		unsigned recs = 0, ovf = 0;
		int bytes = trace_bytes(&recs, &ovf);
		ESP_LOGI(TAG, "[cycles] run total: never_freed=%d B recs=%u ovf=%u",
		         bytes, recs, ovf);
		if (recs)
			report_records();
	}
	ESP_LOGI(TAG, "[cycles] done cycles=%d", CYCLES);

	ESP_ERROR_CHECK(eh_cp_init());   /* leave the CP running */
	vTaskDelete(NULL);
}

esp_err_t esp_hosted_deinit(void);

/* Test-only CLI: lets a test tear the stack down at a chosen moment. Kept here
 * so the product CLI exposes no teardown command. */
static int hosted_deinit_cmd(int argc, char *argv[])
{
	(void)argc; (void)argv;
	return esp_hosted_deinit() == ESP_OK ? 0 : 1;
}

/* Spawn once: this runs from the feature walk, which every cycle repeats. */
static esp_err_t cp_cycles_spawn(void)
{
	static bool spawned;

	if (spawned)
		return ESP_OK;
	spawned = true;

	const esp_console_cmd_t cmd = {
		.command = "hosted-deinit",
		.help = "TEST: call esp_hosted_deinit()",
		.func = hosted_deinit_cmd,
	};
	esp_console_cmd_register(&cmd);

	if (CYCLES > 0)
		xTaskCreate(cp_cycles_task, "cp_dinit_cycles", 4096, NULL, 5, NULL);
	return ESP_OK;
}

static esp_err_t cp_cycles_noop(void) { return ESP_OK; }

EH_CP_FEAT_REGISTER(cp_cycles_spawn, cp_cycles_noop, "cp_deinit_init_cycles",
                    tskNO_AFFINITY, 399);   /* late: after every real feature */
