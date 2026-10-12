# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Manual (auto-init-off) bring-up flow — system/mcu_transport_config.

The example turns BOTH auto-init switches off and drives every step itself:
configure the transport before init, esp_hosted_init + connect_to_slave, then
reconfigure it (deinit -> new set_config -> init), start the bus monitor
(heartbeat + recovery), then Wi-Fi STA and BLE advertising. Each stage emits an
"EH stage=..." marker; this test asserts them in stream order.

Transport config, per bring-up:
  - set_config rc=0 and get_config returns the value set;
  - the bus driver's "bus applied:" line reports what the IDF driver was actually
    configured with (SPI / SDIO clock, UART baud), within the divider's tolerance;
  - a set_config while the transport is up is refused (ALREADY_SET) and changes
    nothing; after deinit the new value is accepted and the bus comes up with it.
UART baud must match the CP, so it keeps the agreed rate across both bring-ups.
The host accepts exactly one CP INIT per bring-up: another one means a stale
pre-reset INIT was taken as the link.
"""
import os
import re
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

FAIL = FATAL_PATTERNS + ['bring-up timed out', 'hosted bring-up failed', 'hosted reconfigure failed',
                        'stage=monitor cp-reboot', 'stage=monitor transport-failure',
                        'stage=monitor heartbeat-loss', 'stage=recovery reinit']
INIT = r'chip=0x[0-9a-f]+ \(.*\) fw=0x'

# transport -> (bus name, example key, (value before, value after deinit), driver key, to-driver-unit)
BUS = {
    'sdio':   ('sdio',   'clk_khz', (20000, 40000),   'clk_khz', 1),
    'spi_fd': ('spi',    'clk_mhz', (20, 26),         'clk_khz', 1000),
    'spi_hd': ('spi_hd', 'clk_mhz', (20, 40),         'clk_khz', 1000),
    'uart':   ('uart',   'baud',    (921600, 921600), 'baud',    1),
}
TOLERANCE = 0.10   # clock dividers round the requested rate


def _number_after(dut, prefix, what):
    try:
        m = dut.expect(re.escape(prefix) + r'(\d+)', not_matching=FAIL, timeout=60)
    except Exception as e:  # timeout or a fail pattern
        raise AssertionError(f'{what}: "{prefix}<n>" not seen: {str(e)[:200]}') from e
    g = m.group(1)
    return int(g.decode() if isinstance(g, bytes) else g)


def _bring_up(host, bus, key, value, dkey, unit, label):
    r = eh_test_expect(host, rf'EH stage=transport bus={bus} set {key}={value} rc=0', fail=FAIL, timeout=90)
    assert r.ok, f'{label}: set_config {key}={value}: {r.matched}'
    r = eh_test_expect(host, rf'EH stage=transport get {key}={value}\b', fail=FAIL, timeout=20)
    assert r.ok, f'{label}: get_config read-back {key}={value}: {r.matched}'
    applied = _number_after(host, f'bus applied: {dkey}=', f'{label}: driver-applied {dkey}')
    want = value * unit
    assert abs(applied - want) <= want * TOLERANCE, \
        f'{label}: bus brought up with {dkey}={applied}, set {want}'
    r = eh_test_expect(host, INIT, fail=FAIL, timeout=30)
    assert r.ok, f'{label}: CP INIT: {r.matched}'
    return applied


@pytest.mark.system
@pytest.mark.parametrize('transport', list(BUS))
def test_manual_bringup_flow(bench, transport):
    bus, key, (v0, v1), dkey, unit = BUS[transport]
    b = bench('system/mcu_transport_config', 'mcu_host', transport, timeout='300s')
    host = b['host']
    one_init = FAIL + [' fw=0x']   # a second INIT inside one bring-up fails the stage

    # bring-up 1: config applied before init
    a0 = _bring_up(host, bus, key, v0, dkey, unit, 'bring-up 1')
    r = eh_test_expect(host, r'EH stage=hosted linked', fail=one_init, timeout=30)
    assert r.ok, f'bring-up 1 linked: {r.matched}'

    # negative: a change while the transport is up is refused and changes nothing
    r = eh_test_expect(host, r'EH stage=transport set-while-up refused=1 rc=\d+', fail=one_init, timeout=20)
    assert r.ok, f'set_config while up must be refused: {r.matched}'
    r = eh_test_expect(host, rf'EH stage=transport get {key}={v0}\b', fail=one_init, timeout=20)
    assert r.ok, f'config unchanged after the refused set: {r.matched}'

    # bring-up 2: deinit -> new config -> init
    a1 = _bring_up(host, bus, key, v1, dkey, unit, 'bring-up 2')
    if v1 != v0:
        assert a1 != a0, f'bus clock did not change across deinit/init ({a0} -> {a1})'
    r = eh_test_expect(host, r'EH stage=transport reconfigured rc=0', fail=one_init, timeout=30)
    assert r.ok, f'reconfigured: {r.matched}'

    r = eh_test_expect(host, r'EH stage=monitor started', fail=one_init, timeout=20)
    assert r.ok, f'monitor: {r.matched}'
    r = eh_test_expect(host, r'EH stage=wifi ip=', fail=one_init, timeout=60)
    assert r.ok, f'wifi ip: {r.matched}'
    r = eh_test_expect(host, r'EH stage=ble advertising rc=0', fail=one_init, timeout=30)
    assert r.ok, f'ble advertising: {r.matched}'
    r = eh_test_expect(host, r'EH manual bringup complete', fail=one_init, timeout=20)
    assert r.ok, f'complete: {r.matched}'
