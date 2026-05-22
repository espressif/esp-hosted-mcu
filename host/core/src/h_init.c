/* host/core/src/h_init.c */
#include "h_init.h"
#include "h_config.h"
#include "h_wrapper.h"

/* Port entry points — defined by each port (e.g. host/port/esp-idf/port_init.c).
 * Uses h_port_* prefix to avoid collision with h_wrapper.h macros
 * (e.g. h_transport_init is a macro that expands to g_h_transport.init). */
extern h_err_t h_port_osal_init(void);
extern void    h_port_osal_deinit(void);
extern h_err_t h_port_event_init(void);
extern void    h_port_event_deinit(void);
extern h_err_t h_port_transport_init(void);
extern void    h_port_transport_deinit(void);
extern h_err_t h_port_rpc_init(void);
extern void    h_port_rpc_deinit(void);

static bool g_hosted_initialized = false;

/* ── Contract Validation (fail-fast at startup) ── */
h_err_t h_validate_contracts(void)
{
    /* OSAL required functions */
    if (!g_h_osal.malloc || !g_h_osal.free || !g_h_osal.mutex_create ||
        !g_h_osal.queue_create || !g_h_osal.sem_create ||
        !g_h_osal.thread_create) {
        H_LOGE("INIT", "OSAL contract missing required functions");
        return H_ERR_INVALID_ARG;
    }

    /* Event required functions */
    if (!g_h_event.register_handler || !g_h_event.post) {
        H_LOGE("INIT", "Event contract missing required functions");
        return H_ERR_INVALID_ARG;
    }

    /* Transport: validate the specific transport selected at compile time */
    /* Common Transport requirements */
    if (!g_h_transport.init || !g_h_transport.deinit ||
        !g_h_transport.bus_ready || !g_h_transport.transmit) {
        H_LOGE("INIT", "Transport contract missing required base functions");
        return H_ERR_INVALID_ARG;
    }

    /* Bus-specific requirements */
#if H_TRANSPORT_IN_USE == H_TRANSPORT_SPI
    if (!g_h_transport.spi_transfer ||
        !g_h_transport.gpio_config || !g_h_transport.gpio_set_intr) {
        H_LOGE("INIT", "SPI transport contract missing bus-specific functions");
        return H_ERR_INVALID_ARG;
    }
#elif H_TRANSPORT_IN_USE == H_TRANSPORT_SPI_HD
    if (!g_h_transport.spi_hd_read_reg ||
        !g_h_transport.spi_hd_write_reg || !g_h_transport.spi_hd_read_dma ||
        !g_h_transport.spi_hd_write_dma || !g_h_transport.spi_hd_send_cmd9 ||
        !g_h_transport.gpio_config || !g_h_transport.gpio_set_intr) {
        H_LOGE("INIT", "SPI-HD transport contract missing bus-specific functions");
        return H_ERR_INVALID_ARG;
    }
#elif H_TRANSPORT_IN_USE == H_TRANSPORT_SDIO
    if (!g_h_transport.sdio_read_block) {
        H_LOGE("INIT", "SDIO transport contract missing bus-specific functions");
        return H_ERR_INVALID_ARG;
    }
#elif H_TRANSPORT_IN_USE == H_TRANSPORT_UART
    if (!g_h_transport.uart_read) {
        H_LOGE("INIT", "UART transport contract missing bus-specific functions");
        return H_ERR_INVALID_ARG;
    }
#endif

    H_LOGI("INIT", "All required vtable slots filled");
    return H_OK;
}

/* ── h_hosted_init ──
 * Explicit init entry point. Application calls this before any Wi-Fi API.
 * Uses goto-cleanup for partial-init rollback. */
h_err_t h_hosted_init(void)
{
    h_err_t err;

    if (g_hosted_initialized)
        return H_OK;

    err = h_validate_contracts();
    if (err != H_OK) return err;

    err = h_port_osal_init();
    if (err != H_OK) goto cleanup_osal;

    err = h_port_event_init();
    if (err != H_OK) goto cleanup_event;

    err = h_port_transport_init();
    if (err != H_OK) goto cleanup_transport;

    err = h_port_rpc_init();
    if (err != H_OK) goto cleanup_rpc;

    g_hosted_initialized = true;
    H_LOGI("INIT", "ESP-Hosted initialized");
    return H_OK;

cleanup_rpc:
    h_port_transport_deinit();
cleanup_transport:
    h_port_event_deinit();
cleanup_event:
    h_port_osal_deinit();
cleanup_osal:
    return err;
}

/* ── h_hosted_deinit ──
 * Reverse-order teardown. Idempotent (safe to call multiple times). */
h_err_t h_hosted_deinit(void)
{
    if (!g_hosted_initialized) return H_OK;

    h_port_rpc_deinit();
    h_port_transport_deinit();
    h_port_event_deinit();
    h_port_osal_deinit();

    g_hosted_initialized = false;
    return H_OK;
}
