#!/bin/bash
# scripts/check_core_isolation.sh
# Verify host/core/ contains zero ESP-IDF dependencies in actual code.
# Legitimate exclusions:
#   - esp_hosted_* headers (project-internal common/ layer)
#   - ESP_STA_IF/ESP_AP_IF etc. (from common/transport/esp_hosted_header.h)
#   - h_rpc_wrap_api.c: esp_wifi_* function names (legacy API compatibility layer)
#   - RPC field accessors (resp_wifi_*, app_req->u.wifi_*) — protobuf internals
set -euo pipefail

FORBIDDEN=""

# Helper: grep non-comment, non-REMOVED lines, exclude API compat file
grepx() {
    grep -rn "$1" host/core/ --include="*.c" --include="*.h" 2>/dev/null \
        | grep -v 'host/core/src/h_rpc_wrap_api\.c' \
        | grep -v ':.*\* Replaces\|:.*\* @brief\|:.*\* Original\|:.*\* Map' \
        | grep -v ': *//\|: */\*\|:  \*' \
        | grep -v ':.*// REMOVED:\|:.*// CHECK:' || true
}

# ESP-IDF headers
FORBIDDEN+=$(grepx '#include "esp_err\.h"\|#include "esp_wifi\.h"\|#include "esp_log\.h"\|#include "esp_timer\.h"\|#include "esp_heap_caps\.h"')
FORBIDDEN+=$(grepx '#include "esp_private/')
# ESP-IDF types
FORBIDDEN+=$(grepx '\besp_err_t\b\|\besp_event_base_t\b\|\besp_mac_type_t\b\|\bwifi_interface_t\b')
FORBIDDEN+=$(grepx 'struct esp_priv_event')
# ESP-IDF log macros
FORBIDDEN+=$(grepx 'ESP_LOG[IEWDV]\|ESP_EARLY_LOG')
# ESP-IDF struct types (C type usage, not field accessor names)
FORBIDDEN+=$(grepx '\bwifi_init_config_t\b\|\bwifi_config_t\b\|\bwifi_ap_record_t\b\|\bwifi_scan_config_t\b\|\bwifi_sta_list_t\b\|\bwifi_country_t\b')
# ESP_PRIV_* chip ID and event type constants
FORBIDDEN+=$(grepx 'ESP_PRIV_FIRMWARE_CHIP_\|ESP_PRIV_EVENT_\|ESP_PRIV_CAPABILITY\b\|ESP_PRIV_CAP_EXT\b\|ESP_PRIV_TEST_RAW\|ESP_PRIV_RX_Q\|ESP_PRIV_TX_Q')
# FreeRTOS calls
FORBIDDEN+=$(grepx 'vTaskDelay\|xQueueCreate\|xQueueSend\|xQueueReceive\|xSemaphoreCreate\|xSemaphoreTake\|xSemaphoreGive\|xTaskCreate\|vTaskDelete')
# heap_caps
FORBIDDEN+=$(grepx '\bheap_caps_malloc\b\|\bheap_caps_free\b\|\bheap_caps_calloc\b\|\bheap_caps_aligned\b')
# ESP_ERROR_CHECK (active code, not replaced with // CHECK:)
FORBIDDEN+=$(grep -rn 'ESP_ERROR_CHECK' host/core/ --include="*.c" --include="*.h" 2>/dev/null | grep -v ':.*// CHECK:' | grep -v 'h_rpc_wrap_api\.c' || true)
# ESP-IDF functions (not esp_hosted_ project functions)
FORBIDDEN+=$(grepx 'esp_wifi_internal_\|esp_event_loop_\|esp_netif_')
# GCC constructor
FORBIDDEN+=$(grepx '__attribute__.*constructor')
# FreeRTOS includes
FORBIDDEN+=$(grepx '#include.*freertos/')

if [ -n "$FORBIDDEN" ]; then
    echo "========================================="
    echo "ERROR: Non-portable dependencies found in host/core/"
    echo "========================================="
    echo "$FORBIDDEN"
    echo ""
    echo "These must be replaced with h_* equivalents or moved to host/port/<platform>/"
    exit 1
fi

echo "OK: host/core/ is ESP-IDF-free and portable"
