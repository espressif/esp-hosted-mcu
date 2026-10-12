/* SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD */
/* SPDX-License-Identifier: Apache-2.0 */

/* IDF version gates and feature availability macros for the CP. */

#ifndef ESP_HOSTED_CP_IDF_COMPAT_H
#define ESP_HOSTED_CP_IDF_COMPAT_H

#include "esp_idf_version.h"

#define EH_CP_IDF_GE(ma, mi, pa) \
    (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(ma, mi, pa))

#define EH_CP_IDF_LT(ma, mi, pa) \
    (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(ma, mi, pa))

#define EH_CP_IDF_GE_LT(ma, mi, pa, mb, nb, pb) \
    (EH_CP_IDF_GE(ma, mi, pa) && EH_CP_IDF_LT(mb, nb, pb))

/* IDF 5 API: QueueHandle_t, uint32_t is long, no periph_ctrl.h */
#define EH_CP_IDF_V5_API                         EH_CP_IDF_GE(5, 0, 0)

/* ESP32 UART transport needs ring buffer in IRAM on IDF 5.x */
#define EH_CP_UART_CHECK_RINGBUF_IRAM            EH_CP_IDF_GE_LT(5, 0, 0,  6, 0, 0)

/* NimBLE ble_hci_trans_cfg_hs() */
#define EH_CP_GOT_BLE_HCI_TRANS                  EH_CP_IDF_LT(5, 3, 1)

/* esp_ota_check_image_validity() */
#define EH_CP_OTA_CHECK_IMAGE_VALIDITY           EH_CP_IDF_GE(6, 1, 0)

#ifdef CONFIG_SOC_WIFI_HE_SUPPORT
#  define EH_CP_SOC_WIFI_HE                      1
#else
#  define EH_CP_SOC_WIFI_HE                      0
#endif

/* wifi_ap_record_t::akm_dpp; release/v5.5 has it, no v5.5 tag does yet */
#define EH_CP_WIFI_GOT_AP_REC_AKM_DPP            EH_CP_IDF_GE(6, 0, 3)

/* wifi_event_ap_stadisconnected_t::reason */
#define EH_CP_WIFI_GOT_AP_STADISCONN_REASON      EH_CP_IDF_GE(5, 1, 1)

/* esp_wifi_set/get_protocols() and _bandwidths() */
#define EH_CP_WIFI_DUALBAND                      EH_CP_IDF_GE(5, 4, 0)

/* wifi_scan_threshold_t::rssi_5g_adjustment */
#define EH_CP_WIFI_GOT_RSSI_5G_ADJ               EH_CP_IDF_GE(5, 4, 0)

/* wifi_scan_config_t::coex_background_scan */
#define EH_CP_WIFI_GOT_COEX_BG_SCAN              EH_CP_IDF_GE(5, 4, 0)

/* wifi_sta_config_t: vht_su/mu_beamformee_disabled, vht_mcs8_enabled */
#define EH_CP_WIFI_GOT_STA_VHT_CFG               EH_CP_IDF_GE(5, 5, 0)

/* wifi_ap_config_t: sae_ext, bss_max_idle_cfg, gtk_rekey_interval */
#define EH_CP_WIFI_GOT_AP_SAE_EXT                EH_CP_IDF_GE(5, 5, 0)

/* wifi_sta_config_t::max_bandwidth_negotiation_enabled_2g/5g */
#define EH_CP_WIFI_GOT_STA_MAX_BW_NEG \
    (EH_CP_IDF_GE_LT(5, 5, 6,  6, 0, 0) || \
     EH_CP_IDF_GE_LT(6, 0, 4,  6, 1, 0) || \
     EH_CP_IDF_GE(6, 1, 1))

/* wifi_scan_default_params_t::max_offchan_duration_ms */
#define EH_CP_WIFI_GOT_SCAN_MAX_OFFCHAN          EH_CP_IDF_GE(6, 1, 1)

#ifdef CONFIG_SOC_WIFI_SUPPORT_5G
/* wifi_country_t::wifi_5g_channel_mask: SoC capability, not IDF version */
#  define EH_CP_WIFI_GOT_5G_CHANNEL_MASK        1
#else
#  define EH_CP_WIFI_GOT_5G_CHANNEL_MASK        0
#endif

/* wifi_ap_config_t::wpa3_compatible_mode */
#define EH_CP_WIFI_GOT_AP_WPA3_COMPATIBLE_MODE   EH_CP_IDF_GE(6, 0, 0)

/* wifi_sta_config_t::disable_wpa3_compatible_mode */
#define EH_CP_WIFI_GOT_WPA3_COMPATIBLE_MODE      EH_CP_IDF_GE(6, 0, 0)

/* HE API changed after IDF v5.3 */
#define EH_CP_WIFI_HE_GT_IDF_5_3                 (EH_CP_SOC_WIFI_HE && EH_CP_IDF_GE(5, 3, 1))

/* wifi_twt_config_t::twt_enable_keep_alive */
#define EH_CP_WIFI_GOT_TWT_KEEP_ALIVE            EH_CP_IDF_GE(5, 3, 2)

/* reserved/he_reserved renamed to reserved1/reserved2 */
#define EH_CP_WIFI_NEW_RESERVED_FIELDS           EH_CP_IDF_GE(5, 5, 0)

/* wifi_ap_config_t::transition_disable */
#define EH_CP_WIFI_GOT_AP_TRANSITION_DISABLE \
    (EH_CP_IDF_GE_LT(5, 3, 3,  5, 4, 0) || EH_CP_IDF_GE(5, 4, 1))

/* WIFI_ENABLE_CACHE_TX_BUFFER fallback */
#if EH_CP_IDF_LT(5, 3, 3)
#  ifndef WIFI_ENABLE_CACHE_TX_BUFFER
#    define WIFI_ENABLE_CACHE_TX_BUFFER          WIFI_ENABLE_SPIRAM
#  endif
#endif

/* spi_slave_enable() / spi_slave_disable() */
#define EH_CP_IDF_SPI_SLAVE_EN_DIS               EH_CP_IDF_GE(5, 5, 0)

#ifdef CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
#  define EH_CP_WIFI_ENTERPRISE                  1
#else
#  define EH_CP_WIFI_ENTERPRISE                  0
#endif

/* esp_eap_client_set_eap_methods(); not in v5.5.0 */
#define EH_CP_WIFI_GOT_SET_EAP_METHODS \
    (EH_CP_IDF_GE_LT(5, 3, 4,  5, 4, 0) || \
     EH_CP_IDF_GE_LT(5, 4, 3,  5, 5, 0) || \
     EH_CP_IDF_GE(5, 5, 1))

/* esp_eap_client_set_domain_name() */
#define EH_CP_WIFI_GOT_EAP_SET_DOMAIN_NAME \
    (EH_CP_IDF_GE_LT(5, 3, 4,  5, 4, 0) || EH_CP_IDF_GE(5, 4, 3))

/* esp_wifi_set_okc_support() */
#define EH_CP_WIFI_GOT_EAP_OKC                   EH_CP_IDF_GE(5, 5, 0)

#ifdef CONFIG_ESP_WIFI_DPP_SUPPORT
#  define EH_CP_CFG_WIFI_DPP                     1
#else
#  define EH_CP_CFG_WIFI_DPP                     0
#endif

/* esp_supp_dpp_* callback API, removed in IDF v6.0 */
#define EH_CP_WIFI_SUPP_DPP                      (EH_CP_CFG_WIFI_DPP && EH_CP_IDF_LT(6, 0, 0))

/* Wi-Fi DPP events */
#define EH_CP_WIFI_DPP                           (EH_CP_CFG_WIFI_DPP && EH_CP_IDF_GE(5, 5, 0))

/* either DPP path */
#define EH_CP_WIFI_DPP_ANY                       (EH_CP_WIFI_SUPP_DPP || EH_CP_WIFI_DPP)

#ifdef CONFIG_ESP_HOSTED_ALLOW_FULL_APP_DESC
#  define EH_CP_ALLOW_FULL_APP_DESC              1
#else
#  define EH_CP_ALLOW_FULL_APP_DESC              0
#endif

/* esp_app_desc_t::min/max_efuse_blk_rev_full */
#define EH_CP_GOT_EFUSE_BLK_REV_APP_DESC         (EH_CP_ALLOW_FULL_APP_DESC && EH_CP_IDF_GE(5, 3, 2))

/* upstream-name alias */
#define EH_CP_GOT_EFUSE_BLK_REV_FULL_APP_DESC    EH_CP_GOT_EFUSE_BLK_REV_APP_DESC

/* esp_app_desc_t::mmu_page_size */
#define EH_CP_GOT_MMU_PAGE_SIZE_APP_DESC         (EH_CP_ALLOW_FULL_APP_DESC && EH_CP_IDF_GE(5, 4, 0))

/* upstream-name alias */
#define EH_CP_GOT_MMU_PAGE_SIZE_FULL_APP_DESC    EH_CP_GOT_MMU_PAGE_SIZE_APP_DESC

#ifdef CONFIG_ESP_HOSTED_USE_MEMPOOL
#  define EH_CP_USE_MEMPOOL                      1
#else
#  define EH_CP_USE_MEMPOOL                      0
#endif

#endif /* ESP_HOSTED_CP_IDF_COMPAT_H */
