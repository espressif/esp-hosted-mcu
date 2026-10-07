/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* ESP-IDF FreeRTOS stream-buffer implementation. */

#include "eh_host_port_stream_buffer.h"

#if EH_HOST_PORT_HAS_STREAM_BUFFER
#ifdef ESP_PLATFORM

#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

struct eh_host_port_stream_buffer {
    StreamBufferHandle_t handle;
};

static TickType_t ticks_from_ms(uint32_t timeout_ms)
{
    if (timeout_ms == EH_HOST_PORT_WAIT_FOREVER) {
        return portMAX_DELAY;
    }
    if (timeout_ms == 0) {
        return 0;
    }
    return pdMS_TO_TICKS(timeout_ms);
}

eh_host_port_stream_buffer_t *eh_host_port_stream_buffer_create(
    size_t capacity,
    size_t trigger_level)
{
    if (capacity == 0 || trigger_level == 0 || trigger_level > capacity) {
        return NULL;
    }

    eh_host_port_stream_buffer_t *buffer = calloc(1, sizeof(*buffer));
    if (!buffer) {
        return NULL;
    }

    buffer->handle = xStreamBufferCreate(capacity, trigger_level);
    if (!buffer->handle) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

void eh_host_port_stream_buffer_destroy(eh_host_port_stream_buffer_t *buffer)
{
    if (!buffer) {
        return;
    }
    if (buffer->handle) {
        vStreamBufferDelete(buffer->handle);
    }
    free(buffer);
}

size_t eh_host_port_stream_buffer_send(
    eh_host_port_stream_buffer_t *buffer,
    const void *data,
    size_t length,
    uint32_t timeout_ms)
{
    if (!buffer || !buffer->handle || !data || length == 0) {
        return 0;
    }
    return xStreamBufferSend(buffer->handle, data, length, ticks_from_ms(timeout_ms));
}

size_t eh_host_port_stream_buffer_receive(
    eh_host_port_stream_buffer_t *buffer,
    void *data,
    size_t length,
    uint32_t timeout_ms)
{
    if (!buffer || !buffer->handle || !data || length == 0) {
        return 0;
    }
    return xStreamBufferReceive(buffer->handle, data, length, ticks_from_ms(timeout_ms));
}

#endif /* ESP_PLATFORM */
#endif /* EH_HOST_PORT_HAS_STREAM_BUFFER */
