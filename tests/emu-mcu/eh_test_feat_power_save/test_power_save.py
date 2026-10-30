# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""
Test: Host power save — CP light-sleep + wake-on-packet.

CP:   power_save/network_split/host_deep_sleep_cp_light_sleep (ESP32-C6)
Host: power_save/network_split/host_deep_sleep_cp_light_sleep (ESP32-P4)

Verifies the whole wake surface:
  - WiFi connect via CLI (routing up)
  - host_power_save -> host deep-sleep, CP light-sleep
  - one datagram on a host-reserved port wakes the host via the CP, and that
    same datagram reaches the host app
  - link comes back cleanly: GOT_IP, then ping to the AP

Authored identically to the HW suite: same infra.expect_helper, same
dut[0]/dut[1] handles. Only the bench provider (conftest) differs — here it
is the C6+P4 esp-emu pair instead of serial-attached boards.
"""

import sys
import os
import re
import time

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS
from infra import ps_wake

PS_FAIL = FATAL_PATTERNS + [
    'bring-up timed out',
    'Firmware abort',
    'Instruction access fault',
    'CORRUPT HEAP',
]


@pytest.mark.power_save
@pytest.mark.parametrize('rep', [1, 2])  # -n 2 runs two independent benches in parallel
class TestHostPowerSave:

    def test_cp_light_sleep_wake_on_packet(self, dut, net, rep):
        host, cp = dut[0], dut[1]

        # ── Stage 1: bring-up + WiFi connect ──
        r = eh_test_expect(host, r'host>', timeout=40)
        assert r.ok, f'CLI ready: {r.matched}'

        host.write('sta myssid mypassword')
        r = eh_test_expect(host, r'IP_EVENT_STA_GOT_IP', fail=PS_FAIL, timeout=60)
        assert r.ok, f'GOT_IP: {r.matched}'

        # ── Stage 2: host deep-sleep -> CP light-sleep ──
        host.write('host_power_save')
        r = eh_test_expect(cp, r'Light sleep ENABLED', fail=PS_FAIL, timeout=45)
        assert r.ok, f'CP light-sleep: {r.matched}'

        # ── Stage 3: one datagram wakes the host via the CP ──
        mark = len(getattr(host, '_buf', ''))
        net.wake_udp(ps_wake.payload(rep))

        r = eh_test_expect(host, r'Host woke up from power save', fail=PS_FAIL, timeout=45)
        assert r.ok, f'Host wake: {r.matched}'
        r = eh_test_expect(host, r'slave chip id', fail=PS_FAIL, timeout=30)
        assert r.ok, f'Transport re-init: {r.matched}'
        r = eh_test_expect(
            cp, r'host  woke up|Host power save off - device fully ready',
            fail=PS_FAIL, timeout=30)
        assert r.ok, f'CP wake ack: {r.matched}'

        # ── Stage 4: the waking datagram reached the app; the link works ──
        end = time.time() + 30
        seg = ''
        while time.time() < end:
            seg = getattr(host, '_buf', '')[mark:]
            if 'IP_EVENT_STA_GOT_IP' in seg and re.search(ps_wake.rx_line(rep), seg):
                break
            time.sleep(0.2)
        assert 'IP_EVENT_STA_GOT_IP' in seg, 'no GOT_IP after wake'
        assert re.search(ps_wake.rx_line(rep), seg), 'the packet that woke the host never reached it'
        r = eh_test_expect(host, r'Returned from app_main', fail=PS_FAIL, timeout=45)
        assert r.ok, f'console ready after wake: {r.matched}'     # a command before it is lost
        host.write('ping 192.168.4.1 -c 2')
        r = eh_test_expect(host, r'packets transmitted, [1-9]\d* received', fail=PS_FAIL, timeout=20)
        assert r.ok, f'ping after wake: {r.matched}'
        # (the _bench teardown runs settle_check — post-success crash catch)

# NOTE: the wake-flood regression (used to hit the START..CONNECTED RX race
# ~1/6) lives in test_power_save_flood.py; the admit-on-CONNECTED gate closes it
# (30/30 clean) — see decision-journal S7.
