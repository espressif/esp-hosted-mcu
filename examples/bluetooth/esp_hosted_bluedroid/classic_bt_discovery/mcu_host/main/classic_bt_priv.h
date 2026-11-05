/* SPDX-License-Identifier: Apache-2.0 */
/* Compile-time guard: Classic BT (BR/EDR) only exists on ESP32 and ESP32-S31. */

#ifndef CLASSIC_BT_PRIV_H_
#define CLASSIC_BT_PRIV_H_

#include "sdkconfig.h"

#if !defined(CONFIG_ESP_HOSTED_CP_HAS_BT_CLASSIC)
#  error \
"\n\n" \
"================================================================\n" \
"  classic_bt_discovery needs an ESP32 or ESP32-S31 co-processor.\n" \
"  Other co-processors have no Classic BT (BR/EDR).\n" \
"\n" \
"  Fix: idf.py menuconfig, select ESP32 or ESP32-S31 in:\n" \
"      Component config -> Wi-Fi Remote -> choose slave target\n" \
"  If Wi-Fi Remote is disabled, use:\n" \
"      Component config -> ESP-Hosted -> Configure host ->\n" \
"      Paired coprocessor\n" \
"  Or in sdkconfig.defaults: CONFIG_SLAVE_IDF_TARGET_ESP32S31=y\n" \
"================================================================\n"
#endif

#if !defined(CONFIG_BT_CLASSIC_ENABLED)
#  error \
"\n\n" \
"================================================================\n" \
"  classic_bt_discovery needs Bluedroid Classic BT enabled.\n" \
"\n" \
"  Fix: in sdkconfig.defaults:\n" \
"      CONFIG_BT_ENABLED=y\n" \
"      CONFIG_BT_BLUEDROID_ENABLED=y\n" \
"      CONFIG_BT_CLASSIC_ENABLED=y\n" \
"================================================================\n"
#endif

#endif
