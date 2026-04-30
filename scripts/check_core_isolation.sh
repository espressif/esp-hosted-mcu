#!/bin/bash
# scripts/check_core_isolation.sh
# Verify host/core/ contains zero ESP-IDF dependencies

FORBIDDEN=""

# ESP-IDF headers
FORBIDDEN+=$(grep -rn '#include.*esp_' host/core/ --include="*.c" --include="*.h" 2>/dev/null || true)
# ESP-IDF types
FORBIDDEN+=$(grep -rn 'esp_err_t\|esp_event_base_t\|esp_mac_type_t' host/core/ --include="*.c" --include="*.h" 2>/dev/null || true)
# ESP-IDF log macros
FORBIDDEN+=$(grep -rn 'ESP_LOG[IEWDV]' host/core/ --include="*.c" --include="*.h" 2>/dev/null || true)
# ESP-IDF Wi-Fi types (pre-migration)
FORBIDDEN+=$(grep -rn 'wifi_init_config_t\|wifi_config_t\|wifi_ap_record_t' host/core/ --include="*.c" --include="*.h" 2>/dev/null || true)
# FreeRTOS direct calls
FORBIDDEN+=$(grep -rn 'vTaskDelay\|xQueueCreate\|xSemaphoreCreate' host/core/ --include="*.c" --include="*.h" 2>/dev/null || true)
# heap_caps direct calls
FORBIDDEN+=$(grep -rn 'heap_caps_malloc\|heap_caps_free\|heap_caps_get' host/core/ --include="*.c" --include="*.h" 2>/dev/null || true)
# GCC constructor (non-portable)
FORBIDDEN+=$(grep -rn '__attribute__.*constructor' host/core/ --include="*.c" --include="*.h" 2>/dev/null || true)

if [ -n "$FORBIDDEN" ]; then
    echo "ERROR: ESP-IDF or non-portable dependencies found in host/core/:"
    echo "$FORBIDDEN"
    exit 1
fi

echo "OK: host/core/ is ESP-IDF-free and portable"
