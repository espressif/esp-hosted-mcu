/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __PORT_ESP_HOSTED_HOST_WIFI_CONFIG_H__
#define __PORT_ESP_HOSTED_HOST_WIFI_CONFIG_H__

#include "eh_host_port_idf_ver.h"

#if CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_EXT_ITWT && CONFIG_SLAVE_SOC_WIFI_HE_SUPPORT
  #define EH_HOST_WIFI_HE_SUPPORT 1
#else
  #define EH_HOST_WIFI_HE_SUPPORT 0
#endif

/* HE support (structs, API) changed after ESP-IDF v5.3 */
#define EH_HOST_WIFI_HE_GREATER_THAN_ESP_IDF_5_3 \
    (EH_HOST_WIFI_HE_SUPPORT && EH_HOST_IDF_GE(5, 3, 1))

/* dual band API */
#define EH_HOST_WIFI_DUALBAND_SUPPORT           EH_HOST_IDF_GE(5, 4, 0)

#ifdef CONFIG_ESP_WIFI_REMOTE_EAP_ENABLED
  #define EH_HOST_WIFI_ENTERPRISE_SUPPORT 1
#else
  #define EH_HOST_WIFI_ENTERPRISE_SUPPORT 0
#endif

/* ESP-IDF 5.5.0 breaking change: reserved/he_reserved renamed to reserved1/reserved2 */
#define EH_HOST_WIFI_NEW_RESERVED_FIELD_NAMES   EH_HOST_IDF_GE(5, 5, 0)

/* wifi_country_t::wifi_5g_channel_mask: slave SoC capability, not an IDF version */
#ifdef CONFIG_SLAVE_SOC_WIFI_SUPPORT_5G
  #define EH_HOST_WIFI_GOT_5G_CHANNEL_MASK      1
#else
  #define EH_HOST_WIFI_GOT_5G_CHANNEL_MASK      0
#endif

/* wifi_ap_config_t::wpa3_compatible_mode */
#define EH_HOST_GOT_AP_WPA3_COMPATIBLE_MODE     EH_HOST_IDF_GE(6, 0, 0)

/* wifi_sta_config_t::disable_wpa3_compatible_mode */
#define EH_HOST_GOT_WPA3_COMPATIBLE_MODE        EH_HOST_IDF_GE(6, 0, 0)

/* User-controllable reserved field decoding - works regardless of IDF version */
#ifdef CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_DECODE_RESERVED_FIELD
  #define EH_HOST_DECODE_WIFI_RESERVED_FIELD 1
#else
  #define EH_HOST_DECODE_WIFI_RESERVED_FIELD 0
#endif

/* wifi_twt_config_t::twt_enable_keep_alive */
#define EH_HOST_GOT_TWT_ENABLE_KEEP_ALIVE       EH_HOST_IDF_GE(5, 3, 2)

/* wifi_ap_config_t::transition_disable */
#define EH_HOST_GOT_AP_CONFIG_PARAM_TRANSITION_DISABLE \
    (EH_HOST_IDF_GE_LT(5, 3, 3,  5, 4, 0) || EH_HOST_IDF_GE(5, 4, 1))

/* wifi_ap_record_t::akm_dpp; release/v5.5 has it, no v5.5 tag does yet */
#define EH_HOST_WIFI_GOT_AP_REC_AKM_DPP         EH_HOST_IDF_GE(6, 0, 3)

/* wifi_scan_threshold_t::rssi_5g_adjustment */
#define EH_HOST_WIFI_GOT_RSSI_5G_ADJ            EH_HOST_IDF_GE(5, 4, 0)

/* wifi_scan_config_t::coex_background_scan */
#define EH_HOST_WIFI_GOT_COEX_BG_SCAN           EH_HOST_IDF_GE(5, 4, 0)

/* wifi_sta_config_t: vht_su/mu_beamformee_disabled, vht_mcs8_enabled */
#define EH_HOST_WIFI_GOT_STA_VHT_CFG            EH_HOST_IDF_GE(5, 5, 0)

/* wifi_ap_config_t: sae_ext, bss_max_idle_cfg, gtk_rekey_interval */
#define EH_HOST_WIFI_GOT_AP_SAE_EXT             EH_HOST_IDF_GE(5, 5, 0)

/* max_bandwidth_negotiation_*: esp_wifi_remote idf_tag_* headers follow the IDF tag, so check both */
#define EH_HOST_WIFI_IDF_GOT_STA_MAX_BW_NEG \
    (EH_HOST_IDF_GE_LT(5, 5, 6,  6, 0, 0) || \
     EH_HOST_IDF_GE_LT(6, 0, 4,  6, 1, 0) || \
     EH_HOST_IDF_GE(6, 1, 1))

#if defined(__has_include)
  #if __has_include("esp_wifi_remote_version.h")
    #include "esp_wifi_remote_version.h"
  #endif
#endif
#if defined(ESP_WIFI_REMOTE_VERSION_MAJOR) && \
    ((ESP_WIFI_REMOTE_VERSION_MAJOR << 16) | (ESP_WIFI_REMOTE_VERSION_MINOR << 8) | \
      ESP_WIFI_REMOTE_VERSION_PATCH) >= ((1 << 16) | (6 << 8) | 5)
  #define EH_HOST_WIFI_REMOTE_GOT_STA_MAX_BW_NEG 1
#else
  #define EH_HOST_WIFI_REMOTE_GOT_STA_MAX_BW_NEG 0
#endif

#if !defined(ESP_PLATFORM)
  #define EH_HOST_WIFI_GOT_STA_MAX_BW_NEG       EH_HOST_WIFI_REMOTE_GOT_STA_MAX_BW_NEG
  #define EH_HOST_WIFI_GOT_SCAN_MAX_OFFCHAN     0
#elif EH_HOST_IDF_GE(6, 1, 0) || \
      defined(CONFIG_ESP_WIFI_ENABLED) || defined(CONFIG_ESP_HOST_WIFI_ENABLED)
  #define EH_HOST_WIFI_GOT_STA_MAX_BW_NEG       EH_HOST_WIFI_IDF_GOT_STA_MAX_BW_NEG
  #define EH_HOST_WIFI_GOT_SCAN_MAX_OFFCHAN     EH_HOST_IDF_GE(6, 1, 1)
#else
  #define EH_HOST_WIFI_GOT_STA_MAX_BW_NEG       (EH_HOST_WIFI_IDF_GOT_STA_MAX_BW_NEG && \
                                                 EH_HOST_WIFI_REMOTE_GOT_STA_MAX_BW_NEG)
  #define EH_HOST_WIFI_GOT_SCAN_MAX_OFFCHAN     0
#endif

/* esp_eap_client_set_eap_methods(); not in v5.5.0 */
#define EH_HOST_GOT_SET_EAP_METHODS_API \
    (EH_HOST_IDF_GE_LT(5, 3, 4,  5, 4, 0) || \
     EH_HOST_IDF_GE_LT(5, 4, 3,  5, 5, 0) || \
     EH_HOST_IDF_GE(5, 5, 1))

/* esp_eap_client_set_domain_name() */
#define EH_HOST_GOT_EAP_SET_DOMAIN_NAME \
    (EH_HOST_IDF_GE_LT(5, 3, 4,  5, 4, 0) || EH_HOST_IDF_GE(5, 4, 3))

/* Host-side RPC only; the IDF API and its esp_wifi_remote hook need v5.5 */
#define EH_HOST_GOT_EAP_OKC_SUPPORT             EH_HOST_IDF_GE(5, 4, 3)

/* esp_wifi_remote_set_okc_support() hook */
#define EH_HOST_WIFI_REMOTE_GOT_OKC_HOOK        EH_HOST_IDF_GE(5, 5, 0)

/**
 * Wi-Fi Easy Connect (DPP) events is returned to user via
 * Wi-Fi DPP events.
 *
 * IDF v5.5 and above only support Wi-Fi DPP events
 * Version check still needed as function declaration for
 * `esp_supp_dpp_init()` changed between IDF 5.5 and IDF 6.0
 */
#if CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_EXT_DPP
#define EH_HOST_CFG_WIFI_DPP 1
#else
#define EH_HOST_CFG_WIFI_DPP 0
#endif

#define EH_HOST_SUPP_DPP_SUPPORT (EH_HOST_CFG_WIFI_DPP && EH_HOST_IDF_LT(6, 0, 0))

// Wi-Fi DPP Events: only in IDF v5.5 and above
#define EH_HOST_WIFI_DPP_SUPPORT (EH_HOST_CFG_WIFI_DPP && EH_HOST_IDF_GE(5, 5, 0))

// for generic DPP support
#define EH_HOST_DPP_SUPPORT      (EH_HOST_SUPP_DPP_SUPPORT || EH_HOST_WIFI_DPP_SUPPORT)

// this sets the maximum length of the URI we can receive to generate
// the QR code
#if EH_HOST_DPP_SUPPORT
#define EH_HOST_DPP_URI_LEN_MAX CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_EXT_DPP_URI_LEN_MAX
#endif

#endif /* __ESP_HOSTED_WIFI_CONFIG_H__ */
