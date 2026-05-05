/* Stubs for h_api.c RPC wrapper functions.
 * h_api.c delegates to rpc_wifi_* functions defined in h_rpc_wrap.c,
 * which depends on ESP-IDF Wi-Fi types and cannot be compiled in the
 * Linux mock environment. These stubs satisfy the linker for mock builds.
 */
#include "h_wifi_api.h"

int rpc_wifi_init(const h_wifi_init_config_t *cfg) { (void)cfg; return 0; }
int rpc_wifi_deinit(void) { return 0; }
int rpc_wifi_set_mode(h_wifi_mode_t mode) { (void)mode; return 0; }
int rpc_wifi_get_mode(h_wifi_mode_t *mode) { (void)mode; return 0; }
int rpc_wifi_start(void) { return 0; }
int rpc_wifi_stop(void) { return 0; }
int rpc_wifi_connect(void) { return 0; }
int rpc_wifi_disconnect(void) { return 0; }
int rpc_wifi_set_config(h_wifi_interface_t interface, h_wifi_config_t *conf) { (void)interface; (void)conf; return 0; }
int rpc_wifi_get_config(h_wifi_interface_t interface, h_wifi_config_t *conf) { (void)interface; (void)conf; return 0; }
int rpc_wifi_scan_start(const h_wifi_scan_config_t *config, bool block) { (void)config; (void)block; return 0; }
int rpc_wifi_scan_stop(void) { return 0; }
int rpc_wifi_scan_get_ap_num(uint16_t *number) { (void)number; return 0; }
int rpc_wifi_scan_get_ap_records(uint16_t *number, h_wifi_ap_record_t *ap_records) { (void)number; (void)ap_records; return 0; }
