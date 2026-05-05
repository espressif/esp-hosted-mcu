#!/bin/bash
# scripts/check_core_isolation.sh
#
# Verifies the **portable** subset of host/core stays ESP-IDF-free.
#
# Definition of "portable" (canonical source: docs/felix/9.Host通用化实施总路线图.md
# §"三个口径的明确定义"):
#   通过本脚本隔离检查的源文件,即不依赖 port_esp_hosted_host_*、
#   ESP_LOG*、esp_* 头文件、FreeRTOS 直接调用、heap_caps_*、
#   __attribute__((constructor)) 等平台专属符号。
#
# Current Phase 1 scope is intentionally narrower than all of host/core/:
#   - public/internal portable headers
#   - core entry points and wrappers that are already meant to be generic
#
# Transitional RPC/transport implementation files remain out of scope until
# their legacy port/log/config dependencies are fully removed (门槛 3).
#
# Known discrepancy with run_linux_mock_tests.sh testable set:
#   - h_api.c       — portable, but not in mock build (depends on h_rpc_wrap.c
#                     which is not yet portable). Will align after 门槛 3.
#   - h_rpc_core.c  — in mock build, but mock exercises H_BUILD_TESTS test-only
#                     branch, not production path. Two-step closure:
#                       * 门槛 3        — make h_rpc_core.c portable (drop
#                                          port_esp_hosted_host_* deps).
#                       * 门槛 4 WP 4.5 — drop H_BUILD_TESTS branch so the real
#                                          production path is what mock exercises.
set -euo pipefail

FORBIDDEN=""
PHASE1_SCOPE=(
    host/core/include/h_public
    host/core/include/h_internal
    host/core/src/h_init.c
    host/core/src/h_api.c
    host/core/src/h_event.c
    host/core/src/h_serial_if.c
)

# Helper: grep non-comment, non-REMOVED lines inside the current Phase 1 scope.
#
# NOTE: 函数末尾两条 `h_wifi_types.h` 豁免规则允许该头文件在 ESP_PLATFORM 下
# 直通 ESP-IDF 类型 — 这是门槛 2(公共类型收口)的待办项(见
# docs/felix/9.Host通用化实施总路线图.md §门槛 2,WP 2.5)。门槛 2 完成后,
# 这两条豁免必须从 grepx() 内移除,届时该头文件应通过 isolation 检查。
grepx() {
    grep -rn "$1" "${PHASE1_SCOPE[@]}" --include="*.c" --include="*.h" 2>/dev/null \
        | grep -v ':.*\* Replaces\|:.*\* @brief\|:.*\* Original\|:.*\* Map' \
        | grep -v ': *//\|: */\*\|:  \*' \
        | grep -v ':.*// REMOVED:\|:.*// CHECK:' \
        | grep -v 'host/core/include/h_public/h_wifi_types\.h:.*#include "esp_wifi\.h"' \
        | grep -v 'host/core/include/h_public/h_wifi_types\.h:.*typedef wifi_.*_t' || true
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
FORBIDDEN+=$(grep -rn 'ESP_ERROR_CHECK' "${PHASE1_SCOPE[@]}" --include="*.c" --include="*.h" 2>/dev/null | grep -v ':.*// CHECK:' || true)
# ESP-IDF functions (not esp_hosted_ project functions)
FORBIDDEN+=$(grepx 'esp_wifi_internal_\|esp_event_loop_\|esp_netif_')
# GCC constructor
FORBIDDEN+=$(grepx '__attribute__.*constructor')
# FreeRTOS includes
FORBIDDEN+=$(grepx '#include.*freertos/')

if [ -n "$FORBIDDEN" ]; then
    echo "========================================="
    echo "ERROR: Non-portable dependencies found in the Phase 1 portable boundary"
    echo "========================================="
    echo "$FORBIDDEN"
    echo ""
    echo "These must be replaced with h_* equivalents or kept out of the declared Phase 1 boundary."
    exit 1
fi

echo "OK: Phase 1 portable boundary is ESP-IDF-free"
echo ""
echo "── portable 集合(host/core/src/ 总文件数 14)──"
PORTABLE_SRCS=(
    host/core/src/h_init.c
    host/core/src/h_api.c
    host/core/src/h_event.c
    host/core/src/h_serial_if.c
)
for f in "${PORTABLE_SRCS[@]}"; do
    echo "  ✓ $f"
done
echo "  覆盖率: ${#PORTABLE_SRCS[@]}/14 = $(awk "BEGIN { printf \"%.0f%%\", ${#PORTABLE_SRCS[@]} / 14 * 100 }")"
echo "(规范定义见 docs/felix/9.Host通用化实施总路线图.md §三个口径)"
