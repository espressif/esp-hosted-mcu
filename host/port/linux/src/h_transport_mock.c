/* host/port/linux/src/h_transport_mock.c
 * Linux mock Transport — stub functions overridable by test fixtures. */

#include "h_port_contract.h"
#include "h_config.h"
#include "h_port_config.h"

#if H_TRANSPORT_IN_USE == H_TRANSPORT_SPI

static int mock_init(void **out)
{
    *out = (void*)0xDEAD; /* sentinel */
    return H_OK;
}

static int mock_deinit(void *h)
{
    (void)h;
    return H_OK;
}

static int mock_spi_transfer(void *h, void *ctx)
{
    (void)h;
    (void)ctx;
    return H_OK;
}

static int mock_gpio_config(uint32_t pin, uint32_t mode)
{
    (void)pin;
    (void)mode;
    return H_OK;
}

static int mock_gpio_set_intr(uint32_t pin, uint32_t intr_type,
                              void (*isr)(void*), void *arg)
{
    (void)pin;
    (void)intr_type;
    (void)isr;
    (void)arg;
    return H_OK;
}

static int mock_bus_ready(void *h)
{
    (void)h;
    return H_OK;
}

static int mock_transmit(uint8_t if_type, uint8_t if_num,
                         uint8_t *payload, uint16_t len, uint8_t zcopy,
                         void *to_free, void (*free_fn)(void *), uint8_t flags)
{
    (void)if_type; (void)if_num; (void)payload; (void)len; (void)zcopy; (void)flags;
    if (free_fn && to_free) free_fn(to_free);
    return H_OK;
}

const h_transport_contract_t g_h_transport = {
    .init         = mock_init,
    .deinit       = mock_deinit,
    .bus_ready    = mock_bus_ready,
    .transmit     = mock_transmit,
    .spi_transfer = mock_spi_transfer,
    .gpio_config  = mock_gpio_config,
    .gpio_set_intr = mock_gpio_set_intr,
    /* All other fields NULL — unused in mock SPI transport */
};

#endif /* H_TRANSPORT_IN_USE == H_TRANSPORT_SPI */

/* ── Port Init/Deinit ── */
h_err_t h_port_transport_init(void)
{
    /* Nothing to init in mock */
    return H_OK;
}

void h_port_transport_deinit(void)
{
    /* nothing to tear down */
}

h_err_t h_port_rpc_init(void)
{
    /* RPC core init stub */
    return H_OK;
}

void h_port_rpc_deinit(void)
{
    /* nothing to tear down */
}
