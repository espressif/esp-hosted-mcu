/* Stubs for virtual serial interface functions required by h_rpc_core.c
 * production path in mock builds.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int transport_pserial_open(void)
{
    return 0;
}

int transport_pserial_close(void)
{
    return 0;
}

int transport_pserial_send(uint8_t* data, uint16_t data_length)
{
    (void)data;
    (void)data_length;
    return 0;
}

uint8_t *transport_pserial_read(uint32_t *out_nbyte)
{
    (void)out_nbyte;
    return NULL;
}
