#!/bin/bash
# scripts/check_core_isolation.sh
# Verify host/core/ contains zero ESP-IDF dependencies in actual code.
set -euo pipefail

FORBIDDEN=""

# Helper: grep non-comment code lines only (filters out // and /* */ and * comments)
grepx() {
    grep -rn "$1" host/core/ --include="*.c" --include="*.h" 2>/dev/null \
        | grep -v ':.*\* Replaces\|:.*\* @brief\|:.*\* Original\|:.*\* Repl' \
        | grep -v ': *//\|: */\*' \
        | grep -v ':  \*' || true
}

# ESP-IDF headers (NOT esp_hosted_* which are project headers)
FORBIDDEN+=$(grepx '#include.*\besp_err\.h\|#include.*\besp_wifi\.h\|#include.*esp_private/')
# ESP-IDF types
FORBIDDEN+=$(grepx '\besp_err_t\b\|\besp_event_base_t\b\|\besp_mac_type_t\b')
# ESP-IDF log macros
FORBIDDEN+=$(grepx 'ESP_LOG[IEWDV]\|ESP_EARLY_LOG')
# ESP-IDF Wi-Fi types (in code, not in "Replaces" doc comments)
FORBIDDEN+=$(grepx '\bwifi_init_config_t\b\|\bwifi_config_t\b\|\bwifi_ap_record_t\b\|\bwifi_scan_config_t\b\|\bwifi_sta_list_t\b')
# FreeRTOS direct calls
FORBIDDEN+=$(grepx 'vTaskDelay\|xQueueCreate\|xQueueSend\|xQueueReceive\|xSemaphoreCreate\|xSemaphoreTake\|xSemaphoreGive\|xTaskCreate\|vTaskDelete')
# heap_caps direct calls
FORBIDDEN+=$(grepx '\bheap_caps_malloc\b\|\bheap_caps_free\b\|\bheap_caps_calloc\b\|\bheap_caps_aligned\b')
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
