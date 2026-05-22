/* host/core/src/h_serial_if.c
 *
 * Serial interface for control path communication.
 * Routes protobuf-encoded RPC messages over the transport layer. */

#include "h_serial_if.h"
#include "h_wrapper.h"
#include "esp_hosted_transport.h"
#include <string.h>
#include <inttypes.h>

/* Internal symbols from h_rpc_slave_if.c and serial_ll_if.c */
struct serial_drv_handle_t;
extern struct serial_drv_handle_t* serial_drv_open(const char *transport);
extern int serial_drv_close(struct serial_drv_handle_t** handle);
extern int serial_drv_write(struct serial_drv_handle_t* handle, uint8_t* buf, int in_count, int* out_count);
extern uint8_t * serial_drv_read(struct serial_drv_handle_t* handle, uint32_t *out_nbyte);
extern int rpc_platform_init(void);
extern int rpc_platform_deinit(void);

static void *g_serial_handle = NULL;

/* ── TLV Helpers ── */

uint16_t h_serial_compose_tlv(uint8_t* buf, const uint8_t* data, uint16_t data_length)
{
    const char* ep_name = RPC_EP_NAME_RSP;
    uint16_t ep_length = strlen(ep_name);
    uint16_t count = 0;
    uint8_t idx;

    buf[count] = H_SERIAL_TLV_T_EPNAME;
    count++;
    buf[count] = (ep_length & 0xFF);
    count++;
    buf[count] = ((ep_length >> 8) & 0xFF);
    count++;

    for (idx = 0; idx < ep_length; idx++) {
        buf[count] = ep_name[idx];
        count++;
    }

    buf[count]= H_SERIAL_TLV_T_DATA;
    count++;
    buf[count] = (data_length & 0xFF);
    count++;
    buf[count] = ((data_length >> 8) & 0xFF);
    count++;
    h_memcpy(&buf[count], data, data_length);
    count = count + data_length;
    return count;
}

uint8_t h_serial_parse_tlv(uint8_t* data, uint32_t* pro_len)
{
    const char* ep_name = RPC_EP_NAME_RSP;
    const char* ep_name2 = RPC_EP_NAME_EVT;
    uint64_t len = 0;
    uint16_t val_len = 0;

    if (data[len] == H_SERIAL_TLV_T_EPNAME) {
        len++;
        val_len = data[len];
        len++;
        val_len = (data[len] << 8) + val_len;
        len++;

        if ((val_len == strlen(ep_name) && strncmp((char*)&data[len], ep_name, val_len) == 0) ||
            (val_len == strlen(ep_name2) && strncmp((char*)&data[len], ep_name2, val_len) == 0)) {
            len = len + val_len;
            if (data[len] == H_SERIAL_TLV_T_DATA) {
                len++;
                val_len = data[len];
                len++;
                val_len = (data[len] << 8) + val_len;
                len++;
                *pro_len = val_len;
                return H_OK;
            }
        }
    }
    return H_FAIL;
}

/* ── Public API ── */

h_err_t h_serial_if_init(void)
{
    if (g_serial_handle) {
        return H_OK;
    }

    g_serial_handle = serial_drv_open(SERIAL_IF_FILE);
    if (!g_serial_handle) {
        H_LOGE("SERIAL", "serial_drv_open failed");
        return H_FAIL;
    }

    if (rpc_platform_init() != H_OK) {
        H_LOGE("SERIAL", "rpc_platform_init failed");
        struct serial_drv_handle_t* h = (struct serial_drv_handle_t*)g_serial_handle;
        serial_drv_close(&h);
        g_serial_handle = NULL;
        return H_FAIL;
    }

    H_LOGI("SERIAL", "Serial IF initialized");
    return H_OK;
}

void h_serial_if_deinit(void)
{
    if (g_serial_handle) {
        rpc_platform_deinit();
        struct serial_drv_handle_t* h = (struct serial_drv_handle_t*)g_serial_handle;
        serial_drv_close(&h);
        g_serial_handle = NULL;
    }
    H_LOGI("SERIAL", "Serial IF deinitialized");
}

h_err_t h_serial_if_send(const uint8_t *data, uint16_t len)
{
    const char* ep_name = RPC_EP_NAME_RSP;
    int count = 0, out_count = 0;
    uint16_t buf_len = 0;
    uint8_t *write_buf = NULL;

    if (!data || !len || !g_serial_handle) {
        return H_ERR_INVALID_ARG;
    }

    buf_len = H_SERIAL_SIZE_OF_TYPE + H_SERIAL_SIZE_OF_LENGTH + strlen(ep_name) +
              H_SERIAL_SIZE_OF_TYPE + H_SERIAL_SIZE_OF_LENGTH + len;

    write_buf = h_calloc(1, buf_len);
    if (!write_buf) {
        return H_ERR_NO_MEM;
    }

    count = h_serial_compose_tlv(write_buf, data, len);
    if (!count) {
        h_free(write_buf);
        return H_FAIL;
    }

    if (serial_drv_write((struct serial_drv_handle_t*)g_serial_handle, write_buf, count, &out_count) != H_OK) {
        h_free(write_buf);
        return H_FAIL;
    }

    /* write_buf is typically freed by transport layer via callback */
    return H_OK;
}

h_err_t h_serial_if_recv(uint8_t *data, uint16_t *len, int32_t timeout_ms)
{
    (void)timeout_ms;

    if (!data || !len || !g_serial_handle) {
        return H_ERR_INVALID_ARG;
    }

    uint32_t out_nbyte = 0;
    uint8_t *rx_data = serial_drv_read((struct serial_drv_handle_t*)g_serial_handle, &out_nbyte);

    if (!rx_data || out_nbyte == 0) {
        return H_ERR_TIMEOUT;
    }

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
