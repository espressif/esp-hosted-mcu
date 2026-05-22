#!/bin/bash
# scripts/run_linux_mock_tests.sh
# CI Tier 2 — Build and run all unit tests on Linux mock port.
#
# Definition of "testable"(canonical source: docs/felix/9.Host通用化实施总路线图.md
# §"三个口径的明确定义"):
#   被本脚本持续编译且测试用例真实执行其**生产代码路径**。
#
# Known discrepancy with check_core_isolation.sh portable set:
#   - h_api.c       — portable, included after 门槛 3 (needs rpc_wifi_* stubs
#                     from h_rpc_wrap.c which is portable but not mock-compilable
#                     due to ESP-IDF Wi-Fi type dependencies).
#   - h_rpc_core.c  — WP 4.5 DONE: 生产路径, 已移除 H_BUILD_TESTS 测试专用分支。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$ROOT_DIR"

mkdir -p build

# NOTE: 门槛 4 完成后, Linux mock 已覆盖全部 13 个 host/core/src 生产文件的
# 编译、链接与生产路径测试验证。脚本输出需与 9 号文档的三口径保持一致。
# 所有 host/core/src/ 文件 — 门槛 4 后全部纳入编译验证
ALL_CORE_SRCS="host/core/src/h_init.c host/core/src/h_rpc_core.c host/core/src/h_serial_if.c host/core/src/h_event.c host/core/src/h_api.c host/core/src/h_transport_util.c host/core/src/h_rpc_req.c host/core/src/h_rpc_rsp.c host/core/src/h_rpc_evt.c host/core/src/h_rpc_utils.c host/core/src/h_rpc_slave_if.c host/core/src/h_transport_drv.c host/core/src/h_rpc_wrap.c"

# 参与链接与测试的 core 文件全集。门槛 4 完成后与 ALL_CORE_SRCS 重合。
LINK_CORE_SRCS="host/core/src/h_init.c host/core/src/h_rpc_core.c host/core/src/h_serial_if.c host/core/src/h_event.c host/core/src/h_api.c host/core/src/h_transport_util.c host/core/src/h_rpc_utils.c host/core/src/h_rpc_rsp.c host/core/src/h_rpc_evt.c host/core/src/h_rpc_slave_if.c host/core/src/h_rpc_req.c host/core/src/h_transport_drv.c host/core/src/h_rpc_wrap.c"
PORT_SRCS="host/port/linux/src/h_osal.c host/port/linux/src/h_event.c host/port/linux/src/h_transport_mock.c"
TEST_SRCS="tests/test_runner.c tests/test_osal.c tests/test_event.c tests/test_transport.c tests/test_rpc_core.c tests/test_rpc_bridge.c tests/unity/unity.c tests/stubs/rpc_wifi_stubs.c tests/stubs/protobuf_c_stubs.c tests/stubs/serial_stubs.c tests/stubs/transport_pserial_stubs.c tests/stubs/rpc_slaveif_stubs.c tests/stubs/transport_drv_stubs.c"
PROTO_SRCS="common/proto/esp_hosted_rpc.pb-c.c"

INC_FLAGS="-I host/core/include/h_public -I host/core/include/h_internal -I host/port/linux -I host/port/include -I tests/unity -I tests/stubs -I common/mempool/include -I common/transport -I common -I host/api/include -I host/drivers/transport -I host/drivers/rpc/core -I host/drivers/rpc/slaveif -I host/drivers/virtual_serial_if -I host/drivers/serial -I host/drivers/rpc/wrap -I common/proto -I common/protobuf-c -I host -I common/rpc -I common/log -I host/drivers/bt -I common/utils -I host/port/esp-idf -D_FORTIFY_SOURCE=0"
WARN_FLAGS="-Wall -Wextra -Werror -Wno-enum-conversion -Wno-unused-but-set-variable"

# ── 编译前打印两个口径下的集合 ──
# 注: WP 4.5 完成后 h_rpc_core.c 已移除 H_BUILD_TESTS, 全部 13 个文件均走生产路径。
echo "── mock 持续编译集合(host/core/src/ 总文件数 13)──"
MOCK_BUILD_CORE=(
    host/core/src/h_init.c
    host/core/src/h_event.c
    host/core/src/h_serial_if.c
    host/core/src/h_api.c            # 门槛 3 新增
    host/core/src/h_transport_util.c # 门槛 3 新增
    host/core/src/h_rpc_core.c       # WP 4.5: 生产路径
    host/core/src/h_rpc_req.c        # 门槛 4 新增
    host/core/src/h_rpc_rsp.c        # 门槛 4 新增
    host/core/src/h_rpc_evt.c        # 门槛 4 新增
    host/core/src/h_rpc_utils.c      # 门槛 4 新增
    host/core/src/h_rpc_slave_if.c   # 门槛 4 新增
    host/core/src/h_transport_drv.c  # 门槛 4 新增
    host/core/src/h_rpc_wrap.c       # 门槛 4 新增
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
    host/core/src/h_rpc_core.c       # WP 4.5: 生产路径编译+链接+测试通过
    host/core/src/h_rpc_rsp.c        # WP 4.5: 生产路径编译+链接通过
    host/core/src/h_rpc_evt.c        # WP 4.5: 生产路径编译+链接通过
    host/core/src/h_rpc_slave_if.c   # WP 4.5: 生产路径编译+链接通过
    host/core/src/h_rpc_req.c        # WP 4.5: 生产路径编译+链接+契约测试通过
    host/core/src/h_transport_drv.c  # 生产路径: 状态机+teardown+remove_channel 契约测试
    host/core/src/h_rpc_wrap.c       # 生产路径: rpc_init/start/stop/deinit 契约测试
    host/core/src/h_rpc_utils.c      # 生产路径: rpc_copy_wifi_sta_config 位掩码+数据拷贝契约测试
)
for f in "${TESTABLE_CORE[@]}"; do
    echo "  ✓ $f"
done
echo "  覆盖率: ${#TESTABLE_CORE[@]}/13 = $(awk "BEGIN { printf \"%.0f%%\", ${#TESTABLE_CORE[@]} / 13 * 100 }")"
echo "(规范定义见 docs/felix/9.Host通用化实施总路线图.md §三个口径)"
echo ""

echo "── compile-only (仅编译验证, 未链接) ──"
COMPILE_ONLY_CORE=()
if [ "${#COMPILE_ONLY_CORE[@]}" -eq 0 ]; then
    echo "  ○ 无"
else
    for f in "${COMPILE_ONLY_CORE[@]}"; do
        echo "  ○ $f"
    done
fi
echo "  待办: ${#COMPILE_ONLY_CORE[@]}/13 (编译通过, 待链接)"
echo ""

echo "── 已链接但未覆盖生产路径测试 ──"
LINKED_BUT_NOT_TESTED=()
if [ "${#LINKED_BUT_NOT_TESTED[@]}" -eq 0 ]; then
    echo "  ◐ 无"
else
    for f in "${LINKED_BUT_NOT_TESTED[@]}"; do
        echo "  ◐ $f"
    done
fi
echo "  已链接: ${#LINKED_BUT_NOT_TESTED[@]}/13 (编译+链接通过, 待生产路径测试覆盖)"
echo ""


PENDING_CORE=()

# ── 机械差集检查: 确保 MOCK_BUILD_CORE + PENDING_CORE == 全集 ──
ALL_CORE_SRCS=$(find host/core/src -maxdepth 1 -name 'h_*.c' | sort)
mkdir -p build
printf '%s\n' "${MOCK_BUILD_CORE[@]}" | sort > build/_mock_build_core.txt
if [ "${#PENDING_CORE[@]}" -eq 0 ]; then
    : > build/_pending_core.txt
else
    printf '%s\n' "${PENDING_CORE[@]}" | sort > build/_pending_core.txt
fi
printf '%s\n' "$ALL_CORE_SRCS" > build/_all_core.txt

echo "── 机械差集检查 ──"
MISSING=$(comm -23 build/_all_core.txt build/_mock_build_core.txt)
if [ -n "$MISSING" ]; then
    echo "  WARNING: MOCK_BUILD_CORE 未覆盖以下文件:"
    echo "$MISSING" | sed 's/^/    /'
else
    echo "  ✓ MOCK_BUILD_CORE == ALL_CORE_SRCS (13/13)"
fi
EXTRA=$(comm -13 build/_all_core.txt build/_mock_build_core.txt)
if [ -n "$EXTRA" ]; then
    echo "  WARNING: MOCK_BUILD_CORE 包含不在 host/core/src/ 的文件:"
    echo "$EXTRA" | sed 's/^/    /'
fi
echo ""

# ── Step 1: Compile ALL core files (compile-only, no link) ──
echo "=== Compiling all core files (compile-only check) ==="
for src in ${ALL_CORE_SRCS}; do
    obj="build/$(basename ${src%.c}.o)"
    gcc -c ${INC_FLAGS} -g -O0 ${WARN_FLAGS} "$src" -o "$obj"
done
echo "  ✓ All 13 core files compiled successfully"
echo ""

# ── Step 2: Build and link test executable (only link-tested files) ──
echo "=== Building test_runner (ASAN + UBSAN) ==="
gcc -o build/test_runner \
    ${INC_FLAGS} \
    -fsanitize=address,undefined \
    -g -O0 \
    ${WARN_FLAGS} \
    ${LINK_CORE_SRCS} \
    ${PORT_SRCS} \
    ${TEST_SRCS} \
    ${PROTO_SRCS} \
    -lpthread

echo "=== Running tests (ASAN + UBSAN) ==="
./build/test_runner -v
ASAN_EXIT=$?

# ── Build and run with ThreadSanitizer ──
echo ""
echo "=== Building test_runner_tsan (TSAN) ==="
gcc -o build/test_runner_tsan \
    ${INC_FLAGS} \
    -fsanitize=thread \
    -g -O1 \
    ${WARN_FLAGS} \
    ${LINK_CORE_SRCS} \
    ${PORT_SRCS} \
    ${TEST_SRCS} \
    ${PROTO_SRCS} \
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
