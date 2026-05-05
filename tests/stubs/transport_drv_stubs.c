/* Stubs for transport driver functions required by h_transport_drv.c
 * and h_rpc_core.c in mock builds.
 */
#include <stdint.h>
#include <stddef.h>
#include "mempool.h"
#include "transport_drv.h"

void *bus_init_internal(void) { return NULL; }
void bus_deinit_internal(void *bus_handle) { (void)bus_handle; }
void check_if_max_freq_used(uint8_t chip_type) { (void)chip_type; }
int ensure_slave_bus_ready(void *bus_handle) { (void)bus_handle; return 0; }

int esp_hosted_tx(uint8_t iface_type, uint8_t iface_num,
                  uint8_t *payload_buf, uint16_t payload_len, uint8_t buff_zerocopy,
                  uint8_t *buffer_to_free, void (*free_buf_func)(void *ptr), uint8_t flags)
{
    (void)iface_type; (void)iface_num; (void)payload_buf; (void)payload_len;
    (void)buff_zerocopy; (void)buffer_to_free; (void)free_buf_func; (void)flags;
    return 0;
}

int esp_hosted_woke_from_power_save(void) { return 0; }

void hci_drv_init(void) { }

hosted_mempool_t * hosted_mempool_create(hosted_mempool_config_t * config)
{
    (void)config;
    return NULL;
}

void * hosted_mempool_alloc(struct hosted_mempool_t *mempool,
                            size_t nbytes, uint8_t need_memset)
{
    (void)mempool; (void)nbytes; (void)need_memset;
    return NULL;
}

int hosted_mempool_free(struct hosted_mempool_t *mempool, void *mem)
{
    (void)mempool; (void)mem;
    return 0;
}

/* rpc_start is provided by h_rpc_wrap.c when linked */

int serial_ll_rx_handler(interface_buffer_handle_t * buf_handle)
{
    (void)buf_handle;
    return 0;
}
