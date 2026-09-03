"""Wi-Fi string and address fields at their limits.

Every SSID and password field crosses the RPC as length+bytes, then lands in a
fixed IDF array. Some of those arrays carry a separate length (wifi_ap_config_t,
both STA events) and some are NUL-terminated (wifi_ap_record_t.ssid[33]).
Mixing the two conventions truncates or over-reads only at the maximum length,
which is why short names never caught it.

Emu-only: the maximum-length SSID needs the modeled SoftAP renamed, which the
emulator supports via --wifi-ssid (EH_EMU_WIFI_SSID)."""

import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

FAIL = FATAL_PATTERNS + ['bring-up timed out']
EX = 'system/api_exerciser'

SSID_MAX = 32                       # wifi_ap_config_t / wifi_sta_config_t ssid[32]
PASSWORD_MAX = 64                   # .password[64]
SSID_AT_MAX = 'S' * SSID_MAX
SSID_OVER_MAX = 'T' * (SSID_MAX + 1)


def _ready(emu_bench, transport):
    b = emu_bench(EX, 'mcu_host', transport, timeout='120s')
    r = eh_test_expect(b['host'], r'EH api_exerciser ready', fail=FAIL, timeout=60)
    assert r.ok, f'[{transport}] ready: {r.matched}'
    return b['host']


def _ok(host, t, cmd, extra=''):
    host.write(cmd)
    pat = r'EH rc=0 cmd=' + cmd.split()[0] + ((' ' + extra) if extra else '')
    r = eh_test_expect(host, pat, fail=FAIL, timeout=20)
    assert r.ok, f'[{t}] {cmd}: {r.matched}'


@pytest.mark.system
@pytest.mark.sanity
@pytest.mark.parametrize('transport', ['sdio'])
def test_sta_config_ssid_at_max(emu_bench, transport):
    """POSITIVE: a 32-byte SSID survives set_config -> get_config whole.

    ssid[32] has no room for a terminator, so a length computed with strlen()
    runs off the array and the round trip loses or corrupts the last bytes."""
    host = _ready(emu_bench, transport)
    t = transport
    _ok(host, t, 'wifi_set_mode 1')
    for c in ('wifi_cfg_reset', f'wifi_cfg_set sta_ssid {SSID_AT_MAX}',
              'wifi_set_config sta'):
        _ok(host, t, c)
    _ok(host, t, 'wifi_get_config sta', f'ssid={SSID_AT_MAX}')


@pytest.mark.system
@pytest.mark.parametrize('transport', ['sdio'])
def test_sta_config_ssid_over_max(emu_bench, transport):
    """NEGATIVE: 33 bytes must bound to 32, not corrupt or crash.

    The command must still answer, and the readback must be exactly the first
    32 bytes -- never 33, and never a short/garbled value."""
    host = _ready(emu_bench, transport)
    t = transport
    _ok(host, t, 'wifi_set_mode 1')
    _ok(host, t, 'wifi_cfg_reset')
    host.write(f'wifi_cfg_set sta_ssid {SSID_OVER_MAX}')
    r = eh_test_expect(host, r'EH rc=-?\d+ cmd=wifi_cfg_set', fail=FAIL, timeout=20)
    assert r.ok, f'[{t}] oversized ssid was not answered: {r.matched}'
    _ok(host, t, 'wifi_set_config sta')
    _ok(host, t, 'wifi_get_config sta', f'ssid={SSID_OVER_MAX[:SSID_MAX]}(\\s|$)')


@pytest.mark.system
@pytest.mark.parametrize('transport', ['sdio'])
def test_sta_config_password_at_max(emu_bench, transport):
    """POSITIVE: a 64-byte password survives the round trip whole.

    password[64] is also unterminated at maximum length. The readback reports
    the length, not the secret, so a truncation to 63 fails the assertion."""
    host = _ready(emu_bench, transport)
    t = transport
    _ok(host, t, 'wifi_set_mode 1')
    for c in ('wifi_cfg_reset', 'wifi_cfg_set sta_ssid pwmax',
              f'wifi_cfg_set sta_password {"P" * PASSWORD_MAX}',
              'wifi_set_config sta'):
        _ok(host, t, c)
    _ok(host, t, 'wifi_get_config sta', f'ssid=pwmax channel=\\d+ pwlen={PASSWORD_MAX}')


@pytest.mark.system
@pytest.mark.sanity
@pytest.mark.parametrize('transport', ['sdio'])
def test_sta_connected_event_ssid_at_max(emu_bench, transport, monkeypatch):
    """POSITIVE: the connected event reports all 32 bytes of the SSID.

    wifi_event_sta_connected_t is ssid[32] + ssid_len. A handler that reserves
    a byte for a terminator reports 30 of the 32 characters."""
    monkeypatch.setenv('EH_EMU_WIFI_SSID', SSID_AT_MAX)
    host = _ready(emu_bench, transport)
    t = transport
    _ok(host, t, 'wifi_set_mode 1')
    for c in ('wifi_cfg_reset', f'wifi_cfg_set sta_ssid {SSID_AT_MAX}',
              'wifi_cfg_set sta_password mypassword', 'wifi_set_config sta'):
        _ok(host, t, c)
    _ok(host, t, 'wifi_connect')
    r = eh_test_expect(host, r'EH event wifi_sta_connected', fail=FAIL, timeout=60)
    assert r.ok, f'[{t}] never associated to the 32-byte SSID: {r.matched}'
    # ap_info reads the record path (ssid[33], NUL-terminated) -- the whole name.
    _ok(host, t, 'wifi_sta_get_ap_info', f'ssid={SSID_AT_MAX} rssi=-\\d+')


@pytest.mark.system
@pytest.mark.parametrize('transport', ['sdio'])
def test_sta_connected_event_ssid_short(emu_bench, transport):
    """NEGATIVE: a short SSID must report its own length, not a padded one.

    Guards the opposite error: taking ssid_len from the wire must not report
    trailing zero bytes as part of the name."""
    host = _ready(emu_bench, transport)
    t = transport
    _ok(host, t, 'wifi_set_mode 1')
    for c in ('wifi_cfg_reset', 'wifi_cfg_set sta_ssid myssid',
              'wifi_cfg_set sta_password mypassword', 'wifi_set_config sta'):
        _ok(host, t, c)
    _ok(host, t, 'wifi_connect')
    r = eh_test_expect(host, r'EH event wifi_sta_connected', fail=FAIL, timeout=60)
    assert r.ok, f'[{t}] never associated: {r.matched}'
    _ok(host, t, 'wifi_sta_get_ap_info', r'ssid=myssid rssi=-\d+')
