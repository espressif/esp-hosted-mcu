"""Raw transport throughput with per-frame loss accounting.

The api_exerciser `raw_tp` flood carries a 32-bit frame counter in the first
payload bytes; each receiver logs `raw_tp seq: rx frames=N lost=M ...` (totals
since boot). Raw TP bypasses Wi-Fi, so a gap is a frame the transport lost.

  EH_RTP_TRANSPORT   bus under test             (default uart)
  EH_RTP_BAUD        uart only: baud both ends  (default Kconfig)
  EH_RTP_DIRS        tx,rx | tx | rx | both     (default tx,rx; tx = host->CP)
  EH_RTP_SECS        flood seconds per direction (default 30)
  EH_RTP_STATS       1: packet stats on both ends, every 5 s (default 0)
"""
import os
import re
import time

import pytest

from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

EX = 'system/api_exerciser'
FAIL = FATAL_PATTERNS + ['bring-up timed out']
SEQ = re.compile(r'raw_tp seq: rx frames=(\d+) lost=(\d+) restarts=(\d+) tx frames=(\d+)')
KBPS_HOST = re.compile(r'sec Tx:(\d+) Rx:(\d+) Kbps')
KBPS_CP = re.compile(r'sec\s+Rx: ([0-9.]+) Tx: ([0-9.]+) kbps')
# Transport drop and recovery markers, either end.
DROP = re.compile(r'uart evt: .*|uart rx: dropped frames: .*|'
                  r'UART RX: fifo_ovf\[[1-9].*|UART RX: fifo_ovf\[\d+\] resync_bytes\[[1-9].*|'
                  r'send buffer\[\d+\] malloc fail|'
                  r'Host is power saving, skipping write')


def _env(k, d):
    return os.environ.get(k, d)


def _read_logs(request):
    cp_txt = host_txt = ''
    d = request.getfixturevalue('lab_tmp')
    for f in sorted(d.glob('*.log')):
        if f.name.endswith('_build.log'):
            continue
        t = f.read_text(errors='replace')
        if f.name.startswith('cp'):
            cp_txt += t
        elif f.name.startswith('host'):
            host_txt += t
    return cp_txt, host_txt


def _seq(txt):
    m = SEQ.findall(txt)
    return tuple(int(x) for x in m[-1]) if m else None


def _median(v):
    v = sorted(v)
    return v[len(v) // 2] if v else 0


@pytest.mark.system
@pytest.mark.parametrize('transport', [_env('EH_RTP_TRANSPORT', 'uart')])
def test_raw_tp_no_loss(bench, transport, request):
    baud = int(_env('EH_RTP_BAUD', '0')) if transport == 'uart' else 0
    secs = int(_env('EH_RTP_SECS', '30'))
    dirs = _env('EH_RTP_DIRS', 'tx,rx').split(',')
    ovl = [f'CONFIG_ESP_HOSTED_HOST_UART_BAUD={baud}'] if baud else []
    cp_ovl = ['CONFIG_ESP_RAW_TP_REPORT_INTERVAL=1']
    cp_ovl += [f'CONFIG_EH_TRANSPORT_CP_UART_BAUDRATE={baud}'] if baud else []
    if _env('EH_RTP_STATS', '0') == '1':
        ovl += ['CONFIG_ESP_HOSTED_HOST_PKT_STATS=y', 'CONFIG_ESP_HOSTED_HOST_PKT_STATS_INTERVAL_SEC=5']
        cp_ovl += ['CONFIG_ESP_PKT_STATS=y', 'CONFIG_ESP_PKT_STATS_INTERVAL_SEC=5']

    b = bench(EX, 'mcu_host', transport, timeout='600s', overlay=ovl, cp_overlay=cp_ovl)
    host, cp = b['host'], b['cp']
    r = eh_test_expect(host, r'EH api_exerciser ready', fail=FAIL, timeout=90)
    assert r.ok, f'console ready: {r.matched}'

    problems = []
    for d in dirs:
        # tx: host floods, CP counts. rx: CP floods, host counts.
        cp0, host0 = (len(t) for t in _read_logs(request))
        host.write(f'raw_tp start {d}')
        assert eh_test_expect(host, r'EH rc=0 cmd=raw_tp', fail=FAIL, timeout=15).ok, f'start {d}'
        time.sleep(5 + secs)        # generators wait 5 s before the first frame
        host.write('raw_tp stop')
        assert eh_test_expect(host, r'EH rc=0 cmd=raw_tp', fail=FAIL, timeout=15).ok, f'stop {d}'
        time.sleep(3)               # let the last frames land and a report print

        cp_txt, host_txt = _read_logs(request)
        cp_new, host_new = cp_txt[cp0:], host_txt[host0:]
        drops = sorted(set(DROP.findall(cp_new + host_new)))
        for rx in (['tx', 'rx'] if d == 'both' else [d]):
            # tx: CP counts frames from the host. rx: host counts frames from the CP.
            seq = _seq(cp_new if rx == 'tx' else host_new)
            if rx == 'tx':
                kbps = [float(m[0]) for m in KBPS_CP.findall(cp_new) if float(m[0]) > 0]
            else:
                kbps = [int(m[1]) for m in KBPS_HOST.findall(host_new) if int(m[1]) > 0]
            print(f'[rtp] {transport} baud={baud or "default"} dir={d}:{rx} '
                  f'rx kbps median={_median(kbps):.0f} seq={seq}')
            if not seq or seq[0] == 0:
                problems.append(f'{d}:{rx}: receiver counted no frames')
            # The CP sender reuses one buffer; only UART copies it before the next frame.
            elif seq[1] and (rx == 'tx' or transport == 'uart'):
                problems.append(f'{d}:{rx}: {seq[1]} of {seq[0] + seq[1]} frames lost')
        for line in drops[:8]:
            print(f'[rtp]   {line.strip()}')

    print(f'[rtp] verdict: {"LOSSLESS" if not problems else "; ".join(problems)}')
    assert not problems, problems
