#!/bin/bash
# scripts/hw_smoke_test.sh
# Tier 3 — Hardware-in-the-loop layered smoke test.
# Run on the host MCU (ESP32-P4) with slave (ESP32-C6) connected via SDIO.
#
# Each step validates one abstraction layer before moving to the next.
# Do NOT skip steps — failures at lower layers make higher-layer results
# meaningless.

set -euo pipefail

# ═══════════════════════════════════════════════════════════
# BASELINE — MUST be filled in before first run.
# Record these on P4+C6 hardware before the refactor:
#   TCP: ___ Mbps  (recorded YYYY-MM-DD)
#   UDP: ___ Mbps  (recorded YYYY-MM-DD)
# ═══════════════════════════════════════════════════════════
BASELINE_TCP=___   # Mbps
BASELINE_UDP=___   # Mbps
[ "$BASELINE_TCP" = "___" ] && { echo "ERROR: Set BASELINE_TCP in this script before running"; exit 1; }
[ "$BASELINE_UDP" = "___" ] && { echo "ERROR: Set BASELINE_UDP in this script before running"; exit 1; }

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

step=0
pass()  { echo -e "${GREEN}[PASS]${NC} Step $step: $1"; }
fail()  { echo -e "${RED}[FAIL]${NC} Step $step: $1"; exit 1; }

echo "============================================"
echo " ESP-Hosted Hardware-in-the-Loop Smoke Test"
echo "============================================"
echo "Hardware: ESP32-P4 (host) + ESP32-C6 (slave)"
echo "Connection: SDIO"
echo ""

# Step 1: Slave standalone boot
step=1
echo "Step $step: Slave standalone boot"
echo "  → Power on slave and check serial output"
echo "  → Expected: 'esp_hosted slave ready' or similar boot message"
echo ""
read -p "  Did slave boot successfully? (y/n) " yn
[ "$yn" = "y" ] && pass "Slave boot OK" || fail "Slave boot failed"

# Step 2: Physical link test
step=2
echo "Step $step: SPI/SDIO/UART physical link"
echo "  → Host sends test pattern (0xAA/0x55), slave loops back"
echo "  → Verify: data matches, no dropped/corrupted bytes"
echo ""
echo "  Run on host: hw_test_link"
read -p "  Did link test pass? (y/n) " yn
[ "$yn" = "y" ] && pass "Physical link OK" || fail "Link test failed"

# Step 3: TLV handshake
step=3
echo "Step $step: TLV handshake protocol"
echo "  → After slave power-on, host parses h_priv_event_t frames"
echo "  → Verify host correctly extracts: chip ID, capability bits, FW version"
echo ""
echo "  Run on host: hw_test_handshake"
read -p "  Did handshake complete correctly? (y/n) " yn
[ "$yn" = "y" ] && pass "TLV handshake OK" || fail "Handshake failed"

# Step 4: RPC request/response
step=4
echo "Step $step: RPC request/response"
echo "  → Host sends h_wifi_get_mode() (simple RPC, no Wi-Fi required)"
echo "  → Verify: request UID matches response UID, return code valid"
echo ""
echo "  Run on host: hw_test_rpc"
read -p "  Did RPC round-trip succeed? (y/n) " yn
[ "$yn" = "y" ] && pass "RPC round-trip OK" || fail "RPC failed"

# Step 5: Wi-Fi control
step=5
echo "Step $step: Wi-Fi control"
echo "  → h_wifi_set_mode(AP) → h_wifi_set_config → h_wifi_start"
echo "  → Verify: external STA can scan and connect to SoftAP"
echo "  → Verify: host receives H_EVENT_WIFI_AP_STACONNECTED"
echo ""
read -p "  Did SoftAP start and STA connect? (y/n) " yn
[ "$yn" = "y" ] && pass "Wi-Fi control OK" || fail "Wi-Fi control failed"

# Step 6: Data path
step=6
echo "Step $step: Data path (iperf)"
echo "  → Host runs iperf server: iperf -s"
echo "  → STA runs: iperf -c <host_ip> -t 30"
echo "  → Verify TCP throughput >= 95% of pre-refactor baseline"
echo ""
read -p "  Did iperf meet throughput threshold? (y/n) " yn
[ "$yn" = "y" ] && pass "Data path OK" || fail "Data path failed"

echo ""
echo "============================================"
echo -e " ${GREEN}ALL 6 STEPS PASSED${NC}"
echo " Hardware in-loop smoke test complete."
echo "============================================"
