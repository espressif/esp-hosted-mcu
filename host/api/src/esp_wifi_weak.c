/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * Weak version of esp_wifi API.
 *
 * Used when WiFi-Remote does not provide required esp_wifi calls
 */

#include "esp_hosted_api_priv.h"
#include "esp_hosted_wifi_config.h"

H_WEAK_REF esp_err_t esp_wifi_init(const wifi_init_config_t *config)
{
	return esp_wifi_remote_init(config);
}

H_WEAK_REF esp_err_t esp_wifi_deinit(void)
{
	return esp_wifi_remote_deinit();
}

H_WEAK_REF esp_err_t esp_wifi_set_mode(wifi_mode_t mode)
{
	return esp_wifi_remote_set_mode(mode);
}

H_WEAK_REF esp_err_t esp_wifi_get_mode(wifi_mode_t *mode)
{
	return esp_wifi_remote_get_mode(mode);
}

H_WEAK_REF esp_err_t esp_wifi_start(void)
{
	return esp_wifi_remote_start();
}

H_WEAK_REF esp_err_t esp_wifi_stop(void)
{
	return esp_wifi_remote_stop();
}

H_WEAK_REF esp_err_t esp_wifi_restore(void)
{
	return esp_wifi_remote_restore();
}

H_WEAK_REF esp_err_t esp_wifi_connect(void)
{
	return esp_wifi_remote_connect();
}

H_WEAK_REF esp_err_t esp_wifi_disconnect(void)
{
	return esp_wifi_remote_disconnect();
}

H_WEAK_REF esp_err_t esp_wifi_clear_fast_connect(void)
{
	return esp_wifi_remote_clear_fast_connect();
}

H_WEAK_REF esp_err_t esp_wifi_deauth_sta(uint16_t aid)
{
	return esp_wifi_remote_deauth_sta(aid);
}

H_WEAK_REF esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block)
{
	return esp_wifi_remote_scan_start(config, block);
}

H_WEAK_REF esp_err_t esp_wifi_scan_stop(void)
{
	return esp_wifi_remote_scan_stop();
}

H_WEAK_REF esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number)
{
	return esp_wifi_remote_scan_get_ap_num(number);
}

H_WEAK_REF esp_err_t esp_wifi_scan_get_ap_record(wifi_ap_record_t *ap_record)
{
	return esp_wifi_remote_scan_get_ap_record(ap_record);
}

H_WEAK_REF esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records)
{
	return esp_wifi_remote_scan_get_ap_records(number, ap_records);
}

H_WEAK_REF esp_err_t esp_wifi_clear_ap_list(void)
{
	return esp_wifi_remote_clear_ap_list();
}

H_WEAK_REF esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info)
{
	return esp_wifi_remote_sta_get_ap_info(ap_info);
}

H_WEAK_REF esp_err_t esp_wifi_set_ps(wifi_ps_type_t type)
{
	return esp_wifi_remote_set_ps(type);
}

H_WEAK_REF esp_err_t esp_wifi_get_ps(wifi_ps_type_t *type)
{
	return esp_wifi_remote_get_ps(type);
}

H_WEAK_REF esp_err_t esp_wifi_set_protocol(wifi_interface_t ifx, uint8_t protocol_bitmap)
{
	return esp_wifi_remote_set_protocol(ifx, protocol_bitmap);
}

H_WEAK_REF esp_err_t esp_wifi_get_protocol(wifi_interface_t ifx, uint8_t *protocol_bitmap)
{
	return esp_wifi_remote_get_protocol(ifx, protocol_bitmap);
}

H_WEAK_REF esp_err_t esp_wifi_set_bandwidth(wifi_interface_t ifx, wifi_bandwidth_t bw)
{
	return esp_wifi_remote_set_bandwidth(ifx, bw);
}

H_WEAK_REF esp_err_t esp_wifi_get_bandwidth(wifi_interface_t ifx, wifi_bandwidth_t *bw)
{
	return esp_wifi_remote_get_bandwidth(ifx, bw);
}

H_WEAK_REF esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t second)
{
	return esp_wifi_remote_set_channel(primary, second);
}

H_WEAK_REF esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *second)
{
	return esp_wifi_remote_get_channel(primary, second);
}

H_WEAK_REF esp_err_t esp_wifi_set_country(const wifi_country_t *country)
{
	return esp_wifi_remote_set_country(country);
}

H_WEAK_REF esp_err_t esp_wifi_get_country(wifi_country_t *country)
{
	return esp_wifi_remote_get_country(country);
}

H_WEAK_REF esp_err_t esp_wifi_set_mac(wifi_interface_t ifx, const uint8_t mac[6])
{
	return esp_wifi_remote_set_mac(ifx, mac);
}

H_WEAK_REF esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6])
{
	return esp_wifi_remote_get_mac(ifx, mac);
}

H_WEAK_REF esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf)
{
	return esp_wifi_remote_set_config(interface, conf);
}

H_WEAK_REF esp_err_t esp_wifi_get_config(wifi_interface_t interface, wifi_config_t *conf)
{
	return esp_wifi_remote_get_config(interface, conf);
}

H_WEAK_REF esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *sta)
{
	return esp_wifi_remote_ap_get_sta_list(sta);
}

H_WEAK_REF esp_err_t esp_wifi_ap_get_sta_aid(const uint8_t mac[6], uint16_t *aid)
{
	return esp_wifi_remote_ap_get_sta_aid(mac, aid);
}

H_WEAK_REF esp_err_t esp_wifi_set_storage(wifi_storage_t storage)
{
	return esp_wifi_remote_set_storage(storage);
}

H_WEAK_REF esp_err_t esp_wifi_set_max_tx_power(int8_t power)
{
	return esp_wifi_remote_set_max_tx_power(power);
}

H_WEAK_REF esp_err_t esp_wifi_get_max_tx_power(int8_t *power)
{
	return esp_wifi_remote_get_max_tx_power(power);
}

H_WEAK_REF esp_err_t esp_wifi_set_country_code(const char *country, bool ieee80211d_enabled)
{
	return esp_wifi_remote_set_country_code(country, ieee80211d_enabled);
}

H_WEAK_REF esp_err_t esp_wifi_get_country_code(char *country)
{
	return esp_wifi_remote_get_country_code(country);
}

H_WEAK_REF esp_err_t esp_wifi_sta_get_negotiated_phymode(wifi_phy_mode_t *phymode)
{
	return esp_wifi_remote_sta_get_negotiated_phymode(phymode);
}

H_WEAK_REF esp_err_t esp_wifi_sta_get_aid(uint16_t *aid)
{
	return esp_wifi_remote_sta_get_aid(aid);
}

H_WEAK_REF esp_err_t esp_wifi_sta_get_rssi(int *rssi)
{
	return esp_wifi_remote_sta_get_rssi(rssi);
}

H_WEAK_REF esp_err_t esp_wifi_set_inactive_time(wifi_interface_t ifx, uint16_t sec)
{
	return esp_wifi_remote_set_inactive_time(ifx, sec);
}

H_WEAK_REF esp_err_t esp_wifi_get_inactive_time(wifi_interface_t ifx, uint16_t *sec)
{
	return esp_wifi_remote_get_inactive_time(ifx, sec);
}

#if H_WIFI_HE_SUPPORT
H_WEAK_REF esp_err_t esp_wifi_sta_twt_config(wifi_twt_config_t *config)
{
	return esp_wifi_remote_sta_twt_config(config);
}

H_WEAK_REF esp_err_t esp_wifi_sta_itwt_setup(wifi_itwt_setup_config_t *setup_config)
{
	return esp_wifi_remote_sta_itwt_setup(setup_config);
}

H_WEAK_REF esp_err_t esp_wifi_sta_itwt_teardown(int flow_id)
{
	return esp_wifi_remote_sta_itwt_teardown(flow_id);
}

H_WEAK_REF esp_err_t esp_wifi_sta_itwt_suspend(int flow_id, int suspend_time_ms)
{
	return esp_wifi_remote_sta_itwt_suspend(flow_id, suspend_time_ms);
}

H_WEAK_REF esp_err_t esp_wifi_sta_itwt_get_flow_id_status(int *flow_id_bitmap)
{
	return esp_wifi_remote_sta_itwt_get_flow_id_status(flow_id_bitmap);
}

H_WEAK_REF esp_err_t esp_wifi_sta_itwt_send_probe_req(int timeout_ms)
{
	return esp_wifi_remote_sta_itwt_send_probe_req(timeout_ms);
}

H_WEAK_REF esp_err_t esp_wifi_sta_itwt_set_target_wake_time_offset(int offset_us)
{
	return esp_wifi_remote_sta_itwt_set_target_wake_time_offset(offset_us);
}

#endif

#if H_WIFI_DUALBAND_SUPPORT
H_WEAK_REF esp_err_t esp_wifi_set_band(wifi_band_t band)
{
	return esp_wifi_remote_set_band(band);
}

H_WEAK_REF esp_err_t esp_wifi_get_band(wifi_band_t *band)
{
	return esp_wifi_remote_get_band(band);
}

H_WEAK_REF esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t band_mode)
{
	return esp_wifi_remote_set_band_mode(band_mode);
}

H_WEAK_REF esp_err_t esp_wifi_get_band_mode(wifi_band_mode_t *band_mode)
{
	return esp_wifi_remote_get_band_mode(band_mode);
}

H_WEAK_REF esp_err_t esp_wifi_set_protocols(wifi_interface_t ifx, wifi_protocols_t *protocols)
{
	return esp_wifi_remote_set_protocols(ifx, protocols);
}

H_WEAK_REF esp_err_t esp_wifi_get_protocols(wifi_interface_t ifx, wifi_protocols_t *protocols)
{
	return esp_wifi_remote_get_protocols(ifx, protocols);
}

H_WEAK_REF esp_err_t esp_wifi_set_bandwidths(wifi_interface_t ifx, wifi_bandwidths_t *bw)
{
	return esp_wifi_remote_set_bandwidths(ifx, bw);
}

H_WEAK_REF esp_err_t esp_wifi_get_bandwidths(wifi_interface_t ifx, wifi_bandwidths_t *bw)
{
	return esp_wifi_remote_get_bandwidths(ifx, bw);
}
#endif
