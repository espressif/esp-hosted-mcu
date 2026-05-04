/* host/core/src/h_api.c
 *
 * Public API — thin wrappers that call RPC layer.
 * All h_wifi_* functions delegate to RPC wrappers in h_rpc_wrap.c.
 * This file provides the public-facing entry points. */

#include "h_wifi_api.h"

/* RPC wrappers — defined in h_rpc_wrap.c */
extern int rpc_wifi_init(const h_wifi_init_config_t *cfg);
extern int rpc_wifi_deinit(void);
extern int rpc_wifi_set_mode(h_wifi_mode_t mode);
extern int rpc_wifi_get_mode(h_wifi_mode_t *mode);
extern int rpc_wifi_start(void);
extern int rpc_wifi_stop(void);
extern int rpc_wifi_connect(void);
extern int rpc_wifi_disconnect(void);
extern int rpc_wifi_set_config(h_wifi_interface_t interface, h_wifi_config_t *conf);
extern int rpc_wifi_get_config(h_wifi_interface_t interface, h_wifi_config_t *conf);
extern int rpc_wifi_scan_start(const h_wifi_scan_config_t *config, bool block);
extern int rpc_wifi_scan_stop(void);
extern int rpc_wifi_scan_get_ap_num(uint16_t *number);
extern int rpc_wifi_scan_get_ap_records(uint16_t *number, h_wifi_ap_record_t *ap_records);

h_err_t h_wifi_init(const h_wifi_init_config_t *cfg) {
    return rpc_wifi_init(cfg);
}
h_err_t h_wifi_deinit(void) {
    return rpc_wifi_deinit();
}
h_err_t h_wifi_set_mode(h_wifi_mode_t mode) {
    return rpc_wifi_set_mode(mode);
}
h_err_t h_wifi_get_mode(h_wifi_mode_t *mode) {
    return rpc_wifi_get_mode(mode);
}
h_err_t h_wifi_start(void) {
    return rpc_wifi_start();
}
h_err_t h_wifi_stop(void) {
    return rpc_wifi_stop();
}
h_err_t h_wifi_connect(void) {
    return rpc_wifi_connect();
}
h_err_t h_wifi_disconnect(void) {
    return rpc_wifi_disconnect();
}
h_err_t h_wifi_set_config(h_wifi_interface_t ifx, h_wifi_config_t *cfg) {
    return rpc_wifi_set_config(ifx, cfg);
}
h_err_t h_wifi_get_config(h_wifi_interface_t ifx, h_wifi_config_t *cfg) {
    return rpc_wifi_get_config(ifx, cfg);
}
h_err_t h_wifi_scan_start(const h_wifi_scan_config_t *cfg) {
    return rpc_wifi_scan_start(cfg, true);
}
h_err_t h_wifi_scan_stop(void) {
    return rpc_wifi_scan_stop();
}
h_err_t h_wifi_scan_get_ap_num(uint16_t *num) {
    return rpc_wifi_scan_get_ap_num(num);
}
h_err_t h_wifi_scan_get_ap_records(uint16_t *num, h_wifi_ap_record_t *records) {
    return rpc_wifi_scan_get_ap_records(num, records);
}
