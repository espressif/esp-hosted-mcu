#!/bin/bash
# scripts/run_linux_mock_tests.sh
# CI Tier 2 — Build and run all unit tests on Linux mock port.
#
# Definition of "testable"(canonical source: docs/felix/9.Host通用化实施总路线图.md
# §"三个口径的明确定义"):
#   被本脚本持续编译且测试用例真实执行其**生产代码路径**(不依赖 H_BUILD_TESTS
#   测试专用分支)。
#
# Known discrepancy with check_core_isolation.sh portable set:
#   - h_api.c       — portable, included after 门槛 3 (needs rpc_wifi_* stubs
#                     from h_rpc_wrap.c which is portable but not mock-compilable
#                     due to ESP-IDF Wi-Fi type dependencies).
#   - h_rpc_core.c  — included here, but currently exercises H_BUILD_TESTS
#                     test-only branch instead of production path. Closure:
#                       * 门槛 3        — DONE: h_rpc_core.c is portable (13/13).
#                       * 门槛 4 WP 4.5 — drop H_BUILD_TESTS branch; mock then
#                                          covers the real production path.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$ROOT_DIR"

mkdir -p build

# NOTE: h_rpc_core.c 当前在 H_BUILD_TESTS 下走测试专用分支,生产路径未在 mock
# 验证。这一事实纳入"已验证生产路径"口径(见上方头部说明)。两步收口:
#   - 门槛 3        — 让 h_rpc_core.c 满足 portable 口径(去 port_esp_hosted_host_* 依赖)
#   - 门槛 4 WP 4.5 — 取消 H_BUILD_TESTS 测试专用分支,mock 直接覆盖生产路径
#
# 门槛 3 完成后新增可编译文件:
#   - h_api.c            — portable, 但依赖 h_rpc_wrap.c 中的 rpc_wifi_* 符号;
#                          通过 tests/stubs/rpc_wifi_stubs.c 提供桩定义解决链接。
#   - h_transport_util.c — portable, 依赖 common/mempool/include 中的 hosted_mem_cap_t;
#                          通过添加 include 路径和最小桩头解决编译。
#
# 门槛 3 完成后仍无法在 mock 下编译生产路径的文件(深度依赖 ESP-IDF 旧头文件系统):
#   - h_rpc_wrap.c, h_rpc_req.c, h_rpc_rsp.c, h_rpc_evt.c
#   - h_transport_drv.c, h_rpc_utils.c, h_rpc_slave_if.c
#   这些文件已通过隔离脚本验证 portable 性(13/13), 但生产路径依赖 transport_drv.h、
#   rpc_core.h 等旧头文件, 这些头文件链又依赖 ESP-IDF 专属头文件(freertos/、esp_*.h)。
#   待 门槛 4 完成旧头文件系统解耦后, mock 才能覆盖其生产路径。
CORE_SRCS="host/core/src/h_init.c host/core/src/h_rpc_core.c host/core/src/h_serial_if.c host/core/src/h_event.c host/core/src/h_api.c host/core/src/h_transport_util.c"
PORT_SRCS="host/port/linux/src/h_osal.c host/port/linux/src/h_event.c host/port/linux/src/h_transport_mock.c"
TEST_SRCS="tests/test_runner.c tests/test_osal.c tests/test_event.c tests/test_transport.c tests/test_rpc_core.c tests/unity/unity.c tests/stubs/rpc_wifi_stubs.c"

INC_FLAGS="-I host/core/include/h_public -I host/core/include/h_internal -I host/port/linux -I host/port/include -I tests/unity -I tests/stubs -I common/mempool/include -I host/drivers/transport"

# ── 编译前打印两个口径下的集合 ──
# 注:本脚本的"持续编译集合"包含 h_rpc_core.c,但它走 H_BUILD_TESTS 测试专用分支,
# 因此不属 9 号文档定义下的 testable / 已验证生产路径口径。下面分别打印这两个集合,
# 让脚本输出与 9 号 canonical 定义一致(避免"testable" 一词被赋多重含义)。
echo "── mock 持续编译集合(host/core/src/ 总文件数 13)──"
MOCK_BUILD_CORE=(
    host/core/src/h_init.c
    host/core/src/h_event.c
    host/core/src/h_serial_if.c
    host/core/src/h_api.c            # 门槛 3 新增: 生产路径编译通过
    host/core/src/h_transport_util.c # 门槛 3 新增: 生产路径编译通过
    host/core/src/h_rpc_core.c       # H_BUILD_TESTS 测试专用分支,不计入 testable
)
for f in "${MOCK_BUILD_CORE[@]}"; do
    echo "  ✓ $f"
done
echo "  编译覆盖率: ${#MOCK_BUILD_CORE[@]}/13 = $(awk "BEGIN { printf \"%.0f%%\", ${#MOCK_BUILD_CORE[@]} / 13 * 100 }")"
echo ""
echo "── testable / 已验证生产路径(9 号文档规范口径)──"
TESTABLE_CORE=(
    host/core/src/h_init.c
    host/core/src/h_event.c
    host/core/src/h_serial_if.c
    host/core/src/h_api.c            # 门槛 3 新增
    host/core/src/h_transport_util.c # 门槛 3 新增
)
for f in "${TESTABLE_CORE[@]}"; do
    echo "  ✓ $f"
done
echo "  覆盖率: ${#TESTABLE_CORE[@]}/13 = $(awk "BEGIN { printf \"%.0f%%\", ${#TESTABLE_CORE[@]} / 13 * 100 }")"
echo "  注:h_rpc_core.c 当前在 H_BUILD_TESTS 下走测试专用分支,不计入此集合"
echo "  注:其余 7 个文件(h_rpc_wrap.c/h_rpc_req.c/h_rpc_rsp.c/h_rpc_evt.c/"
echo "       h_transport_drv.c/h_rpc_utils.c/h_rpc_slave_if.c) 已通过隔离脚本"
echo "       验证 portable 性(13/13),但生产路径深度依赖旧头文件系统(transport_drv.h/"
echo "       rpc_core.h 等),待门槛 4 解耦后纳入 mock 编译。"
echo "(规范定义见 docs/felix/9.Host通用化实施总路线图.md §三个口径)"
echo ""

# ── Build test executable (ASAN + UBSAN) ──
echo "=== Building test_runner (ASAN + UBSAN) ==="
gcc -o build/test_runner \
    ${INC_FLAGS} \
    -DH_BUILD_TESTS \
    -fsanitize=address,undefined \
    -g -O0 \
    -Wall -Wextra -Werror \
    ${CORE_SRCS} \
    ${PORT_SRCS} \
    ${TEST_SRCS} \
    -lpthread

echo "=== Running tests (ASAN + UBSAN) ==="
./build/test_runner -v
ASAN_EXIT=$?

# ── Build and run with ThreadSanitizer ──
echo ""
echo "=== Building test_runner_tsan (TSAN) ==="
gcc -o build/test_runner_tsan \
    ${INC_FLAGS} \
    -DH_BUILD_TESTS \
    -fsanitize=thread \
    -g -O1 \
    -Wall -Wextra -Werror \
    ${CORE_SRCS} \
    ${PORT_SRCS} \
    ${TEST_SRCS} \
    -lpthread

echo "=== Running tests (TSAN) ==="
TSAN_OPTIONS="second_deadlock_stack=1" ./build/test_runner_tsan -v
TSAN_EXIT=$?

# ── Report ──
echo ""
if [ "$ASAN_EXIT" = "0" ] && [ "$TSAN_EXIT" = "0" ]; then
    echo "ALL TESTS PASSED (ASAN + TSAN clean)"
    exit 0
else
    echo "TEST FAILURES DETECTED"
    exit 1
fi
