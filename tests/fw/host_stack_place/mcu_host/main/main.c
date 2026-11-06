/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

/* Reports the memory region of each transport task stack after auto-init,
 * started by the first hosted call. The test sweeps EH_TEST_BSS_PAD_BYTES;
 * no stack may land in LP RAM. */
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"

static const char *TAG = "stack_place";

#if CONFIG_EH_TEST_BSS_PAD_BYTES > 0
static volatile uint8_t s_pad[CONFIG_EH_TEST_BSS_PAD_BYTES] __attribute__((used));
#endif

static const char *region(const void *p)
{
	if (esp_ptr_in_rtc_dram_fast(p))
		return "lp_ram";
	if (esp_ptr_in_spm(p))
		return "tcm";
	if (esp_ptr_external_ram(p))
		return "psram";
	if (esp_ptr_internal(p))
		return "internal";
	return "other";
}

void app_main(void)
{
#if CONFIG_EH_TEST_BSS_PAD_BYTES > 0
	s_pad[0] = 1;
#endif
	esp_hosted_coprocessor_fwver_t ver;
	int rc = esp_hosted_get_coprocessor_fwversion(&ver);
	ESP_LOGI(TAG, "first call rc=%d pad=%d stack_hwm=%u", rc, CONFIG_EH_TEST_BSS_PAD_BYTES,
		 (unsigned)uxTaskGetStackHighWaterMark(NULL));
	static const char *names[] = { "sdio_read", "sdio_write", "sdio_process_rx", "sdio_rx_buf" };
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		TaskHandle_t h = xTaskGetHandle(names[i]);
		void *s = h ? (void *)pxTaskGetStackStart(h) : NULL;
		ESP_LOGI(TAG, "task %s stack=%p region=%s", names[i], s,
			 h ? region(s) : "none");
	}
	ESP_LOGI(TAG, "internal free=%u largest=%u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
		 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
	ESP_LOGI(TAG, "done");
}
