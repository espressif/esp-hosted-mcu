/* host/core/src/h_api.c
 *
 * Public API — thin wrappers that call RPC layer.
 * All h_wifi_* functions delegate to RPC wrappers in h_rpc_wrap.c.
 * This file provides the public-facing entry points. */

#include "h_wifi_api.h"
#include "h_rpc_core.h"

/* RPC wrappers — defined in h_rpc_wrap.c */
extern h_err_t h_wifi_init_remote(const h_wifi_init_config_t *cfg);
extern h_err_t h_wifi_deinit_remote(void);
extern h_err_t h_wifi_set_mode_remote(h_wifi_mode_t mode);
extern h_err_t h_wifi_get_mode_remote(h_wifi_mode_t *mode);
extern h_err_t h_wifi_start_remote(void);
extern h_err_t h_wifi_stop_remote(void);
extern h_err_t h_wifi_connect_remote(void);
extern h_err_t h_wifi_disconnect_remote(void);
extern h_err_t h_wifi_set_config_remote(h_wifi_interface_t ifx, h_wifi_config_t *cfg);
extern h_err_t h_wifi_get_config_remote(h_wifi_interface_t ifx, h_wifi_config_t *cfg);
extern h_err_t h_wifi_scan_start_remote(const h_wifi_scan_config_t *cfg);
extern h_err_t h_wifi_scan_stop_remote(void);
extern h_err_t h_wifi_scan_get_ap_num_remote(uint16_t *num);
extern h_err_t h_wifi_scan_get_ap_records_remote(uint16_t *num, h_wifi_ap_record_t *records);

h_err_t h_wifi_init(const h_wifi_init_config_t *cfg) {
    return h_wifi_init_remote(cfg);
}
h_err_t h_wifi_deinit(void) {
    return h_wifi_deinit_remote();
}
h_err_t h_wifi_set_mode(h_wifi_mode_t mode) {
    return h_wifi_set_mode_remote(mode);
}
h_err_t h_wifi_get_mode(h_wifi_mode_t *mode) {
    return h_wifi_get_mode_remote(mode);
}
h_err_t h_wifi_start(void) {
    return h_wifi_start_remote();
}
h_err_t h_wifi_stop(void) {
    return h_wifi_stop_remote();
}
h_err_t h_wifi_connect(void) {
    return h_wifi_connect_remote();
}
h_err_t h_wifi_disconnect(void) {
    return h_wifi_disconnect_remote();
}
h_err_t h_wifi_set_config(h_wifi_interface_t ifx, h_wifi_config_t *cfg) {
    return h_wifi_set_config_remote(ifx, cfg);
}
h_err_t h_wifi_get_config(h_wifi_interface_t ifx, h_wifi_config_t *cfg) {
    return h_wifi_get_config_remote(ifx, cfg);
}
h_err_t h_wifi_scan_start(const h_wifi_scan_config_t *cfg) {
    return h_wifi_scan_start_remote(cfg);
}
h_err_t h_wifi_scan_stop(void) {
    return h_wifi_scan_stop_remote();
}
h_err_t h_wifi_scan_get_ap_num(uint16_t *num) {
    return h_wifi_scan_get_ap_num_remote(num);
}
h_err_t h_wifi_scan_get_ap_records(uint16_t *num, h_wifi_ap_record_t *records) {
    return h_wifi_scan_get_ap_records_remote(num, records);
}
