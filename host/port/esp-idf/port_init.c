/* host/port/esp-idf/port_init.c
 * Port init/deinit entry points -- called by h_hosted_init() in
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

/* --  OSAL  -- */

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

/* --  Event Loop  -- */
static bool g_event_loop_created = false;

h_err_t h_port_event_init(void)
{
    esp_err_t ret = esp_event_loop_create_default();
    if (ret == ESP_OK) {
        g_event_loop_created = true;
        return H_OK;
    }
    if (ret == ESP_ERR_INVALID_STATE) {
        /* Default loop already exists (created by app or framework) */
        g_event_loop_created = false;
        return H_OK;
    }
    return H_FAIL;
}

void h_port_event_deinit(void)
{
    if (g_event_loop_created) {
        esp_event_loop_delete_default();
        g_event_loop_created = false;
    }
}

/* --  Transport  -- */

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

/* --  RPC Core  -- */

extern int rpc_core_init(void);
extern int rpc_core_deinit(void);
extern int rpc_core_start(void);
extern int rpc_core_stop(void);

h_err_t h_port_rpc_init(void)
{
    if (rpc_core_init() != 0)
        return H_FAIL;
    if (rpc_core_start() != 0)
        return H_FAIL;
    return H_OK;
}

void h_port_rpc_deinit(void)
{
    rpc_core_stop();
    rpc_core_deinit();
}
