/* host/core/include/h_public/h_wifi_api.h */
#ifndef H_WIFI_API_H
#define H_WIFI_API_H

#include "h_types.h"
#include "h_wifi_types.h"

/* Phase 1 minimal Wi-Fi API surface */
h_err_t h_wifi_init(const h_wifi_init_config_t *cfg);
h_err_t h_wifi_deinit(void);
h_err_t h_wifi_set_mode(h_wifi_mode_t mode);
h_err_t h_wifi_get_mode(h_wifi_mode_t *mode);
h_err_t h_wifi_start(void);
h_err_t h_wifi_stop(void);
h_err_t h_wifi_connect(void);
h_err_t h_wifi_disconnect(void);
h_err_t h_wifi_set_config(h_wifi_interface_t ifx, h_wifi_config_t *cfg);
h_err_t h_wifi_get_config(h_wifi_interface_t ifx, h_wifi_config_t *cfg);
h_err_t h_wifi_scan_start(const h_wifi_scan_config_t *cfg);
h_err_t h_wifi_scan_stop(void);
h_err_t h_wifi_scan_get_ap_num(uint16_t *num);
h_err_t h_wifi_scan_get_ap_records(uint16_t *num, h_wifi_ap_record_t *records);

#endif
