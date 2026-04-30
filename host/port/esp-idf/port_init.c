/* host/port/esp-idf/port_init.c
 * Port init/deinit entry points — called by h_hosted_init() in
 * host/core/src/h_init.c via h_port_* symbols.
 *
 * In ESP-IDF, the OS (FreeRTOS) and event loop are already bootstrapped
 * by app_main before any ESP-Hosted code runs, so osal_init is a no-op.
 * The transport bus init is handled inside each transport's contract
 * (h_transport_*.c) and the h_port_transport_init here is a lightweight
 * hook for any cross-cutting transport setup. */

#include "h_port_contract.h"
#include "h_port_config.h"

#include <esp_event.h>

/* ──  OSAL  ── */

h_err_t h_port_osal_init(void)
{
    /* ESP-IDF FreeRTOS is already initialized by app_main.
     * g_h_osal is statically defined in h_osal.c and requires no
     * runtime init. */
    return H_OK;
}

void h_port_osal_deinit(void)
{
    /* ESP-IDF FreeRTOS is managed by the framework; no-op at port level. */
}

/* ──  Event Loop ── */

h_err_t h_port_event_init(void)
{
    esp_err_t ret = esp_event_loop_create_default();
    return (ret == ESP_OK) ? H_OK : H_FAIL;
}

void h_port_event_deinit(void)
{
    esp_event_loop_delete_default();
}

/* ──  Transport ── */

h_err_t h_port_transport_init(void)
{
    /* Transport bus init is done inside each transport's contract
     * (h_transport_*.c). This hook exists for cross-cutting transport
     * setup if needed. */
    return H_OK;
}

void h_port_transport_deinit(void)
{
    /* Bus deinit is handled per-transport. */
}

/* ──  RPC Core ── */

h_err_t h_port_rpc_init(void)
{
    /* RPC core init — allocate queues, start RX thread.
     * Stub for Phase 1; detailed implementation in RPC phase. */
    return H_OK;
}

void h_port_rpc_deinit(void)
{
    /* Stub — teardown RPC queues and threads. */
}
