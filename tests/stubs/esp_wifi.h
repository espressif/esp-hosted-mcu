/* Stub for Linux mock build -- minimal ESP-IDF Wi-Fi types */
#ifndef ESP_WIFI_H
#define ESP_WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Minimal wifi_sta_config_t needed by bridge layer */
typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    uint8_t scan_method;
    uint8_t bssid_set;
    uint8_t bssid[6];
    uint8_t channel;
    uint8_t listen_interval;
    uint8_t sort_method;
    struct {
        int8_t rssi;
        uint8_t authmode;
        int8_t rssi_5g_adjustment;
    } threshold;
    struct {
        uint8_t capable;
        uint8_t required;
    } pmf_cfg;
    uint8_t rm_enabled;
    uint8_t btm_enabled;
    uint8_t mbo_enabled;
    uint8_t ft_enabled;
    uint8_t owe_enabled;
    uint8_t transition_disable;
    uint8_t reserved;
    uint8_t reserved1;
    uint8_t reserved2;
    uint8_t he_reserved;
    uint8_t sae_pwe_h2e;
    uint8_t sae_pk_mode;
    uint8_t sae_h2e_identifier[32];
    uint8_t failure_retry_cnt;
    uint8_t he_dcm_set;
    uint8_t he_dcm_max_constellation_tx;
    uint8_t he_dcm_max_constellation_rx;
    uint8_t he_mcs9_enabled;
    uint8_t he_su_beamformee_disabled;
    uint8_t he_trig_su_bmforming_feedback_disabled;
    uint8_t he_trig_mu_bmforming_partial_feedback_disabled;
    uint8_t he_trig_cqi_feedback_disabled;
    uint8_t vht_su_beamformee_disabled;
    uint8_t vht_mu_beamformee_disabled;
    uint8_t vht_mcs8_enabled;
} wifi_sta_config_t;

/* Minimal wifi_ap_config_t */
typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    uint8_t ssid_len;
    uint8_t channel;
    uint8_t authmode;
    uint8_t ssid_hidden;
    uint8_t max_connection;
    uint16_t beacon_interval;
    uint8_t csa_count;
    uint8_t dtim_period;
    uint8_t pairwise_cipher;
    uint8_t ftm_responder;
    struct {
        uint8_t capable;
        uint8_t required;
    } pmf_cfg;
    uint8_t sae_pwe_h2e;
} wifi_ap_config_t;

/* wifi_config_t union */
typedef union {
    wifi_ap_config_t ap;
    wifi_sta_config_t sta;
} wifi_config_t;

/* Minimal wifi_he_ap_info_t */
typedef struct {
    uint8_t bss_color;
    uint8_t partial_bss_color;
    uint8_t bss_color_disabled;
} wifi_he_ap_info_t;

/* Minimal wifi_country_t */
typedef struct {
    char cc[3];
    uint8_t schan;
    uint8_t nchan;
    int8_t max_tx_power;
    uint8_t policy;
} wifi_country_t;

/* Minimal wifi_ap_record_t */
typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[32];
    uint8_t ssid_len;
    uint8_t primary;
    uint8_t second;
    int8_t  rssi;
    uint8_t authmode;
    uint8_t pairwise_cipher;
    uint8_t group_cipher;
    uint16_t beacon_interval;
    wifi_country_t country;
    uint8_t ant;
    uint8_t phy_11b;
    uint8_t phy_11g;
    uint8_t phy_11n;
    uint8_t phy_lr;
    uint8_t phy_11a;
    uint8_t phy_11ac;
    uint8_t phy_11ax;
    uint8_t wps;
    uint8_t ftm_responder;
    uint8_t ftm_initiator;
    uint8_t reserved;
    wifi_he_ap_info_t he_ap;
    uint8_t bandwidth;
    uint8_t vht_ch_freq1;
    uint8_t vht_ch_freq2;
} wifi_ap_record_t;

/* Minimal scan time struct */
typedef struct {
    struct {
        uint32_t min;
        uint32_t max;
    } active;
    uint32_t passive;
} wifi_scan_time_t;

/* Minimal wifi_scan_channel_bitmap_t */
typedef struct {
    uint16_t ghz_2_channels;
    uint16_t ghz_5_channels;
} wifi_scan_channel_bitmap_t;

/* Minimal wifi_scan_config_t */
typedef struct {
    uint8_t *bssid;
    uint8_t *ssid;
    uint8_t channel;
    bool show_hidden;
    wifi_scan_time_t scan_time;
    uint8_t home_chan_dwell_time;
    uint8_t scan_type;
    wifi_scan_channel_bitmap_t channel_bitmap;
} wifi_scan_config_t;

/* Minimal wifi_init_config_t */
typedef struct {
    uint32_t magic;
    uint16_t static_rx_buf_num;
    uint16_t dynamic_rx_buf_num;
    uint8_t  tx_buf_type;
    uint16_t static_tx_buf_num;
    uint16_t dynamic_tx_buf_num;
    uint8_t  rx_mgmt_buf_type;
    uint16_t rx_mgmt_buf_num;
    uint16_t cache_tx_buf_num;
    uint8_t  csi_enable;
    uint8_t  ampdu_rx_enable;
    uint8_t  ampdu_tx_enable;
    uint8_t  amsdu_tx_enable;
    uint8_t  nvs_enable;
    uint8_t  nano_enable;
    uint8_t  rx_ba_win;
    int      wifi_task_core_id;
    uint16_t beacon_max_len;
    uint32_t feature_caps;
    uint8_t  mgmt_sbuf_num;
    uint8_t  sta_disconnected_pm;
    uint8_t  espnow_max_encrypt_num;
    uint8_t  tx_hetb_queue_num;
    uint8_t  dump_hesigb_enable;
} wifi_init_config_t;

/* Minimal wifi_sta_info_t */
typedef struct {
    uint8_t mac[6];
    int8_t  rssi;
    uint8_t phy_11b;
    uint8_t phy_11g;
    uint8_t phy_11n;
    uint8_t phy_lr;
    uint8_t phy_11ax;
    uint8_t is_mesh_child;
    uint8_t reserved;
} wifi_sta_info_t;

/* Minimal wifi_sta_list_t */
typedef struct {
    wifi_sta_info_t sta[10];
    int num;
} wifi_sta_list_t;

#define ESP_WIFI_MAX_CONN_NUM 10

#define H_MAX_CUSTOM_MSG_HANDLERS 4

/* Minimal wifi_event types */
typedef struct {
    uint8_t mac[6];
    uint8_t aid;
    uint8_t is_mesh_child;
} wifi_event_ap_staconnected_t;

typedef struct {
    uint8_t mac[6];
    uint8_t aid;
    uint8_t reason;
    uint8_t is_mesh_child;
} wifi_event_ap_stadisconnected_t;

typedef struct {
    uint8_t status;
    uint8_t number;
    uint8_t scan_id;
} wifi_event_sta_scan_done_t;

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[32];
    uint8_t ssid_len;
    uint8_t channel;
    uint8_t authmode;
    uint16_t aid;
} wifi_event_sta_connected_t;

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[32];
    uint8_t ssid_len;
    uint8_t reason;
    int8_t  rssi;
} wifi_event_sta_disconnected_t;

typedef struct {
    int flow_id;
} wifi_event_sta_itwt_setup_t;

typedef struct {
    int flow_id;
} wifi_event_sta_itwt_teardown_t;

typedef struct {
    int flow_id;
    int suspend_time_ms;
} wifi_event_sta_itwt_suspend_t;

typedef struct {
    int flow_id;
    int timeout_ms;
} wifi_event_sta_itwt_probe_t;

/* Minimal wifi_scan_default_params_t */
typedef struct {
    wifi_scan_time_t scan_time;
    uint8_t home_chan_dwell_time;
} wifi_scan_default_params_t;

/* Wi-Fi event constants */
#define WIFI_EVENT_WIFI_READY          0
#define WIFI_EVENT_STA_START           1
#define WIFI_EVENT_STA_STOP            2
#define WIFI_EVENT_STA_CONNECTED       3
#define WIFI_EVENT_STA_DISCONNECTED    4
#define WIFI_EVENT_STA_SCAN_DONE       5
#define WIFI_EVENT_STA_AUTHMODE_CHANGE 6
#define WIFI_EVENT_AP_START            7
#define WIFI_EVENT_AP_STOP             8
#define WIFI_EVENT_AP_STACONNECTED     9
#define WIFI_EVENT_AP_STADISCONNECTED  10
#define WIFI_EVENT_HOME_CHANNEL_CHANGE 11
#define WIFI_EVENT_SCAN_DONE           12

#define SAE_H2E_IDENTIFIER_LEN         32

/* Enums */
typedef enum {
    WIFI_MODE_NULL = 0,
    WIFI_MODE_STA,
    WIFI_MODE_AP,
    WIFI_MODE_APSTA,
    WIFI_MODE_NAN,
    WIFI_MODE_MAX,
} wifi_mode_t;

typedef enum {
    WIFI_IF_STA = 0,
    WIFI_IF_AP  = 1,
} wifi_interface_t;

typedef enum {
    WIFI_SECOND_CHAN_NONE = 0,
} wifi_second_chan_t;

typedef enum {
    WIFI_AUTH_OPEN = 0,
} wifi_auth_mode_t;

typedef enum {
    WIFI_CIPHER_TYPE_NONE = 0,
} wifi_cipher_type_t;

typedef enum {
    WIFI_BW_HT20 = 1,
} wifi_bandwidth_t;

typedef enum {
    WIFI_PS_NONE = 0,
    WIFI_PS_MIN_MODEM,
    WIFI_PS_MAX_MODEM,
} wifi_ps_type_t;

typedef enum {
    WIFI_STORAGE_FLASH = 0,
} wifi_storage_t;

typedef enum {
    WIFI_PHY_MODE_LR = 0,
} wifi_phy_mode_t;

typedef enum {
    WIFI_VENDOR_IE_ID_0 = 0,
} wifi_vendor_ie_id_t;

typedef enum {
    WIFI_VND_IE_TYPE_BEACON = 0,
} wifi_vendor_ie_type_t;

typedef struct {
    uint8_t element_id;
    uint8_t length;
    uint8_t vendor_oui[3];
    uint8_t vendor_oui_type;
    uint8_t payload[0];
} vendor_ie_data_t;

/* wifi_softap_vendor_ie_t is defined in rpc_slave_if.h, not here */

/* iTWT config stubs */
typedef struct {
    int setup;
} wifi_twt_config_t;

typedef struct {
    int setup;
} wifi_itwt_setup_config_t;

typedef struct {
    int setup;
} wifi_twt_setup_config_t;

/* DPP stubs */
typedef enum {
    ESP_SUPP_DPP_CFG_RECVD = 0,
} esp_supp_dpp_event_t;

typedef enum {
    ESP_SUPP_DPP_BOOTSTRAP_QR_CODE = 0,
} esp_supp_dpp_bootstrap_t;

typedef void (*esp_supp_dpp_event_cb_t)(esp_supp_dpp_event_t evt, void *data);

/* Enterprise stubs */
typedef enum {
    ESP_EAP_TTLS_PHASE2_EAP = 0,
} esp_eap_ttls_phase2_types_t;

typedef enum {
    ESP_EAP_METHOD_TLS = 0,
} esp_eap_method_t;

typedef struct {
    int placeholder;
} esp_eap_fast_config_t;

/* Band / protocol stubs */
typedef enum {
    WIFI_BAND_2G = 0,
} wifi_band_t;

typedef enum {
    WIFI_BAND_MODE_2G_ONLY = 0,
} wifi_band_mode_t;

typedef struct {
    uint8_t ghz_2g;
    uint8_t ghz_5g;
} wifi_protocols_t;

typedef struct {
    wifi_bandwidth_t ghz_2g;
    wifi_bandwidth_t ghz_5g;
} wifi_bandwidths_t;

#endif
