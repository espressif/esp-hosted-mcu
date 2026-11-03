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
  EH_TP_TRANSPORT           bus under test            (default sdio)
  EH_TP_BAUD                uart only: baud on both ends (default Kconfig, 921600)
  EH_TP_DIRS                tx,rx | rx | tx            (default tx,rx)
  EH_TP_PEER_RUN            prefix for the peer-side RX client (e.g. a launcher)
  EH_TP_PROTO               tcp | udp                  (default tcp)
  EH_TP_UDP_BW              udp offered load, Mbits/sec (uart default: just above 0.8 x baud)
  EH_TP_SETTLE              seconds to idle before reading the logs (default 0)

UDP is read at the receiver only: the peer server for TX, the board server
for RX (the board prints binary megabits; converted to 10^6).
"""
import math
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
# UART faults. Credit keeps the CP RX ring from overflowing, so any one is a
# defect. The RX tasks do not log; these lines come from other tasks.
UART_ERR = re.compile(r'uart transmit error|incoming data too big')
# The host bus dropped Wi-Fi RX because its delivery queue was full (SPI-FD).
WIFI_RX_DROP = re.compile(r'wifi rx queue full: \d+ frames dropped')
# CP: uart_evt_task and flow_ctrl_task, last line wins.
CP_EVT = re.compile(r'uart evt: fifo_ovf=(\d+) buf_full=(\d+)')
CP_DROP = re.compile(r'dropped frames: no rx buffer=(\d+) queue full=(\d+), resync bytes=(\d+)')
# Host packet stats (ESP_PKT_STATS), last line wins.
HOST_UART = re.compile(r'UART RX: fifo_ovf\[(\d+)\] resync_bytes\[(\d+)\] TX credit: '
                       r'unit\[(\d+)\] .*reset\[(\d+)\] bad\[(\d+)\]')


def _env(name, default=None, required=False):
    v = os.environ.get(name, default)
    if required and not v:
        pytest.skip(f'{name} not set')
    return v


def _totals(text):
    """Every '0.0-10.0 sec  N Mbits/sec' total the board printed. The end time
    must cover the run, else the first '-i 1' interval (0.0-1.0) counts too."""
    return [float(v) for end, v in re.findall(
        r'0\.0-\s*(\d+\.\d+) sec\s+([0-9.]+) Mbits/sec', text)
        if float(end) >= IPERF_SECS - 1]


def _peer_totals(out):
    return [float(v) for end, v in re.findall(
        r'0\.00?-\s*(\d+\.\d+) sec\s+[0-9.]+ \w?Bytes\s+([0-9.]+) Mbits/sec', out)
        if float(end) >= IPERF_SECS - 1]


def _interval_mean(out):
    """Mean of the full 1 s intervals of one session, without the partial
    first and last ones."""
    v = [float(x) for a, b, x in re.findall(
        r'\]\s+(\d+\.\d+)-\s*(\d+\.\d+) sec\s+[0-9.]+ \w?Bytes\s+([0-9.]+) Mbits/sec', out)
        if abs(float(b) - float(a) - 1.0) < 0.01]
    while v and v[0] == 0:
        v.pop(0)
    while v and v[-1] == 0:
        v.pop()
    v = v[1:-1]
    return [round(sum(v) / len(v), 3)] if v else []


@pytest.mark.system
@pytest.mark.parametrize('transport', [os.environ.get('EH_TP_TRANSPORT', 'sdio')])
def test_tcp_throughput(bench, transport, request):
    ssid = _env('EH_TP_SSID', required=True)
    password = _env('EH_TP_PASS', required=True)
    peer = _env('EH_TP_PEER', required=True)
    band = _env('EH_TP_BAND', '5g')
    n = int(_env('EH_TP_N', '3'))
    min_tx = float(_env('EH_TP_MIN_TX', '0'))
    min_rx = float(_env('EH_TP_MIN_RX', '0'))
    # macOS Local Network privacy blocks outbound LAN connects from a process
    # without the grant; this prefix runs the RX client under one that has it.
    peer_run = _env('EH_TP_PEER_RUN', '').split()
    udp = _env('EH_TP_PROTO', 'tcp') == 'udp'

    baud = int(_env('EH_TP_BAUD', '0')) if transport == 'uart' else 0
    udp_bw = int(_env('EH_TP_UDP_BW', '0')) or (math.ceil(baud * 0.8 / 1e6) if baud else 100)
    u = f' -u -b {udp_bw}' if udp else ''
    ovl = [f'CONFIG_ESP_HOSTED_HOST_UART_BAUD={baud}'] if baud else []
    cp_ovl = [f'CONFIG_EH_TRANSPORT_CP_UART_BAUDRATE={baud}'] if baud else []
    b = bench(EX, 'esp_host', transport, timeout='900s', overlay=ovl, cp_overlay=cp_ovl)
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
    dirs = _env('EH_TP_DIRS', 'tx,rx').split(',')
    peer_tx = []
    if 'tx' in dirs:
        # Own the peer server, one per sample. The esp iperf UDP client sends no
        # end-of-test datagram, so a shared iperf2 server never closes the
        # session; its 1 s intervals are the only trustworthy UDP figure.
        for _ in range(n):
            srv = subprocess.Popen(['iperf', '-s', '-B', peer, '-f', 'm', '-i', '1']
                                   + (['-u', '-t', str(IPERF_SECS + 8)] if udp else []),
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            time.sleep(1)
            host.write(f'iperf -c {peer}{u} -i 1 -t {IPERF_SECS}\n')
            eh_test_expect(host, r'THIS_NEVER_MATCHES', timeout=IPERF_SECS + 6)
            srv.terminate()
            out = srv.communicate(timeout=5)[0]
            got = _interval_mean(out) if udp else _peer_totals(out)[-1:]
            if not got:
                print(f'[tp] peer server gave no total:\n{out}')
            peer_tx += got
            time.sleep(2)
        print(f'[tp] peer-side TX totals: {peer_tx}')

    # ---- RX: peer -> board, measured separately (never bidirectional) ----
    host.write('iperf --abort\n')
    time.sleep(2)
    rx = []
    rx_from = len(_read_logs(request)[1])
    if board_ip and 'rx' in dirs:
        # The board's iperf server ends after one session: start it per sample.
        for _ in range(n):
            host.write(f'iperf -s{" -u" if udp else ""} -i 1 -t {IPERF_SECS if udp else IPERF_SECS + 20}\n')
            if not eh_test_expect(host, r'mode=(tcp|udp)-server', timeout=20).ok:
                print('[tp] board iperf server did not start')
            time.sleep(2)
            try:
                # Bind to `peer`: unbound, a VPN may route the LAN subnet into its tunnel.
                p = subprocess.run(
                    peer_run + ['iperf', '-c', board_ip, '-B', peer, '-f', 'm', '-i', '1', '-t', str(IPERF_SECS)]
                    + (['-u', '-b', f'{udp_bw}M'] if udp else []),
                    capture_output=True, text=True, timeout=IPERF_SECS + 40)
                got = [] if udp else _peer_totals(p.stdout)[-1:]
                if not got and not udp:
                    print(f'[tp] peer iperf gave no total:\n{p.stdout}{p.stderr}')
                rx += got
            except (subprocess.TimeoutExpired, FileNotFoundError) as e:
                print(f'[tp] peer iperf failed: {e}')
            host.write('iperf --abort\n')
            time.sleep(3)
    host.write('iperf --abort\n')
    time.sleep(2)

    # EH_TP_SETTLE: seconds to idle first, so both ends log their final counters.
    time.sleep(float(_env('EH_TP_SETTLE', '0')))
    # Read the FULL logs off disk: pexpect .before only holds unconsumed bytes.
    cp_log, host_txt = _read_logs(request)
    if udp:
        tx = peer_tx if 'tx' in dirs else []
        rx = [round(v * 1.048576, 3) for v in _totals(host_txt[rx_from:])][:n]
    else:
        tx = _totals(host_txt[:rx_from])[:n]

    d = wifi_diag.parse(cp_log, host_txt)
    report = wifi_diag.summary(d, tx, rx, ssid=ssid, transport=transport)
    print('\n' + report)
    _write_artifact(request, report)

    problems = [f'{t}' for v, t, _ in wifi_diag.check(d) if v == wifi_diag.FAIL]
    if transport == 'uart':
        problems += _uart_checks(cp_log, host_txt, baud, tx, rx)
    for line in WIFI_RX_DROP.findall(host_txt)[-1:]:
        print(f'[tp] host {line}')
    if not tx and 'tx' in dirs:
        problems.append('no TX total captured')
    if board_ip and not rx and 'rx' in dirs:
        problems.append('no RX total captured')
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
            if f.name.endswith('_build.log'):
                continue
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


def _uart_checks(cp_log, host_txt, baud, tx, rx):
    """Prove the claimed baud on both ends and that no reading beats the wire.
    8N1 puts 8 payload bits in 10, so goodput can never exceed 0.8 x baud."""
    out = []
    host_b = re.findall(r'bus applied: baud=(\d+)', host_txt)
    cp_b = re.findall(r'transport\[cp\]: UART port=\d+ (\d+) bps', cp_log)
    print(f'[tp] uart baud host={host_b[-1:] or "?"} cp={cp_b[-1:] or "?"} want={baud or "default"}')
    if not host_b or not cp_b:
        out.append('UART baud not confirmed in host and CP logs')
    # The host prints the divider's real rate (921600 -> 922190); 2% is UART tolerance.
    elif abs(int(host_b[-1]) - int(cp_b[-1])) > int(cp_b[-1]) * 0.02 or \
            (baud and int(cp_b[-1]) != baud):
        out.append(f'UART baud mismatch: host {host_b[-1]} cp {cp_b[-1]} want {baud}')
    applied = int(host_b[-1]) if host_b else baud
    if applied:
        ceil = applied * 0.8 / 1e6
        for name, vals in (('TX', tx), ('RX', rx)):
            for v in vals:
                print(f'[tp] {name} {v:.3f} Mbits/sec = {v / ceil * 100:.0f}% of 8N1 ceiling {ceil:.3f}')
                if v > ceil:
                    out.append(f'{name} {v:.3f} > 8N1 ceiling {ceil:.3f}: traffic bypassed UART')
    out += uart_faults(cp_log, host_txt)
    return out


def uart_faults(cp_log, host_txt):
    """UART transport faults from both logs; empty if none."""
    out = []
    n_err = len(UART_ERR.findall(cp_log + '\n' + host_txt))
    if n_err:
        out.append(f'{n_err} UART errors in the logs')
    if 'credit: unit ' not in host_txt:
        out.append('host did not enable UART credit')
    for name, rx, vals in (('CP uart evt fifo_ovf/buf_full', CP_EVT, cp_log),
                           ('CP no rx buffer/queue full/resync bytes', CP_DROP, cp_log),
                           ('host fifo_ovf/resync_bytes/-/credit reset/bad', HOST_UART, host_txt)):
        m = rx.findall(vals)
        print(f'[tp] {name}: {m[-1] if m else "none"}')
        # credit unit is not a fault
        last = [int(v) for i, v in enumerate(m[-1]) if not (rx is HOST_UART and i == 2)] if m else []
        if any(last):
            out.append(f'{name}: {m[-1]}')
    return out
