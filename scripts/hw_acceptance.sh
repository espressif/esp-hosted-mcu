#!/bin/bash
# scripts/hw_acceptance.sh
# Tier 3 acceptance test — run before merging PRs.
# Tests: build, SoftAP start, STA connect, TCP/UDP throughput, stability, restart.
#
# Baseline (recorded on ESP32-P4 + ESP32-C6 SDIO, pre-refactor):
#   TCP: ___ Mbps
#   UDP: ___ Mbps
# These values are the >=95% threshold for acceptance.

set -euo pipefail

BASELINE_TCP=___   # Mbps — FILL IN from pre-refactor measurement on P4+C6
BASELINE_UDP=___   # Mbps — FILL IN from pre-refactor measurement on P4+C6
[ "$BASELINE_TCP" = "___" ] && { echo "ERROR: Set BASELINE_TCP in this script before running"; exit 1; }
[ "$BASELINE_UDP" = "___" ] && { echo "ERROR: Set BASELINE_UDP in this script before running"; exit 1; }
THRESHOLD=0.95

echo "=== ESP-Hosted Hardware Acceptance Test ==="
echo "Device: ESP32-P4-Function-EV-Board (host) + ESP32-C6 (slave)"
echo "Connection: SDIO"
echo ""

# 1. Build
echo "--- 1. Build ---"
echo "Run: cd examples/<example> && idf.py set-target esp32p4 && idf.py build"
echo "Expected: zero errors, zero warnings"
echo ""

# 2. SoftAP Start
echo "--- 2. SoftAP Start ---"
echo "Run: h_wifi_init -> h_wifi_set_mode(AP) -> h_wifi_set_config -> h_wifi_start"
echo "Expected: STA device can see AP hotspot in scan results"
echo ""

# 3. STA Connect Event
echo "--- 3. STA Connect Event ---"
echo "Action: Connect phone/PC to host's SoftAP"
echo "Expected: H_EVENT_WIFI_AP_STACONNECTED callback fires on host"
echo ""

# 4. TCP Throughput
echo "--- 4. TCP Throughput (threshold: >= $BASELINE_TCP x $THRESHOLD = $(echo "$BASELINE_TCP * $THRESHOLD" | bc) Mbps) ---"
echo "On host: iperf -s"
echo "On STA:  iperf -c <host_ip> -t 30"
echo "Result: ___ Mbps  [PASS/FAIL]"
echo ""

# 5. UDP Throughput
echo "--- 5. UDP Throughput (threshold: >= $BASELINE_UDP x $THRESHOLD = $(echo "$BASELINE_UDP * $THRESHOLD" | bc) Mbps) ---"
echo "On host: iperf -s -u"
echo "On STA:  iperf -c <host_ip> -u -b 20M -t 30"
echo "Result: ___ Mbps  [PASS/FAIL]"
echo ""

# 6. Stability
echo "--- 6. Stability (10min continuous iperf) ---"
echo "Run: iperf -c <host_ip> -t 600"
echo "Expected: zero crashes, zero disconnects, throughput stable within +/-5%"
echo "Result: [PASS/FAIL]"
echo ""

# 7. Restart Recovery
echo "--- 7. Restart Recovery ---"
echo "Action: Host restart -> h_hosted_init() again"
echo "Expected: AP visible again, iperf throughput restored to baseline"
echo "Result: [PASS/FAIL]"
echo ""

echo "=== Acceptance Checklist ==="
echo "  [ ] 1. Build: zero errors, zero warnings"
echo "  [ ] 2. SoftAP start: visible to STA"
echo "  [ ] 3. STA connect: H_EVENT_WIFI_AP_STACONNECTED received"
echo "  [ ] 4. TCP throughput: >= $(echo "$BASELINE_TCP * $THRESHOLD" | bc) Mbps"
echo "  [ ] 5. UDP throughput: >= $(echo "$BASELINE_UDP * $THRESHOLD" | bc) Mbps"
echo "  [ ] 6. Stability: 10min continuous, zero crashes"
echo "  [ ] 7. Restart recovery: re-init functional"
echo ""
echo "All items must be checked before merging PR."
