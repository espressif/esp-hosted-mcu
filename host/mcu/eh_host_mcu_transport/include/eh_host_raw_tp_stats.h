/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

/* Raw-TP capability and direction bits; same wire values as the CP's eh_caps.h. */
#ifndef ESP_TEST_RAW_TP_NONE
#define ESP_TEST_RAW_TP_NONE              0
#define ESP_TEST_RAW_TP                   (1 << 0)
#define ESP_TEST_RAW_TP__ESP_TO_HOST      (1 << 1)
#define ESP_TEST_RAW_TP__HOST_TO_ESP      (1 << 2)
#define ESP_TEST_RAW_TP__BIDIRECTIONAL    (1 << 3)
#endif

#ifdef __cplusplus
extern "C" {
#endif

void eh_host_raw_tp_process_test_capabilities(uint8_t cap);

/* Runtime control (mirrors the kmod's raw_tp_mode semantics):
 * dir = ESP_TEST_RAW_TP__HOST_TO_ESP / __ESP_TO_HOST / __BIDIRECTIONAL.
 * start: informs the CP (0x46) and runs the local engine; stop: dir=0 both
 * sides. Return 0 on success; -1 without raw-TP support compiled in. */
int eh_host_raw_tp_start(uint8_t dir);
int eh_host_raw_tp_stop(void);
void eh_host_raw_tp_update_rx_len(const uint8_t *payload, uint16_t payload_len);
void eh_host_raw_tp_deinit(void);

#ifdef __cplusplus
}
#endif
