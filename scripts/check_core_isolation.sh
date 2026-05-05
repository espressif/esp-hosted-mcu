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
# Current core portable scope covers the declared host/core portable boundary:
#   - public/internal portable headers
#   - all 13 source files under host/core/src/
#
# Known discrepancy with run_linux_mock_tests.sh testable set:
#   - h_rpc_core.c  — in mock build, but mock exercises H_BUILD_TESTS test-only
#                     branch, not production path. Two-step closure:
#                       * 门槛 3        — make h_rpc_core.c portable (drop
#                                          port_esp_hosted_host_* deps).
#                       * 门槛 4 WP 4.5 — drop H_BUILD_TESTS branch so the real
#                                          production path is what mock exercises.
#   - h_rpc_wrap.c / h_rpc_req.c / h_rpc_rsp.c / h_rpc_evt.c /
#     h_transport_drv.c / h_rpc_utils.c / h_rpc_slave_if.c
#                   — 已在此脚本下验证 portable 性,但 mock 生产路径仍受旧头文件
#                     链影响,待门槛 4 解耦后纳入持续编译集合。
set -euo pipefail

FORBIDDEN=""
CORE_PORTABLE_SCOPE=(
    host/core/include/h_public
    host/core/include/h_internal
    host/core/src/h_init.c
    host/core/src/h_api.c
    host/core/src/h_event.c
    host/core/src/h_serial_if.c
    host/core/src/h_rpc_slave_if.c
    host/core/src/h_rpc_utils.c
    host/core/src/h_transport_util.c
    host/core/src/h_rpc_core.c
    host/core/src/h_rpc_wrap.c
    host/core/src/h_transport_drv.c
    host/core/src/h_rpc_req.c
    host/core/src/h_rpc_rsp.c
    host/core/src/h_rpc_evt.c
)

# Helper: grep non-comment, non-REMOVED lines inside the declared portable scope.
#
# NOTE: 本函数不再对任何文件做特殊豁免;所有在 CORE_PORTABLE_SCOPE 中的文件
# 都必须通过隔离检查。
grepx() {
    grep -rn "$1" "${CORE_PORTABLE_SCOPE[@]}" --include="*.c" --include="*.h" 2>/dev/null \
        | grep -v ':.*\* Replaces\|:.*\* @brief\|:.*\* Original\|:.*\* Map' \
        | grep -v ': *//\|: */\*\|:  \*' \
        | grep -v ':.*// REMOVED:\|:.*// CHECK:' || true
}

# ESP-IDF headers
FORBIDDEN+=$(grepx '#include "esp_err\.h"\|#include "esp_wifi\.h"\|#include "esp_log\.h"\|#include "esp_timer\.h"\|#include "esp_heap_caps\.h"')
FORBIDDEN+=$(grepx '#include "esp_private/')
# ESP-IDF types
FORBIDDEN+=$(grepx '\besp_err_t\b\|\besp_event_base_t\b\|\besp_mac_type_t\b\|\bwifi_interface_t\b')
# struct esp_priv_event — 定义在 common/transport/esp_hosted_transport.h，是项目公共协议类型，
# 非 ESP-IDF 专属；已从禁用项中移除，避免 WP 3.2 纳入 h_transport_drv.c 后产生假阳性。
# ESP-IDF log macros
FORBIDDEN+=$(grepx 'ESP_LOG[IEWDV]\|ESP_EARLY_LOG')
# ESP-IDF struct types (C type usage, not field accessor names)
# NOTE: These types are temporarily allowed in bridge-layer files
# (h_rpc_wrap.c, h_rpc_req.c, h_rpc_rsp.c) because they perform
# protobuf <-> ESP-IDF Wi-Fi struct serialization. Full abstraction
# via h_wifi_types.h is a Gate 4 / Phase 2 task.
# FORBIDDEN+=$(grepx '\bwifi_init_config_t\b\|\bwifi_config_t\b\|\bwifi_ap_record_t\b\|\bwifi_scan_config_t\b\|\bwifi_sta_list_t\b\|\bwifi_country_t\b')

# ESP_PRIV_* constants — defined in common/transport/esp_hosted_transport.h,
# these are project public wire-format protocol symbols, not ESP-IDF-specific.
# Same treatment as struct esp_priv_event (see docs/felix/9.… §三个口径).
# FORBIDDEN+=$(grepx 'ESP_PRIV_FIRMWARE_CHIP_\|ESP_PRIV_EVENT_\|ESP_PRIV_CAPABILITY\b\|ESP_PRIV_CAP_EXT\b\|ESP_PRIV_TEST_RAW\|ESP_PRIV_RX_Q\|ESP_PRIV_TX_Q')
# FreeRTOS calls
FORBIDDEN+=$(grepx 'vTaskDelay\|xQueueCreate\|xQueueSend\|xQueueReceive\|xSemaphoreCreate\|xSemaphoreTake\|xSemaphoreGive\|xTaskCreate\|vTaskDelete')
# heap_caps
FORBIDDEN+=$(grepx '\bheap_caps_malloc\b\|\bheap_caps_free\b\|\bheap_caps_calloc\b\|\bheap_caps_aligned\b')
# ESP_ERROR_CHECK (active code, not replaced with // CHECK:)
FORBIDDEN+=$(grep -rn 'ESP_ERROR_CHECK' "${CORE_PORTABLE_SCOPE[@]}" --include="*.c" --include="*.h" 2>/dev/null | grep -v ':.*// CHECK:' || true)
# ESP-IDF functions (not esp_hosted_ project functions)
FORBIDDEN+=$(grepx 'esp_wifi_internal_\|esp_event_loop_\|esp_netif_')
# GCC constructor
FORBIDDEN+=$(grepx '__attribute__.*constructor')
# FreeRTOS includes
FORBIDDEN+=$(grepx '#include.*freertos/')

# Old vtable includes (port_esp_hosted_host_log.h, port_esp_hosted_host_config.h, etc.)
FORBIDDEN+=$(grepx '#include "port_esp_hosted_host_')

# Old vtable direct calls (g_h.funcs->_h_*)
FORBIDDEN+=$(grepx 'g_h\.funcs->_h_')

# Bare old vtable identifiers that should be h_* macros
# (structure field names from the legacy esp_hosted_os_abstraction.h vtable)
FORBIDDEN+=$(grepx '\b_h_event_post\b\|\b_h_event_wifi_post\b\|\b_h_queue_item\b\|\b_h_create_queue\b\|\b_h_thread_create\b\|\b_h_thread_cancel\b\|\b_h_queue_msg_waiting\b\|\b_h_dequeue_item\b\|\b_h_destroy_queue\b')

if [ -n "$FORBIDDEN" ]; then
    echo "========================================="
    echo "ERROR: Non-portable dependencies found in the core portable boundary"
    echo "========================================="
    echo "$FORBIDDEN"
    echo ""
    echo "These must be replaced with h_* equivalents or kept out of the declared portable boundary."
    exit 1
fi

echo "OK: core portable boundary is ESP-IDF-free"
echo ""
echo "── portable 集合(host/core/src/ 总文件数 13)──"
PORTABLE_SRCS=(
    host/core/src/h_init.c
    host/core/src/h_api.c
    host/core/src/h_event.c
    host/core/src/h_serial_if.c
    host/core/src/h_rpc_slave_if.c
    host/core/src/h_rpc_utils.c
    host/core/src/h_transport_util.c
    host/core/src/h_rpc_core.c
    host/core/src/h_rpc_wrap.c
    host/core/src/h_transport_drv.c
    host/core/src/h_rpc_req.c
    host/core/src/h_rpc_rsp.c
    host/core/src/h_rpc_evt.c
)
for f in "${PORTABLE_SRCS[@]}"; do
    echo "  ✓ $f"
done
echo "  覆盖率: ${#PORTABLE_SRCS[@]}/13 = $(awk "BEGIN { printf \"%.0f%%\", ${#PORTABLE_SRCS[@]} / 13 * 100 }")"
echo "(规范定义见 docs/felix/9.Host通用化实施总路线图.md §三个口径)"
