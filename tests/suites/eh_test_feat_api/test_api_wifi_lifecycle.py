# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Wi-Fi init/deinit lifecycle as the host app sees it (S46).

Every return code and STA event must match plain ESP-IDF for the same call
sequence (components/esp_wifi/src/wifi_init.c):

  init while inited           ESP_ERR_INVALID_STATE   esp_wifi_init()
  deinit while started        ESP_ERR_WIFI_NOT_STOPPED wifi_deinit_internal()
  deinit while not inited     ESP_ERR_WIFI_NOT_INIT   esp_wifi_deinit()
  start/get_mode/set_config/connect while not inited   ESP_ERR_WIFI_NOT_INIT

The exerciser host inits, sets STA mode and starts Wi-Fi at boot, so its first
`wifi_init` command is a second init.
"""
import os
import re
import sys
import time

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

FAIL = FATAL_PATTERNS + ['bring-up timed out']
EX = 'system/api_exerciser'
INVALID_STATE, NOT_INIT, NOT_STOPPED = 0x103, 0x3001, 0x3003
NOT_STARTED, WIFI_MODE, WIFI_CONN, FAIL_RC = 0x3002, 0x3005, 0x3007, -1
DISCONNECTED = 'EH event wifi_sta_disconnected'
ABSENT_AP = 'eh-no-such-ap'
CYCLES = 2
STA_START, STA_STOP = 'EH event wifi_sta_start', 'EH event wifi_sta_stop'
CONNECTED = 'EH event wifi_sta_connected'


def _rc(host, cmd, rc=0, extra=''):
    host.write(cmd)
    base = cmd.split()[0]
    r = eh_test_expect(host, rf'EH rc={rc} cmd={base}' + (f' {extra}' if extra else ''),
                       fail=FAIL + [rf'EH rc=(?!{rc}\b)-?\d+ cmd={base}\b'], timeout=20)
    assert r.ok, f'{cmd}: want rc={rc}: {r.matched}'


def _rc_row(host, cmd, rc, bad, name):
    """One rc row of a table: the whole table is recorded before the test fails."""
    mark = _mark(host)
    host.write(cmd)
    base = cmd.split()[0]
    pat = rf'EH rc=(-?\d+) cmd={base}\b'
    r = eh_test_expect(host, pat, fail=FAIL, timeout=20)
    m = re.search(pat, _since(host, mark)) if r.ok else None
    got = int(m.group(1)) if m else None
    if got != rc:
        bad.append(f'{name}: {cmd} rc={got}, want {rc}')


def _mark(dut):
    with dut._lock:
        return len(dut._buf)


def _since(dut, mark):
    with dut._lock:
        return dut._buf[mark:]


def _wait_for(host, mark, line, timeout):
    """Event lines and rc lines interleave freely: search the log since mark."""
    end = time.time() + timeout
    while time.time() < end:
        if line in _since(host, mark):
            return True
        time.sleep(0.2)
    return False


@pytest.mark.system
@pytest.mark.wifi
def test_wifi_init_deinit_rc(bench, sta_ap):
    b = bench(EX, 'mcu_host', 'sdio', timeout='300s', wake=True)
    host = b['host']
    if not hasattr(host, '_buf'):
        pytest.skip('event counting needs the emu DUT log buffer')
    if 'wifi_ap' not in b['caps'] or not sta_ap:
        pytest.skip('bench has no associable AP')
    assert eh_test_expect(host, r'EH api_exerciser ready', fail=FAIL, timeout=60).ok

    _rc(host, 'wifi_init', INVALID_STATE)
    _rc(host, 'wifi_deinit', NOT_STOPPED)
    _rc(host, 'wifi_get_mode', 0, 'mode=1')

    # Started, not associated: the rc and events every request gets (idf-measured M6, R10, R14).
    bad = []
    mark = _mark(host)
    _rc_row(host, 'wifi_disconnect', 0, bad, 'M6 disconnect idle')
    time.sleep(3)
    if DISCONNECTED in _since(host, mark):
        bad.append('M6 disconnect idle: posted STA_DISCONNECTED')
    for cmd in ('wifi_cfg_reset', f'wifi_cfg_set sta_ssid {ABSENT_AP}',
                'wifi_cfg_set sta_password nopassword', 'wifi_set_config sta'):
        _rc(host, cmd)
    mark = _mark(host)
    _rc_row(host, 'wifi_connect', 0, bad, 'R10 connect, AP absent')
    _rc_row(host, 'wifi_connect', WIFI_CONN, bad, 'R10 connect during the attempt')
    # The same config during the attempt is already in place: OK, the attempt goes on
    # (no teardown, no second attempt). IDF itself answers WIFI_STATE (R4).
    _rc_row(host, 'wifi_set_config sta', 0, bad, 'set_config(same) during the attempt')
    if not _wait_for(host, mark, DISCONNECTED, 20):
        bad.append('R10 AP absent: no STA_DISCONNECTED')
    time.sleep(8)                           # IDF never retries on its own (M19, M20)
    seg = _since(host, mark)
    if seg.count(DISCONNECTED) != 1 or f'{DISCONNECTED} reason=201' not in seg:
        bad.append(f'R10 AP absent: {seg.count(DISCONNECTED)} STA_DISCONNECTED, want 1 reason=201 '
                   '(a second one = the same-config set_config restarted the attempt)')
    _rc(host, 'wifi_set_mode 2')
    _rc_row(host, 'wifi_connect', WIFI_MODE, bad, 'R14 connect in AP-only mode')
    _rc_row(host, 'wifi_disconnect', FAIL_RC, bad, 'R14 disconnect in AP-only mode')
    _rc(host, 'wifi_set_mode 1')
    time.sleep(2)

    mark = _mark(host)
    _rc(host, 'wifi_stop')
    assert _wait_for(host, mark, STA_STOP, 20), 'no STA_STOP after wifi_stop'
    mark = _mark(host)
    _rc_row(host, 'wifi_connect', NOT_STARTED, bad, 'R13 connect after stop')
    _rc_row(host, 'wifi_disconnect', NOT_STARTED, bad, 'R13 disconnect after stop')
    time.sleep(2)
    if 'EH event wifi_sta' in _since(host, mark):
        bad.append('R13 connect/disconnect after stop posted an event')
    _rc(host, 'wifi_deinit')
    _rc(host, 'wifi_deinit', NOT_INIT)
    for cmd in ('wifi_start', 'wifi_get_mode', 'wifi_set_config sta', 'wifi_connect'):
        _rc(host, cmd, NOT_INIT)

    creds = ('wifi_cfg_reset', f'wifi_cfg_set sta_ssid {sta_ap["ssid"]}',
             f'wifi_cfg_set sta_password {sta_ap["password"]}', 'wifi_set_config sta')
    for c in range(1, CYCLES + 1):
        mark = _mark(host)
        _rc(host, 'wifi_init')
        _rc(host, 'wifi_set_mode 1')
        for cmd in creds:
            _rc(host, cmd)
        _rc(host, 'wifi_start')
        _rc(host, 'wifi_connect')
        assert _wait_for(host, mark, CONNECTED, 60), f'cycle {c}: no STA_CONNECTED'
        _rc(host, 'wifi_stop')
        assert _wait_for(host, mark, STA_STOP, 20), f'cycle {c}: no STA_STOP'
        _rc(host, 'wifi_deinit')
        time.sleep(3)                       # room for a duplicate event
        seg = _since(host, mark)
        for ev, n in ((STA_START, 1), (CONNECTED, 1), (STA_STOP, 1)):
            assert seg.count(ev) == n, f'cycle {c}: {ev} x{seg.count(ev)}, want {n}'
    assert not bad, 'rc / events vs IDF:\n' + '\n'.join(bad)


AP_START, AP_STOP = 'EH event wifi_ap_start', 'EH event wifi_ap_stop'
EMU_AP_BSSID = '26:0a:c4:00:00:01'     # esp-emu SoftAP
AP_CYCLES = 5
EVENT = re.compile(r'EH event wifi_(\w+)')

# Calls in APSTA and across mode changes, with the events plain IDF posts for each
# (intent/idf-measured.md M22-M31). The station is associated before the first row.
# D-b: a connect with a new config while associated is carried out (IDF keeps the old
# link, M10): DISCONNECTED for the old link, then the attempt's outcome.
APSTA_STEPS = (
    ('M8',  ['wifi_connect'], []),
    ('D-b', ['wifi_cfg_set sta_ssid ' + ABSENT_AP, 'wifi_set_config sta', 'wifi_connect'],
            ['sta_disconnected', 'sta_disconnected']),
    ('',    ['wifi_cfg_set sta_ssid {ssid}', 'wifi_set_config sta', 'wifi_connect'],
            ['sta_connected']),
    ('D-b', ['wifi_cfg_set sta_bssid {bssid}', 'wifi_set_config sta', 'wifi_connect'],
            ['sta_disconnected', 'sta_connected']),
    ('M22', ['wifi_set_mode 3'], ['ap_start']),
    ('M23', ['wifi_stop'], ['sta_disconnected', 'sta_stop', 'ap_stop']),
    ('M24', ['wifi_start'], ['sta_start', 'ap_start']),
    ('',    ['wifi_connect'], ['sta_connected']),
    ('M25', ['wifi_set_mode 2'], ['sta_disconnected', 'sta_stop']),
    ('M26', ['wifi_set_mode 3'], ['sta_start']),
    ('',    ['wifi_connect'], ['sta_connected']),
    ('M27', ['wifi_set_mode 1'], ['ap_stop']),
    ('M22', ['wifi_set_mode 3'], ['ap_start']),
    ('M28', ['wifi_disconnect'], ['sta_disconnected']),
    ('M29', ['wifi_cfg_reset', 'wifi_cfg_set ap_ssid ehap2', 'wifi_cfg_set ap_channel 1',
             'wifi_cfg_set ap_max_conn 2', 'wifi_set_config ap'],
            ['ap_stop', 'ap_start']),
    ('',    ['wifi_connect'], ['sta_connected']),
    ('M30', ['wifi_set_mode 0'], ['sta_disconnected', 'sta_stop', 'ap_stop']),
    ('M31', ['wifi_set_mode 1'], ['sta_start']),
)


def _events(host, mark):
    return EVENT.findall(_since(host, mark))


def _step(host, cmds, want):
    """Run one row; the events since its first call, once they stop coming."""
    mark = _mark(host)
    for cmd in cmds:
        _rc(host, cmd)
    end = time.time() + 20
    while time.time() < end and len(_events(host, mark)) < len(want):
        time.sleep(0.2)
    time.sleep(3)                           # room for late or duplicate events
    return _events(host, mark)


def _sta_connect(host, ap):
    for cmd in ('wifi_cfg_reset', f'wifi_cfg_set sta_ssid {ap["ssid"]}',
                f'wifi_cfg_set sta_password {ap["password"]}', 'wifi_set_config sta'):
        _rc(host, cmd)
    mark = _mark(host)
    _rc(host, 'wifi_connect')
    assert _wait_for(host, mark, CONNECTED, 60), 'no STA_CONNECTED'


@pytest.mark.system
@pytest.mark.wifi
@pytest.mark.parametrize('mode,transport', [('ap', 'sdio'), ('apsta', 'sdio'), ('apsta', 'spi_fd'),
                                            ('apsta', 'spi_hd'), ('apsta', 'uart')])
def test_softap_stop_start_back_to_back(bench, sta_ap, mode, transport):
    """Soft-AP and APSTA as the app sees them. apsta: every row of APSTA_STEPS posts
    exactly the IDF events in IDF order. Both: esp_wifi_stop then esp_wifi_start with
    no wait posts one STOP and one START per cycle for each running interface,
    however late the CP's own stop report lands."""
    b = bench(EX, 'mcu_host', transport, timeout='300s')
    host = b['host']
    if not hasattr(host, '_buf'):
        pytest.skip('event counting needs the emu DUT log buffer')
    if mode == 'apsta' and ('wifi_ap' not in b['caps'] or not sta_ap):
        pytest.skip('bench has no associable AP')
    assert eh_test_expect(host, r'EH api_exerciser ready', fail=FAIL, timeout=60).ok
    bad = []
    if mode == 'apsta':
        _sta_connect(host, sta_ap)
        time.sleep(2)
        fmt = dict(sta_ap, bssid=sta_ap.get('bssid') or EMU_AP_BSSID)
        for i, (mid, cmds, want) in enumerate(APSTA_STEPS, 1):
            cmds = [c.format(**fmt) for c in cmds]
            got = _step(host, cmds, want)
            if got != want:
                bad.append(f'row {i} {mid} {" ; ".join(cmds)}: got {got}, want {want}')
        _rc(host, 'wifi_set_mode 3')
    else:
        _rc(host, 'wifi_stop')
        _rc(host, 'wifi_set_mode 2')
        mark = _mark(host)
        _rc(host, 'wifi_start')
        assert _wait_for(host, mark, AP_START, 30), 'no AP_START'
    time.sleep(2)
    mark = _mark(host)
    for _ in range(AP_CYCLES):
        _rc(host, 'wifi_stop')
        _rc(host, 'wifi_start')
    time.sleep(5)                           # room for late or duplicate events
    seg = _since(host, mark)
    evs = [AP_STOP, AP_START] + ([STA_STOP, STA_START] if mode == 'apsta' else [])
    for ev in evs:
        if seg.count(ev) != AP_CYCLES:
            bad.append(f'back-to-back: {ev} x{seg.count(ev)}, want {AP_CYCLES}')
    if seg.rfind(AP_START) < seg.rfind(AP_STOP):
        bad.append('back-to-back: the AP ends stopped')
    assert not bad, '\n'.join(bad)


@pytest.mark.system
@pytest.mark.wifi
def test_disconnect_before_sta_start_wins(bench, sta_ap):
    """Auto-connect on start on: esp_wifi_start then esp_wifi_disconnect at once,
    before the CP's STA_START lands. The app's disconnect wins: no association."""
    b = bench(EX, 'mcu_host', 'sdio', timeout='300s', wake=True,
              overlay=['CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_CONNECT_ON_START=y'])
    host = b['host']
    if not hasattr(host, '_buf'):
        pytest.skip('event counting needs the emu DUT log buffer')
    if 'wifi_ap' not in b['caps'] or not sta_ap:
        pytest.skip('bench has no associable AP')
    assert eh_test_expect(host, r'EH api_exerciser ready', fail=FAIL, timeout=60).ok
    _rc(host, 'wifi_stop')
    for cmd in ('wifi_cfg_reset', f'wifi_cfg_set sta_ssid {sta_ap["ssid"]}',
                f'wifi_cfg_set sta_password {sta_ap["password"]}', 'wifi_set_config sta'):
        _rc(host, cmd)
    time.sleep(3)
    mark = _mark(host)
    host.write('wifi_start')
    host.write('wifi_disconnect')
    assert _wait_for(host, mark, STA_START, 20), 'no STA_START'
    time.sleep(15)
    assert CONNECTED not in _since(host, mark), 'associated after the app disconnected'


# ── Plain mode: provisioned, rebooted, connect without set_config (IDF reference:
# tests/fw/idf_wifi_persist). Flash storage keeps the config: connect -> CONNECTED.
# RAM storage loses it: connect -> ESP_ERR_WIFI_SSID, no event.
WIFI_SSID_ERR = 0x300a


@pytest.mark.system
@pytest.mark.wifi
@pytest.mark.parametrize('storage', ['flash', 'ram'])
@pytest.mark.parametrize('transport', ['sdio', 'uart'])
def test_reboot_connect_stored_config(bench, sta_ap, storage, transport):
    b = bench(EX, 'mcu_host', transport, timeout='300s')
    host = b['host']
    if not hasattr(host, '_buf'):
        pytest.skip('event counting needs the emu DUT log buffer')
    if 'wifi_ap' not in b['caps'] or not sta_ap:
        pytest.skip('bench has no associable AP')
    assert eh_test_expect(host, r'EH api_exerciser ready', fail=FAIL, timeout=60).ok
    _rc(host, f'wifi_set_storage {0 if storage == "flash" else 1}')
    _sta_connect(host, sta_ap)
    mark = _mark(host)
    host.write('host_restart')
    assert _wait_for(host, mark, 'EH api_exerciser ready', 90), 'host did not come back'
    time.sleep(2)
    mark = _mark(host)
    host.write('wifi_get_config sta')
    eh_test_expect(host, r'EH rc=-?\d+ cmd=wifi_get_config', fail=FAIL, timeout=20)
    want_rc = 0 if storage == 'flash' else WIFI_SSID_ERR
    _rc(host, 'wifi_connect', want_rc)
    time.sleep(8)
    seg = _since(host, mark)
    if storage == 'flash':
        assert seg.count(CONNECTED) == 1, f'connect with the stored config: {seg.count(CONNECTED)} STA_CONNECTED'
    else:
        assert 'EH event wifi_sta' not in seg.replace(STA_START, ''), 'a failed connect posted an event'


# ── Plain mode: the host reboots without resetting the CP (reset only if needed,
# SDIO only) while the CP holds the old app's link or attempt. A kept link is
# adopted: one STA_CONNECTED, and the app's own connect to it adds nothing. A moved
# config switches AP as IDF does: one STA_DISCONNECTED, one STA_CONNECTED. A
# leftover attempt the new app never made stays hidden.
@pytest.mark.system
@pytest.mark.wifi
@pytest.mark.parametrize('leftover', ['link', 'link_moved', 'attempt'])
def test_plain_restart_leftover_link(bench, sta_ap, leftover):
    b = bench(EX, 'mcu_host', 'sdio', timeout='300s',
              overlay=['CONFIG_ESP_HOSTED_HOST_CP_RESET_STRATEGY_ONLY_IF_NECESSARY=y'])
    host = b['host']
    if not hasattr(host, '_buf'):
        pytest.skip('event counting needs the emu DUT log buffer')
    if 'wifi_ap' not in b['caps'] or not sta_ap:
        pytest.skip('bench has no associable AP')
    assert eh_test_expect(host, r'EH api_exerciser ready', fail=FAIL, timeout=60).ok
    if leftover == 'attempt':
        for cmd in ('wifi_cfg_reset', f'wifi_cfg_set sta_ssid {ABSENT_AP}',
                    'wifi_cfg_set sta_password nopassword', 'wifi_set_config sta', 'wifi_connect'):
            _rc(host, cmd)
    else:
        _sta_connect(host, sta_ap)
    mark = _mark(host)
    host.write('host_restart')
    assert _wait_for(host, mark, 'EH api_exerciser ready', 90), 'host did not come back'
    time.sleep(8)
    seg = _since(host, mark)
    assert not re.search(r'Reset co-processor|Attempt slave reset|Slave reset issued', seg), \
        'the CP was reset: the case needs a running CP'
    assert seg.count(STA_START) == 1, f'STA_START x{seg.count(STA_START)}'
    adopted = 0 if leftover == 'attempt' else 1
    assert seg.count(CONNECTED) == adopted, f'after restart: STA_CONNECTED x{seg.count(CONNECTED)}'
    assert DISCONNECTED not in seg, 'after restart: STA_DISCONNECTED the app did not cause'
    mark = _mark(host)
    for cmd in ('wifi_cfg_reset', f'wifi_cfg_set sta_ssid {sta_ap["ssid"]}',
                f'wifi_cfg_set sta_password {sta_ap["password"]}'):
        _rc(host, cmd)
    if leftover == 'link_moved':
        _rc(host, f'wifi_cfg_set sta_bssid {sta_ap.get("bssid") or EMU_AP_BSSID}')
    _rc(host, 'wifi_set_config sta')
    _rc(host, 'wifi_connect')
    time.sleep(10)
    seg = _since(host, mark)
    want = {'link': (0, 0), 'link_moved': (1, 1), 'attempt': (1, 0)}[leftover]
    got = (seg.count(CONNECTED), seg.count(DISCONNECTED))
    assert got == want, f'app connect: (STA_CONNECTED, STA_DISCONNECTED) = {got}, want {want}'
