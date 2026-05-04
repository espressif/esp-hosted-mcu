/* host/core/src/h_serial_if.c
 *
 * Serial interface for control path communication.
 * Routes protobuf-encoded RPC messages over the transport layer.
 * Migrated from host/drivers/serial/serial_drv.c */

#include "h_serial_if.h"
#include "h_wrapper.h"
#include <string.h>
#include <inttypes.h>

/* Transport frame callback -- set by transport driver during init */
static void (*g_recv_callback)(uint8_t *data, uint16_t len) = NULL;

/* Phase 1: bridge to legacy transport_pserial_* APIs.
 * These are defined in host/drivers/virtual_serial_if/serial_if.c,
 * still compiled as part of the component. */
extern int transport_pserial_send(uint8_t* data, uint16_t data_length);
extern uint8_t *transport_pserial_read(uint32_t *out_nbyte);
extern int transport_pserial_open(void);
extern void transport_pserial_close(void);

h_err_t h_serial_if_init(void)
{
    if (transport_pserial_open() != 0) {
        H_LOGE("SERIAL", "transport_pserial_open failed");
        return H_FAIL;
    }
    H_LOGI("SERIAL", "Serial IF initialized");
    return H_OK;
}

void h_serial_if_deinit(void)
{
    transport_pserial_close();
    g_recv_callback = NULL;
    H_LOGI("SERIAL", "Serial IF deinitialized");
}

h_err_t h_serial_if_send(const uint8_t *data, uint16_t len)
{
    if (!data || !len) {
        H_LOGW("SERIAL", "Empty send data");
        return H_ERR_INVALID_ARG;
    }

    int ret = transport_pserial_send((uint8_t *)data, len);
    return (ret == 0) ? H_OK : H_FAIL;
}

h_err_t h_serial_if_recv(uint8_t *data, uint16_t *len, int32_t timeout_ms)
{
    (void)timeout_ms;

    if (!data || !len) {
        return H_ERR_INVALID_ARG;
    }

    uint32_t out_nbyte = 0;
    uint8_t *rx_data = transport_pserial_read(&out_nbyte);

    if (!rx_data || out_nbyte == 0) {
        return H_ERR_TIMEOUT;
    }

    /* Caller buffer too small — refuse truncated response */
    if (*len < out_nbyte) {
        h_free(rx_data);
        H_LOGW("SERIAL", "recv buffer too small: need %" PRIu32 ", have %u",
               out_nbyte, *len);
        return H_ERR_INVALID_ARG;
    }

    h_memcpy(data, rx_data, (uint16_t)out_nbyte);
    *len = (uint16_t)out_nbyte;

    h_free(rx_data);
    return H_OK;
}
