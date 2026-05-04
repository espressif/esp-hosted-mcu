#!/bin/bash
# scripts/run_linux_mock_tests.sh
# CI Tier 2 — Build and run all unit tests on Linux mock port.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$ROOT_DIR"

mkdir -p build

CORE_SRCS="host/core/src/h_init.c host/core/src/h_rpc_core.c host/core/src/h_serial_if.c host/core/src/h_event.c"
PORT_SRCS="host/port/linux/src/h_osal.c host/port/linux/src/h_event.c host/port/linux/src/h_transport_mock.c"
TEST_SRCS="tests/test_runner.c tests/test_osal.c tests/test_event.c tests/test_transport.c tests/test_rpc_core.c tests/unity/unity.c"

INC_FLAGS="-I host/core/include/h_public -I host/core/include/h_internal -I host/port/linux -I host/port/include -I tests/unity"

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
