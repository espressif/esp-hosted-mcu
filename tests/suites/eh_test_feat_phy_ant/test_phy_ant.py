# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""PHY antenna selection (esp_phy_set_ant / esp_phy_set_ant_gpio) over hosted.

Drives the api_exerciser `phy_ant_*` commands against the result contract
`EH rc=<int> cmd=<name> [k=v ...]`. Substrate-agnostic: the emu cannot route
PHY signals to a pad, so the pad-level check runs only where the bench reports
CAP_ANT_SEL_PAD.

Command arguments:
  phy_ant_set_gpio <g0> <g1> <g2> <g3>      '-' = slot unused
  phy_ant_set <rx> <def> <tx> <ant0> <ant1> rx/tx: 0=ANT0 1=ANT1 2=AUTO
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS
from targets.base import CAP_ANT_SEL_PAD, CAP_HOST_EXT_CAPS_LOG

FAIL = FATAL_PATTERNS + ['bring-up timed out']
EX = 'system/api_exerciser'
INVALID_ARG = 0x102
NOT_SUPPORTED = 0x106
CP_FEAT_OFF_OVL = ['CONFIG_ESP_HOSTED_CP_FEAT_PHY_ANT=n']
NO_PADS = (99, 128)   # 128 would truncate to pad 0 in the 7-bit field
UNUSED = ['-'] * 4
REFUSED = r'-?[1-9]\d*'

# Distinct value in every field, so a field the RPC drops or swaps shows up.
CONFIGS = [
    (0, 0, 0, 1, 2),     # rx ANT0, tx ANT0
    (1, 1, 1, 5, 10),    # rx ANT1, tx ANT1
    (2, 1, 2, 15, 0),    # rx AUTO, tx AUTO
    (2, 0, 1, 3, 12),    # rx AUTO, tx fixed
]
BASELINE = CONFIGS[0]

INVALID_CONFIGS = [
    ('rx mode MAX',       (3, 0, 0, 1, 2)),
    ('tx mode MAX',       (0, 0, 3, 1, 2)),
    ('rx default MAX',    (2, 2, 2, 1, 2)),
    ('tx AUTO, rx fixed', (0, 0, 2, 1, 2)),
    ('ant0 over 4 bits',  (0, 0, 0, 16, 0)),   # would truncate to 0
    ('ant1 over 4 bits',  (0, 0, 0, 0, 16)),
]


def _ready(bench, t, cap=False, **kw):
    b = bench(EX, 'mcu_host', t, timeout='120s', **kw)
    if cap and CAP_HOST_EXT_CAPS_LOG in b['caps']:
        # The coprocessor advertises EH_EXT_CAP_PHY_ANT in its init event.
        r = eh_test_expect(b['host'], r'\* PHY antenna selection', fail=FAIL, timeout=60)
        assert r.ok, f'[{t}] cap bit not advertised: {r.matched}'
    r = eh_test_expect(b['host'], r'EH api_exerciser ready', fail=FAIL, timeout=60)
    assert r.ok, f'[{t}] ready: {r.matched}'
    return b['host'], b['caps']


def _rc(host, t, cmd, rc_pat, extra=''):
    host.write(cmd)
    pat = rf'EH rc={rc_pat} cmd={cmd.split()[0]}\b' + ((' ' + extra) if extra else '')
    r = eh_test_expect(host, pat, fail=FAIL, timeout=20)
    assert r.ok, f'[{t}] {cmd}: {r.matched}'


def _ok(host, t, cmd, extra=''):
    _rc(host, t, cmd, '0', extra)


def _args(vals):
    return ' '.join(str(v) for v in vals)


def _set_cfg(host, t, c):
    _ok(host, t, f'phy_ant_set {_args(c)}')


def _cfg_is(host, t, c):
    _ok(host, t, 'phy_ant_get', r'rx={} def={} tx={} ant0={} ant1={}(?=\s|$)'.format(*c))


def _set_gpio(host, t, slots, rc_pat='0'):
    _rc(host, t, f'phy_ant_set_gpio {_args(slots)}', rc_pat)


def _gpio_is(host, t, slots):
    _ok(host, t, 'phy_ant_get_gpio', 'gpio=' + ','.join(str(s) for s in slots) + r'(?=\s|$)')


def _link_alive(host, t):
    _ok(host, t, 'sys_fw_version', r'ver=\d+\.\d+\.\d+')


def _ant_pin(bench_gpio):
    return bench_gpio.get('ant_pin', bench_gpio.get('pin'))


def _bus_pins(bench_gpio, transport):
    reserved = bench_gpio.get('reserved')
    pins = list(reserved) if isinstance(reserved, list) else (
        [reserved] if reserved is not None else [])
    bus_pin = bench_gpio.get('bus_pins', {}).get(transport)
    if bus_pin is not None and bus_pin not in pins:
        pins.append(bus_pin)
    return pins


@pytest.mark.phy_ant
@pytest.mark.parametrize('transport', [
    pytest.param('sdio', marks=pytest.mark.sanity),
    'sdio_1', 'uart', 'spi_hd', 'spi_fd',
])
@pytest.mark.second_chance
def test_phy_ant_api(bench, transport, bench_gpio):
    """Wi-Fi is started at exerciser boot, so every set takes the immediate-apply path."""
    host, _ = _ready(bench, transport, cap=True)
    t = transport
    bus = _bus_pins(bench_gpio, transport)
    pin = _ant_pin(bench_gpio)

    # ── antenna config: exact round-trip of every field ─────────────────
    for c in CONFIGS + [BASELINE]:
        _set_cfg(host, t, c)
        _cfg_is(host, t, c)

    # ── antenna config: IDF rejects, nothing is applied ─────────────────
    for name, c in INVALID_CONFIGS:
        host.write(f'phy_ant_set {_args(c)}')
        r = eh_test_expect(host, rf'EH rc={INVALID_ARG} cmd=phy_ant_set\b',
                           fail=FAIL, timeout=20)
        assert r.ok, f'[{t}] {name} not rejected: {r.matched}'
        _cfg_is(host, t, BASELINE)

    # ── antenna GPIOs: transport-owned pins refused in any slot ─────────
    _gpio_is(host, t, UNUSED)
    for p in bus:
        for slot in range(4):
            slots = list(UNUSED)
            slots[slot] = p
            _set_gpio(host, t, slots, REFUSED)
            _gpio_is(host, t, UNUSED)
        _link_alive(host, t)

    for g in NO_PADS:
        _set_gpio(host, t, [g, '-', '-', '-'], str(INVALID_ARG))
        _gpio_is(host, t, UNUSED)

    # ── antenna GPIOs: slot index carried exactly ───────────────────────
    if pin is not None:
        for slot in range(4):
            slots = list(UNUSED)
            slots[slot] = pin
            _set_gpio(host, t, slots)
            _gpio_is(host, t, slots)
        _set_gpio(host, t, UNUSED)
        _gpio_is(host, t, UNUSED)
        # One bad slot rejects the whole set: the good pin is not applied either.
        if bus:
            _set_gpio(host, t, [pin, bus[0], '-', '-'], REFUSED)
            _gpio_is(host, t, UNUSED)

    # ── usage guard ─────────────────────────────────────────────────────
    _rc(host, t, 'phy_ant_set 0 0', '-1', 'err=USAGE')
    _rc(host, t, 'phy_ant_set_gpio', '-1', 'err=USAGE')
    _rc(host, t, 'phy_ant_set_gpio 300 - - -', '-1', 'err=USAGE')
    _link_alive(host, t)


@pytest.mark.phy_ant
@pytest.mark.parametrize('transport', ['sdio', 'uart'])
@pytest.mark.second_chance
def test_phy_ant_gpio_idf_check(bench, transport, bench_gpio):
    """IDF esp_phy_set_ant_gpio() passes the GPIO number to esp_gpio_is_reserved(),
    which takes a mask. GPIO 14 (0b1110) then fails on a reserved GPIO 2, and a
    re-applied config fails on its own pins, and a flash pin can be accepted.
    The CP checks with a mask itself, so a flash pin is always refused."""
    pins = bench_gpio.get('ant_pins')
    if not pins or len(pins) < 2:
        pytest.skip('bench declares fewer than two free antenna pins')
    host, _ = _ready(bench, transport)
    t = transport
    a, b = pins[0], pins[1]

    _set_gpio(host, t, [b, '-', '-', '-'])
    _gpio_is(host, t, [b, '-', '-', '-'])
    for _ in range(2):
        _set_gpio(host, t, [a, b, '-', '-'])
        _gpio_is(host, t, [a, b, '-', '-'])
    _set_gpio(host, t, UNUSED)
    _gpio_is(host, t, UNUSED)
    # As in IDF, a dropped pin stays reserved until the app resets it.
    _set_gpio(host, t, [a, '-', '-', '-'], REFUSED)
    _gpio_is(host, t, UNUSED)
    for pin in (a, b):
        _ok(host, t, f'gpio_reset_pin {pin}')
    _set_gpio(host, t, [a, '-', '-', '-'])
    _gpio_is(host, t, [a, '-', '-', '-'])
    _set_gpio(host, t, UNUSED)
    _ok(host, t, f'gpio_reset_pin {a}')
    flash = bench_gpio.get('flash_pin')
    if flash is not None:
        _set_gpio(host, t, [flash, '-', '-', '-'], REFUSED)
        _gpio_is(host, t, UNUSED)
    _link_alive(host, t)


@pytest.mark.phy_ant
@pytest.mark.parametrize('transport', ['sdio', 'uart'])
@pytest.mark.second_chance
def test_phy_ant_without_wifi(bench, transport, bench_gpio):
    """Config set with Wi-Fi deinitialised survives Wi-Fi init + start. When no
    other radio holds the PHY, this is IDF's deferred-apply path."""
    host, _ = _ready(bench, transport)
    t = transport
    pin = _ant_pin(bench_gpio)
    slots = [pin, '-', '-', '-'] if pin is not None else UNUSED
    cfg = CONFIGS[1]

    _ok(host, t, 'wifi_stop')
    _ok(host, t, 'wifi_deinit')
    _set_gpio(host, t, slots)
    _set_cfg(host, t, cfg)
    _cfg_is(host, t, cfg)
    _gpio_is(host, t, slots)

    _ok(host, t, 'wifi_init')
    _ok(host, t, 'wifi_start')
    _cfg_is(host, t, cfg)
    _gpio_is(host, t, slots)
    _link_alive(host, t)


@pytest.mark.phy_ant
@pytest.mark.parametrize('transport', ['sdio', 'uart'])
def test_phy_ant_pad_level(bench, transport, bench_gpio):
    """ANT_SEL0 on the pin: ANT0 pattern 0b0001 drives 1, ANT1 pattern 0b0000 drives 0.
    The PHY drives ANT_SEL only while the radio is active, so each level is read
    during a scan. Input-enable comes after set_gpio, which configures the pad as
    output only."""
    pin = _ant_pin(bench_gpio)
    if pin is None:
        pytest.skip('bench declares no free CP pin')
    host, caps = _ready(bench, transport)
    if CAP_ANT_SEL_PAD not in caps:
        pytest.skip('bench does not route PHY ANT_SEL to the pad')
    t = transport

    _set_gpio(host, t, [pin, '-', '-', '-'])
    _ok(host, t, 'gpio_init')
    _ok(host, t, f'gpio_input_enable {pin}')
    for mode, level in ((0, 1), (1, 0), (0, 1)):
        _set_cfg(host, t, (mode, mode, mode, 1, 0))
        _ok(host, t, 'wifi_scan_start 0 - 0 0 active')
        _ok(host, t, f'gpio_get_level {pin}', f'level={level}')
        _ok(host, t, 'wifi_scan_stop')
    _set_gpio(host, t, UNUSED)
    _ok(host, t, f'gpio_reset_pin {pin}')


@pytest.mark.phy_ant
@pytest.mark.parametrize('transport', ['sdio', 'uart'])
def test_phy_ant_cp_without_feature(bench, transport):
    """Host built with the feature, CP without it: clean NOT_SUPPORTED, no hang."""
    host, _ = _ready(bench, transport, cp_overlay=CP_FEAT_OFF_OVL)
    t = transport
    for cmd in ('phy_ant_get', 'phy_ant_get_gpio',
                f'phy_ant_set {_args(BASELINE)}', f'phy_ant_set_gpio {_args(UNUSED)}'):
        _rc(host, t, cmd, str(NOT_SUPPORTED))
    _link_alive(host, t)
