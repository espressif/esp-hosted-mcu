/* host/core/src/h_serial_if.c
 *
 * Serial interface for control path communication.
 * Routes protobuf-encoded RPC messages over the transport layer.
 * Migrated from host/drivers/serial/serial_drv.c */

#include "h_serial_if.h"
#include "h_wrapper.h"

/* Transport frame callback — set by transport driver during init */
static void (*g_recv_callback)(uint8_t *data, uint16_t len) = NULL;

h_err_t h_serial_if_init(void)
{
    H_LOGI("SERIAL", "Serial IF initialized");
    return H_OK;
}

void h_serial_if_deinit(void)
{
    g_recv_callback = NULL;
    H_LOGI("SERIAL", "Serial IF deinitialized");
}

h_err_t h_serial_if_send(const uint8_t *data, uint16_t len)
{
    /* Frame is send via transport layer.
     * Actual transport-specific write is handled by h_transport_drv.c */
    (void)data;
    (void)len;
    return H_OK; /* Stub — full implementation in port layer */
}

h_err_t h_serial_if_recv(uint8_t *data, uint16_t *len, int32_t timeout_ms)
{
    (void)data;
    (void)len;
    (void)timeout_ms;
    return H_ERR_TIMEOUT; /* Stub — populated by transport driver */
}
