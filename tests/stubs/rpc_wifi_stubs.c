/* Stubs for h_api.c RPC wrapper functions.
 * h_api.c delegates to rpc_wifi_* functions defined in h_rpc_wrap.c,
 * which depends on ESP-IDF Wi-Fi types and cannot be compiled in the
 * Linux mock environment. These stubs satisfy the linker for mock builds.
 */
/* Stubs for functions removed from port_esp_hosted_host_config.h to avoid
 * static inline / non-static declaration mismatch with esp_hosted_transport_config.h.
 *
 * Note: rpc_wifi_* stubs previously here have been removed because
 * h_rpc_wrap.c is now linked and provides the real implementations.
 */
#include <stdbool.h>

bool esp_hosted_is_config_valid(void) { return true; }
int esp_hosted_set_default_config(void) { return 0; }
