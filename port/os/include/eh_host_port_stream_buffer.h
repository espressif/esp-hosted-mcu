/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Byte-stream buffer abstraction.
 *
 * timeout_ms: 0=non-blocking, EH_HOST_PORT_WAIT_FOREVER=block,
 * otherwise wait for the specified number of milliseconds.
 */

#ifndef EH_HOST_PORT_STREAM_BUFFER_H_
#define EH_HOST_PORT_STREAM_BUFFER_H_

#include "eh_host_port_config.h"
#include "eh_host_port_sync.h"

#ifdef __cplusplus
extern "C" {
#endif

#if EH_HOST_PORT_HAS_STREAM_BUFFER

eh_host_port_stream_buffer_t *eh_host_port_stream_buffer_create(
    size_t capacity,
    size_t trigger_level);

void eh_host_port_stream_buffer_destroy(eh_host_port_stream_buffer_t *buffer);

size_t eh_host_port_stream_buffer_send(
    eh_host_port_stream_buffer_t *buffer,
    const void *data,
    size_t length,
    uint32_t timeout_ms);

size_t eh_host_port_stream_buffer_receive(
    eh_host_port_stream_buffer_t *buffer,
    void *data,
    size_t length,
    uint32_t timeout_ms);

#endif /* EH_HOST_PORT_HAS_STREAM_BUFFER */

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_PORT_STREAM_BUFFER_H_ */
