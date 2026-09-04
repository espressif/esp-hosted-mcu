"""TCP throughput with reasoning.

Measures TCP TX and RX SEPARATELY on the network-split example and, critically,
records every link parameter that changes the answer. A bare Mbits/sec number is
not a result: on this hardware the same channel, bandwidth, MCS, RSSI and
power-save setting produced 1.36 Mbps and 36.61 Mbps, and the only difference
visible anywhere was the rate-control band (`(trc)band`).

The run ends with a decorated summary that states the numbers AND every check,
so a bad reading explains itself instead of looking like a mystery.

  EH_TP_SSID / EH_TP_PASS   AP credentials            (required)
  EH_TP_PEER                iperf peer (this machine) (required)
  EH_TP_BAND                2g | 5g | auto            (default 5g)
  EH_TP_N                   repeats per direction     (default 3)
  EH_TP_MIN_TX / MIN_RX     Mbits/sec floor, 0 = only report  (default 0)
"""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))
import pytest
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS
from infra import wifi_diag

EX = 'power_save/host+cp/network_split__host_deep_sleep_cp_light_sleep'
FAIL_PATTERNS = FATAL_PATTERNS + ['bring-up timed out']
IPERF_SECS = 10


def _env(name, default=None, required=False):
    v = os.environ.get(name, default)
    if required and not v:
        pytest.skip(f'{name} not set')
    return v


def _totals(text):
    """Every '0.0-10.0 sec  N Mbits/sec' total the board printed."""
    return [float(x) for x in re.findall(
        r'0\.0-\s*\d+\.\d+ sec\s+([0-9.]+) Mbits/sec', text)]


def _peer_totals(out):
    return [float(x) for x in re.findall(
        r'0\.00-\s*\d+\.\d+ sec\s+[0-9.]+ \w?Bytes\s+([0-9.]+) Mbits/sec', out)]


@pytest.mark.system
@pytest.mark.parametrize('transport', ['sdio'])
def test_tcp_throughput(bench, transport, request):
    ssid = _env('EH_TP_SSID', required=True)
    password = _env('EH_TP_PASS', required=True)
    peer = _env('EH_TP_PEER', required=True)
    band = _env('EH_TP_BAND', '5g')
    n = int(_env('EH_TP_N', '3'))
    min_tx = float(_env('EH_TP_MIN_TX', '0'))
    min_rx = float(_env('EH_TP_MIN_RX', '0'))

    b = bench(EX, 'esp_host', transport, timeout='900s')
    host, cp = b['host'], b['cp']
    assert eh_test_expect(host, r'console ready|Steps to test|host>',
                          fail=FAIL_PATTERNS, timeout=90).ok, 'console did not come up'

    # Power save OFF: this is a throughput measurement, not a power measurement.
    host.write('ps none\n')
    eh_test_expect(host, r'ps none: ESP_OK', timeout=15)
    time.sleep(1)

    # Force a REAL band-mode transition before associating. esp_wifi syncs the
    # rate-control band only when set_band_mode actually changes value, so
    # setting the band it is already on leaves the wrong rate table in place
    # (measured: 27x less TX). Toggling guarantees the transition.
    other = '2g' if band != '2g' else '5g'
    host.write(f'band {other}\n')
    eh_test_expect(host, r'band .*: ESP_OK', timeout=15)
    time.sleep(2)
    host.write(f'band {band}\n')
    eh_test_expect(host, r'band .*: ESP_OK', timeout=15)
    time.sleep(2)

    host.write(f'sta {ssid} {password}\n')
    r = eh_test_expect(host, r'sta ip: \d+\.\d+\.\d+\.\d+', fail=FAIL_PATTERNS, timeout=90)
    assert r.ok, f'no DHCP address: {r.matched}'
    # Take the address from the log on disk: expect().matched does not reliably
    # carry the matched text, and an empty IP silently skips the whole RX phase.
    time.sleep(3)
    _, _h = _read_logs(request)
    ips = re.findall(r'sta ip: (\d+\.\d+\.\d+\.\d+)', _h)
    board_ip = ips[-1] if ips else os.environ.get('EH_TP_BOARD_IP', '')
    assert board_ip, 'could not determine the board IP address'
    print(f'[tp] board_ip={board_ip} peer={peer} band={band}')
    time.sleep(2)

    # ---- TX: board -> peer, N repeats, no reconfiguration between them ----
    for _ in range(n):
        host.write(f'iperf -c {peer} -i 1 -t {IPERF_SECS}\n')
        eh_test_expect(host, r'THIS_NEVER_MATCHES', timeout=IPERF_SECS + 12)
        time.sleep(3)

    # ---- RX: peer -> board, measured separately (never bidirectional) ----
    host.write('iperf --abort\n')
    time.sleep(2)
    rx = []
    if board_ip:
        host.write(f'iperf -s -i 1 -t {IPERF_SECS * (n + 1) + 20}\n')
        srv = eh_test_expect(host, r'mode=tcp-server', timeout=20)
        if not srv.ok:
            print('[tp] board iperf server did not start; RX skipped')
        time.sleep(2)
        for _ in range(n):
            try:
                out = subprocess.run(
                    ['iperf', '-c', board_ip, '-i', '1', '-t', str(IPERF_SECS)],
                    capture_output=True, text=True, timeout=IPERF_SECS + 40).stdout
                rx += _peer_totals(out)
            except (subprocess.TimeoutExpired, FileNotFoundError) as e:
                print(f'[tp] peer iperf failed: {e}')
            time.sleep(2)
    host.write('iperf --abort\n')
    time.sleep(2)

    # Read the FULL logs off disk: pexpect .before only holds unconsumed bytes.
    cp_log, host_txt = _read_logs(request)
    tx = _totals(host_txt)[:n]

    d = wifi_diag.parse(cp_log, host_txt)
    report = wifi_diag.summary(d, tx, rx, ssid=ssid, transport=transport)
    print('\n' + report)
    _write_artifact(request, report)

    problems = [f'{t}' for v, t, _ in wifi_diag.check(d) if v == wifi_diag.FAIL]
    if not tx:
        problems.append('no TCP TX total captured')
    if board_ip and not rx:
        problems.append('no TCP RX total captured')
    for name, vals in (('TX', tx), ('RX', rx)):
        if len(vals) >= 2 and max(vals) > 0:
            spread = (max(vals) - min(vals)) / max(vals)
            if spread > 0.30:
                problems.append(
                    f'{name} samples vary {spread * 100:.0f}% '
                    f'({min(vals):.2f}-{max(vals):.2f}) - link not steady')
    if tx and min_tx and min(tx) < min_tx:
        problems.append(f'TX {min(tx):.2f} < floor {min_tx} Mbits/sec')
    if rx and min_rx and min(rx) < min_rx:
        problems.append(f'RX {min(rx):.2f} < floor {min_rx} Mbits/sec')
    assert not problems, 'throughput run not valid:\n  - ' + '\n  - '.join(problems)


def _read_logs(request):
    """Full CP and host console logs from the bench's scratch dir."""
    cp_txt = host_txt = ''
    try:
        d = request.getfixturevalue('lab_tmp')
        for f in sorted(d.glob('*.log')):
            t = f.read_text(errors='replace')
            if f.name.startswith('cp'):
                cp_txt += t
            elif f.name.startswith('host'):
                host_txt += t
    except Exception as e:
        print(f'[tp] could not read logs: {e}')
    return cp_txt, host_txt


def _write_artifact(request, report):
    try:
        d = request.getfixturevalue('lab_tmp')
        (d / 'throughput_summary.txt').write_text(report)
    except Exception:
        pass
