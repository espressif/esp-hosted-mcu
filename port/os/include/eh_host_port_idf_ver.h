/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EH_HOST_PORT_IDF_VER_H
#define EH_HOST_PORT_IDF_VER_H

/* On non-IDF builds, supply ESP_IDF_VERSION_VAL ourselves so version-
 * gated IDF code sees all features "present". */
#if defined(__has_include)
#  if __has_include("esp_idf_version.h")
#    include "esp_idf_version.h"
#  endif
#endif

#ifndef ESP_IDF_VERSION_VAL
#  define ESP_IDF_VERSION_VAL(major, minor, patch) \
       (((major) << 16) | ((minor) << 8) | (patch))
#endif

#ifndef ESP_IDF_VERSION
   /* Linux / non-IDF default: pretend we're on the newest IDF so every
    * feature gate compiled below evaluates the "latest" branch. CONFIG_*
    * symbols (CONFIG_ESP_WIFI_* plus Hosted Wi-Fi feature toggles) that gate
    * actual code paths still drive whether the code is compiled in. */
#  define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(99, 0, 0)
#endif

/* EH_HOST_IDF_GE(5, 5, 6): IDF >= v5.5.6.  EH_HOST_IDF_LT(6, 0, 0): IDF < v6.0.0.
 * EH_HOST_IDF_GE_LT(5, 5, 6,  6, 0, 0): IDF >= v5.5.6 and < v6.0.0.
 * Use the last for a change that reached an older release branch in a patch
 * release. All work in #if and in C expressions. */
#define EH_HOST_IDF_GE(ma, mi, pa) \
    (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(ma, mi, pa))
#define EH_HOST_IDF_LT(ma, mi, pa) \
    (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(ma, mi, pa))
#define EH_HOST_IDF_GE_LT(ma, mi, pa, mb, nb, pb) \
    (EH_HOST_IDF_GE(ma, mi, pa) && EH_HOST_IDF_LT(mb, nb, pb))

/* sdmmc_host_deinit_slot() */
#define EH_HOST_GOT_SDMMC_DEINIT_SLOT           EH_HOST_IDF_GE(5, 4, 0)

/* esp_sleep_get_wakeup_causes() bitmap */
#define EH_HOST_GOT_WAKEUP_CAUSES_BITMAP        EH_HOST_IDF_GE(6, 0, 0)

#endif /* EH_HOST_PORT_IDF_VER_H */
