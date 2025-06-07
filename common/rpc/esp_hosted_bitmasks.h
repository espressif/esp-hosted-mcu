// Copyright 2025 Espressif Systems (Shanghai) PTE LTD
/* SPDX-License-Identifier: GPL-2.0-only OR Apache-2.0 */

/* Bitmask definitions used in RPC data */

#ifndef __ESP_HOSTED_BITMASKS__H
#define __ESP_HOSTED_BITMASKS__H

/* For host builds, include ESP-specific wifi config */
#ifdef CONFIG_ESP_HOSTED_ENABLED
#include "esp_hosted_wifi_config.h"
#endif

/* Default fallbacks for non-ESP host builds */
#ifndef H_WIFI_DUALBAND_SUPPORT
  #define H_WIFI_DUALBAND_SUPPORT 0
#endif

#ifndef H_WIFI_VHT_FIELDS_AVAILABLE
  #define H_WIFI_VHT_FIELDS_AVAILABLE 0
#endif

#ifndef H_WIFI_NEW_RESERVED_FIELD_NAMES
  #define H_WIFI_NEW_RESERVED_FIELD_NAMES 0
#endif

#define H_SET_BIT(pos, val)                       (val|=(1<<pos))

#define H_GET_BIT(pos, val)                       (val&(1<<pos)? 1: 0)

/* Station config bitmasks */
enum {
	STA_RM_ENABLED_BIT         = 0,
	STA_BTM_ENABLED_BIT        = 1,
	STA_MBO_ENABLED_BIT        = 2,
	STA_FT_ENABLED_BIT         = 3,
	STA_OWE_ENABLED_BIT        = 4,
	STA_TRASITION_DISABLED_BIT = 5,
	STA_MAX_USED_BIT           = 6,
};

#define WIFI_CONFIG_STA_RESERVED_BITMASK          0xFFC0

#define WIFI_CONFIG_STA_GET_RESERVED_VAL(num)                                   \
    ((num&WIFI_CONFIG_STA_RESERVED_BITMASK)>>STA_MAX_USED_BIT)

#define WIFI_CONFIG_STA_SET_RESERVED_VAL(reserved_in,num_out)                   \
    (num_out|=(reserved_in <<  STA_MAX_USED_BIT));

enum {
	WIFI_SCAN_AP_REC_phy_11b_BIT       = 0,
	WIFI_SCAN_AP_REC_phy_11g_BIT       = 1,
	WIFI_SCAN_AP_REC_phy_11n_BIT       = 2,
	WIFI_SCAN_AP_REC_phy_lr_BIT        = 3,
	WIFI_SCAN_AP_REC_phy_11ax_BIT      = 4,
	WIFI_SCAN_AP_REC_wps_BIT           = 5,
	WIFI_SCAN_AP_REC_ftm_responder_BIT = 6,
	WIFI_SCAN_AP_REC_ftm_initiator_BIT = 7,
	WIFI_SCAN_AP_REC_phy_11a_BIT       = 8,
	WIFI_SCAN_AP_REC_phy_11ac_BIT      = 9,
	WIFI_SCAN_AP_REC_MAX_USED_BIT      = 10,
};

#define WIFI_SCAN_AP_RESERVED_BITMASK             0xFC00

#define WIFI_SCAN_AP_GET_RESERVED_VAL(num)                                      \
    ((num&WIFI_SCAN_AP_RESERVED_BITMASK)>>WIFI_SCAN_AP_REC_MAX_USED_BIT)

#define WIFI_SCAN_AP_SET_RESERVED_VAL(reserved_in,num_out)                      \
    (num_out|=(reserved_in <<  WIFI_SCAN_AP_REC_MAX_USED_BIT));

enum {
	WIFI_STA_INFO_phy_11b_BIT       = 0,
	WIFI_STA_INFO_phy_11g_BIT       = 1,
	WIFI_STA_INFO_phy_11n_BIT       = 2,
	WIFI_STA_INFO_phy_lr_BIT        = 3,
	WIFI_STA_INFO_phy_11ax_BIT      = 4,
	WIFI_STA_INFO_is_mesh_child_BIT = 5,
	WIFI_STA_INFO_MAX_USED_BIT      = 6,
};

#define WIFI_STA_INFO_RESERVED_BITMASK             0xFFC0

#define WIFI_STA_INFO_GET_RESERVED_VAL(num)                                      \
    ((num&WIFI_STA_INFO_RESERVED_BITMASK)>>WIFI_STA_INFO_MAX_USED_BIT)

#define WIFI_STA_INFO_SET_RESERVED_VAL(reserved_in,num_out)                      \
    (num_out|=(reserved_in <<  WIFI_STA_INFO_MAX_USED_BIT));

/* WIFI HE AP Info bitmasks */
enum {
	/* WIFI_HE_AP_INFO_BSS_COLOR is six bits wide */
	WIFI_HE_AP_INFO_partial_bss_color_BIT  = 6,
	WIFI_HE_AP_INFO_bss_color_disabled_BIT = 7,
	WIFI_HE_AP_INFO_MAX_USED_BIT           = 8,
};

#define WIFI_HE_AP_INFO_BSS_COLOR_BITS 0x3F

/* WIFI HE Station Config bitmasks */
enum {
	WIFI_HE_STA_CONFIG_he_dcm_set_BIT                                     = 0,
	/* WIFI_HE_STA_CONFIG_he_dcm_max_constellation_tx is two bits wide */
	WIFI_HE_STA_CONFIG_he_dcm_max_constellation_tx_BITS                   = 1,
	/* WIFI_HE_STA_CONFIG_he_dcm_max_constellation_rx is two bits wide */
	WIFI_HE_STA_CONFIG_he_dcm_max_constellation_rx_BITS                   = 3,
	WIFI_HE_STA_CONFIG_he_mcs9_enabled_BIT                                = 5,
	WIFI_HE_STA_CONFIG_he_su_beamformee_disabled_BIT                      = 6,
	WIFI_HE_STA_CONFIG_he_trig_su_bmforming_feedback_disabled_BIT         = 7,
	WIFI_HE_STA_CONFIG_he_trig_mu_bmforming_partial_feedback_disabled_BIT = 8,
	WIFI_HE_STA_CONFIG_he_trig_cqi_feedback_disabled_BIT                  = 9,

#if H_WIFI_VHT_FIELDS_AVAILABLE
	/* VHT fields (ESP-IDF 5.5.0+) */
	WIFI_VHT_STA_CONFIG_vht_su_beamformee_disabled_BIT                    = 10,
	WIFI_VHT_STA_CONFIG_vht_mu_beamformee_disabled_BIT                    = 11,
	WIFI_VHT_STA_CONFIG_vht_mcs8_enabled_BIT                              = 12,
	WIFI_HE_STA_CONFIG_MAX_USED_BIT                                       = 13,
#else
	WIFI_HE_STA_CONFIG_MAX_USED_BIT                                       = 10,
#endif
};

#if H_WIFI_VHT_FIELDS_AVAILABLE
#define WIFI_HE_STA_CONFIG_RESERVED_BITMASK 0xE000
#else
#define WIFI_HE_STA_CONFIG_RESERVED_BITMASK 0xFC00
#endif

#define WIFI_HE_STA_GET_RESERVED_VAL(num)                                      \
    ((num&WIFI_HE_STA_CONFIG_RESERVED_BITMASK)>>WIFI_HE_STA_CONFIG_MAX_USED_BIT)

#define WIFI_HE_STA_SET_RESERVED_VAL(reserved_in,num_out)                      \
    (num_out|=(reserved_in <<  WIFI_HE_STA_CONFIG_MAX_USED_BIT));

/* Aliases for compatibility with RPC code */
#define WIFI_HE_STA_CONFIG_GET_RESERVED_VAL WIFI_HE_STA_GET_RESERVED_VAL
#define WIFI_HE_STA_CONFIG_SET_RESERVED_VAL WIFI_HE_STA_SET_RESERVED_VAL

#endif
