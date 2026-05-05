/* Stubs for serial driver functions required by h_rpc_slave_if.c in mock builds. */
#include <stdint.h>
#include <stddef.h>

struct serial_handle_s;
typedef struct serial_handle_s serial_ll_handle_t;

serial_ll_handle_t *serial_ll_init(void(*rx_data_ind)(void))
{
    (void)rx_data_ind;
    return NULL;
}

int parse_tlv(const uint8_t *data, size_t len, void *out)
{
    (void)data;
    (void)len;
    (void)out;
    return 0;
}
