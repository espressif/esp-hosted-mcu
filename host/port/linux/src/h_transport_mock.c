/* host/port/linux/src/h_transport_mock.c
 * Linux mock Transport — stub functions overridable by test fixtures. */

#include "h_port_contract.h"
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

const h_transport_contract_t g_h_transport = {
    .init         = mock_init,
    .deinit       = mock_deinit,
    .spi_transfer = mock_spi_transfer,
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
