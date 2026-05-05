/* Stubs for rpc_slaveif_* and h_wifi_adapt_* functions required by
 * h_rpc_wrap.c in mock builds.
 */
#include <stdint.h>
#include <stddef.h>
#include "rpc_slave_if.h"
#include "h_wifi_type_adapt.h"

/* ── rpc_slaveif: lifecycle ── */
int rpc_slaveif_init(void) { return 0; }
int rpc_slaveif_deinit(void) { return 0; }
int rpc_slaveif_start(void) { return 0; }
int rpc_slaveif_stop(void) { return 0; }

/* ── rpc_slaveif: callbacks / misc ── */
int rpc_slaveif_register_custom_callback(uint32_t msg_id_exp,
    void (*callback)(uint32_t msg_id_recvd, const uint8_t *data_recvd,
                     size_t data_len_recvd, void *local_context),
    void *local_context)
{
    (void)msg_id_exp; (void)callback; (void)local_context;
    return 0;
}

/* ── rpc_slaveif: control ── */
ctrl_cmd_t * rpc_slaveif_config_heartbeat(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_custom_rpc(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_feature_control(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_get_coprocessor_app_desc(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_get_coprocessor_fwversion(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_iface_mac_addr_len_get(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_iface_mac_addr_set_get(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_set_slave_dhcp_dns_status(ctrl_cmd_t *req) { (void)req; return NULL; }

/* ── rpc_slaveif: OTA ── */
ctrl_cmd_t * rpc_slaveif_ota_activate(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_ota_begin(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_ota_end(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_ota_write(ctrl_cmd_t *req) { (void)req; return NULL; }

/* ── rpc_slaveif: Wi-Fi ── */
ctrl_cmd_t * rpc_slaveif_wifi_ap_get_sta_aid(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_ap_get_sta_list(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_clear_ap_list(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_clear_fast_connect(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_connect(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_deauth_sta(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_deinit(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_disconnect(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_bandwidth(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_band(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_band_mode(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_channel(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_config(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_country_code(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_country(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_inactive_time(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_mac(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_max_tx_power(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_mode(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_protocol(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_ps(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_init(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_restore(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_scan_get_ap_num(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_scan_get_ap_record(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_scan_get_ap_records(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_scan_params(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_scan_start(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_scan_stop(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_bandwidth(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_band(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_band_mode(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_channel(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_config(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_country_code(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_country(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_inactive_time(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_mac(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_max_tx_power(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_mode(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_protocol(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_ps(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_storage(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_sta_get_aid(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_sta_get_ap_info(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_sta_get_negotiated_phymode(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_sta_get_rssi(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_start(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_stop(ctrl_cmd_t *req) { (void)req; return NULL; }

/* Dual-band / HE / Enterprise / DPP / GPIO / ExtCoex stubs */
ctrl_cmd_t * rpc_slaveif_wifi_set_protocols(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_protocols(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_bandwidths(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_get_bandwidths(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_sta_twt_config(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_sta_itwt_setup(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_sta_itwt_teardown(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_sta_itwt_suspend(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_sta_itwt_get_flow_id_status(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_sta_itwt_send_probe_req(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_sta_itwt_set_target_wake_time_offset(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_sta_enterprise_enable(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_sta_enterprise_disable(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_set_identity(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_clear_identity(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_set_username(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_clear_username(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_set_password(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_clear_password(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_set_new_password(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_clear_new_password(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_set_ca_cert(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_clear_ca_cert(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_set_certificate_and_key(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_clear_certificate_and_key(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_set_disable_time_check(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_get_disable_time_check(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_set_ttls_phase2_method(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_set_suiteb_certification(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_set_pac_file(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_set_fast_params(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_use_default_cert_bundle(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_wifi_set_okc_support(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_set_domain_name(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_eap_set_eap_methods(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_supp_dpp_init(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_supp_dpp_deinit(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_supp_dpp_bootstrap_gen(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_supp_dpp_start_listen(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_supp_dpp_stop_listen(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_gpio_config(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_gpio_reset_pin(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_gpio_set_level(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_gpio_get_level(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_gpio_set_direction(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_gpio_input_enable(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_gpio_set_pull_mode(ctrl_cmd_t *req) { (void)req; return NULL; }
ctrl_cmd_t * rpc_slaveif_ext_coex(ctrl_cmd_t *req) { (void)req; return NULL; }

/* ── h_wifi_adapt stubs ── */
void h_wifi_adapt_init_config_to_native(const h_wifi_init_config_t *src, wifi_init_config_t *dst)
{ (void)src; (void)dst; }
void h_wifi_adapt_config_to_native(const h_wifi_config_t *src, wifi_config_t *dst)
{ (void)src; (void)dst; }
void h_wifi_adapt_scan_config_to_native(const h_wifi_scan_config_t *src, wifi_scan_config_t *dst)
{ (void)src; (void)dst; }
void h_wifi_adapt_ap_record_to_native(const h_wifi_ap_record_t *src, wifi_ap_record_t *dst)
{ (void)src; (void)dst; }
void h_wifi_adapt_sta_list_to_native(const h_wifi_sta_list_t *src, wifi_sta_list_t *dst)
{ (void)src; (void)dst; }
void h_wifi_adapt_country_to_native(const h_wifi_country_t *src, wifi_country_t *dst)
{ (void)src; (void)dst; }
void h_wifi_adapt_init_config_to_host(const wifi_init_config_t *src, h_wifi_init_config_t *dst)
{ (void)src; (void)dst; }
void h_wifi_adapt_config_to_host(const wifi_config_t *src, h_wifi_config_t *dst)
{ (void)src; (void)dst; }
void h_wifi_adapt_scan_config_to_host(const wifi_scan_config_t *src, h_wifi_scan_config_t *dst)
{ (void)src; (void)dst; }
void h_wifi_adapt_ap_record_to_host(const wifi_ap_record_t *src, h_wifi_ap_record_t *dst)
{ (void)src; (void)dst; }
void h_wifi_adapt_sta_list_to_host(const wifi_sta_list_t *src, h_wifi_sta_list_t *dst)
{ (void)src; (void)dst; }
void h_wifi_adapt_country_to_host(const wifi_country_t *src, h_wifi_country_t *dst)
{ (void)src; (void)dst; }
wifi_interface_t h_wifi_adapt_iface_to_native(h_wifi_interface_t v)
{ (void)v; return 0; }
