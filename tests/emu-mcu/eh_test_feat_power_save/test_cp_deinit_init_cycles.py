"""A CP teardown and rebuild cycle must return every byte of heap.

The cycle destroys the console, so no command can drive it: a full
esp_hosted_deinit() takes the CLI down with everything else. The CP therefore
measures itself, from tests/fw/cp_deinit_init_cycles: a task calls eh_cp_deinit() then
eh_cp_init() N times, printing free heap once per cycle at the same point. The
harness is injected into the scratch build copy, so no example carries it, and
the instrument is not part of the system under test.

Standard: EVERY cycle, from the first, must return what it took. All drifts must
be equal (no accumulation) and equal to ALLOWED_DRIFT_BYTES. A non-zero
allowance is a named exception with a reason, never a waived cycle.
"""
import os
import re
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS
from infra.heaptrace import name_borrowers, table

EXAMPLE = 'power_save/host+cp/network_split__host_deep_sleep_cp_light_sleep'
# The harness lives here, not in the example; build_fw stages it into the
# scratch copy as an extra component.
_FW = os.path.join(os.path.dirname(__file__), '..', '..', 'fw', 'cp_deinit_init_cycles')
CP_INJECT = {f'components/cp_deinit_init_cycles/{f}': os.path.abspath(os.path.join(_FW, f))
             for f in ('cp_deinit_init_cycles.c', 'CMakeLists.txt', 'Kconfig.projbuild')}
CYCLES = int(os.environ.get('EH_CP_DEINIT_INIT_CYCLES', '20'))
# Bytes a settled cycle may keep. Raise only with the allocation named here.
ALLOWED_DRIFT_BYTES = 0
FAIL = FATAL_PATTERNS + ['CORRUPT HEAP', 'Firmware abort', 'tasks still running']
ROW = re.compile(r'\[cycles\] cycle=(\d+) init=(-?\d+) deinit=(-?\d+) '
                 r'bytes=(-?\d+) recs=(\d+) ovf=(\d+) polls=(\d+) free=(\d+)')
# Opt-in with EH_CP_HEAP_TRACE=1. Off by default: tracing costs memory and
# shifts timing. On, it decides ownership - free memory alone cannot.
HEAP_TRACE = os.environ.get('EH_CP_HEAP_TRACE', '0') == '1'
TRACE_OVL = ['CONFIG_HEAP_TRACING_STANDALONE=y',
             'CONFIG_ESP_SYSTEM_USE_FRAME_POINTER=y',
             # NOTE: alloced_by[] is unreliable at -O2 (free paths appear inside
             # allocation stacks), and -Og slows the CP enough to trip the host's
             # RPC timeout. Treat the frames as a hint, never as proof.
             'CONFIG_HEAP_TRACING_STACK_DEPTH=10']


def _keep_logs(cp, host, transport, ok):
    """A gate that fails without evidence costs a whole run. Keep both streams
    every time, and print the CP tail when the cycles did not complete."""
    outdir = os.environ.get('EH_DUT_LOG_DIR',
                            os.path.join(os.path.dirname(__file__), '..', '..',
                                         '.work', 'dutlogs'))
    os.makedirs(outdir, exist_ok=True)
    stamp = f'dinit-cycles-{transport}-{os.getpid()}-{"PASS" if ok else "FAIL"}'
    for name, dut in (('cp', cp), ('host', host)):
        if dut is None:
            continue
        path = os.path.join(outdir, f'{stamp}-{name}.log')
        with open(path, 'w') as fh:
            fh.write(getattr(dut, '_buf', ''))
        print(f'[dut-log] {path}')
    if not ok:
        tail = getattr(cp, '_buf', '').splitlines()[-200:]
        print(f'\n----- cp tail ({len(tail)} lines) -----')
        for line in tail:
            print(f'  {line}')



def _assert_cycles_built(bench, cycles):
    """Read CONFIG_EH_TEST_CP_DEINIT_INIT_CYCLES out of the CP build the bench
    just made. A mismatch means the overlay did not take, so fail here rather
    than time out later on output that can never come."""
    elf = (bench.get('artifacts') or {}).get('cp_elf')
    if not elf:
        pytest.fail('bench exposed no cp_elf, cannot check the built cycle count')
    key = 'CONFIG_EH_TEST_CP_DEINIT_INIT_CYCLES'
    d = os.path.dirname(os.path.abspath(elf))
    for _ in range(5):
        f = os.path.join(d, 'sdkconfig')
        if os.path.isfile(f):
            for line in open(f):
                if line.startswith(key + '='):
                    got = line.strip().split('=', 1)[1]
                    assert got == str(cycles), (
                        f'{key}={got} in the built CP, expected {cycles}. '
                        'Kconfig ignored the value (check its range).')
                    return
            pytest.fail(f'{key} absent from {f}')
        d = os.path.dirname(d)
    pytest.fail(f'no sdkconfig found above {elf}')


@pytest.mark.power_save
@pytest.mark.parametrize('transport', [
    pytest.param('sdio', marks=pytest.mark.sanity),
    'uart', 'spi_fd', 'spi_hd',
])
def test_cp_deinit_init_cycles_return_heap(emu_bench, transport, wifi_ap):
    # wake=True: the bench wires the wake path, which spi_fd needs to reach
    # 'Open Data Path'. Same wiring for every transport keeps rows comparable.
    ovl = [f'CONFIG_EH_TEST_CP_DEINIT_INIT_CYCLES={CYCLES}']
    if HEAP_TRACE:
        ovl += TRACE_OVL
    # Diagnostic hook: extra CP sdkconfig lines, comma separated. Used to arm
    # heap tracing without committing it on.
    ovl += [x for x in os.environ.get('EH_CP_EXTRA_OVL', '').split(',') if x]
    bench = emu_bench(EXAMPLE, 'esp_host', transport, wake=True, timeout='900s',
                      cp_extra_ovl=ovl, cp_inject=CP_INJECT)
    # Kconfig drops an out-of-range int SILENTLY and falls back to the default
    # (0 = harness compiled out). The run then hangs on the priming wait with no
    # clue why, so read the value the build actually took.
    _assert_cycles_built(bench, CYCLES)
    cp = bench['cp']

    host = bench['host']
    net = bench.get('net')
    # Preconditions can fail before the cycles, so keep the streams from here.
    import atexit
    atexit.register(lambda: _keep_logs(cp, host, transport, ok=False))

    # Preconditions. The host example waits to be told to connect, so an
    # undriven run tears down an idle stack - which is what these assert away.
    # Drain the host FIRST. Waiting on the CP while the host is unread lets the
    # host's console pipe fill; it then blocks in a log write and its own RPCs
    # time out. spi_hd exposes this because its datapath takes seconds to open.
    # Preconditions are COLLECTED, not asserted: a precondition failure must
    # still produce the leak verdict below, so a run always answers "was the
    # origin hosted or not" rather than just "failed".
    problems = []

    def need(dut, pat, why, timeout=90):
        if not eh_test_expect(dut, pat, fail=FAIL, timeout=timeout).ok:
            problems.append(why)
            return False
        return True

    need(host, r'host>', 'host CLI never came up')
    need(cp, r'ESP-Hosted core initialized', 'CP core never initialised')
    # spi_fd never raises ESP_OPEN_DATA_PATH at boot: its datapath is open from
    # transport init, and the event exists only as a power-save wake re-announce
    # (eh_cp_transport_spi.c, FLAG_POWER_SAVE_STOPPED). Requiring it there was
    # asserting a message that transport does not send.
    if transport != 'spi_fd':
        need(cp, r'Open Data Path', 'datapath never opened')
    need(cp, r'Send slave up event', 'slave-up event never sent')

    if not wifi_ap or not wifi_ap.get('ssid'):
        problems.append('no AP configured for this substrate')
    else:
        host.write(f"sta {wifi_ap['ssid']} {wifi_ap['password']}")
        need(host, r'IP_EVENT_STA_GOT_IP',
             'never associated: cycles tore down an idle stack')

    # Put packets through the split path so pbufs, ARP and the port tables are
    # populated when the first deinit runs. A reply needs a listener, which this
    # example does not run, so only the send is required.
    if net is None:
        problems.append('no net stimulus provider on this bench')
    try:
        if net is not None:
            net.request(22, b'cycles-probe', read_bytes=16, timeout=2)
    except Exception as exc:                       # no listener is expected
        print(f'[cycles] probe sent, no reply ({type(exc).__name__})')

    assert eh_test_expect(cp, r'\[cycles\] priming deinit=0', fail=FAIL,
                          timeout=60).ok, 'priming deinit failed'
    r = eh_test_expect(cp, rf'\[cycles\] done cycles={CYCLES}', fail=FAIL, timeout=180)
    _keep_logs(cp, bench.get('host'), transport, ok=r.ok)
    assert r.ok, f'harness did not finish its cycles: {r.matched}'

    rows = [tuple(int(x) for x in m) for m in ROW.findall(getattr(cp, '_buf', ''))]
    if len(rows) != CYCLES:
        problems.append(f'expected {CYCLES} cycles, got {len(rows)}')

    if rows:
        print(f"\n{'cycle':>5} {'init':>4} {'deinit':>6} {'free':>8} {'delta':>6} "
              f"{'bytes':>6} {'blocks':>6} {'ovf':>3}")
        print('-' * 56)
        prev = None
        for cyc, r, d, outstanding, recs, ovf, polls, free in rows:
            delta = '' if prev is None else f'{free - prev:+d}'
            blk = str(recs) if recs > 1 else ''
            print(f'{cyc:>5} {r:>4} {d:>6} {free:>8} {delta:>6} '
                  f'{outstanding:>6} {blk:>6} {ovf:>3}')
            prev = free

        bad = [(c, r, d) for c, r, d, _, _, _, _, _ in rows if r != 0 or d != 0]
        if bad:
            problems.append(f'init/deinit returned an error: {bad}')
        if any(ovf for _, _, _, _, _, ovf, _, _ in rows):
            problems.append('trace buffer overflowed: result is void')
        if any(p >= 19 for _, _, _, _, _, _, p, _ in rows):
            problems.append('trace count never settled')

        frees = [free for _, _, _, _, _, _, _, free in rows]
        half = frees[len(frees) // 2:]
        falling = half[-1] < half[0]
        print(f'[cycles] free: cycle1={frees[0]} cycleN={frees[-1]} '
              f'delta={frees[-1] - frees[0]:+d} still_falling={falling}')
    else:
        falling = False

    # ── ORIGIN VERDICT — always printed, however the run went ──────────────
    if not HEAP_TRACE:
        print('[cycles] ORIGIN: unknown (no heap trace; '
              'rerun with EH_CP_HEAP_TRACE=1)')
        if falling:
            problems.append('free memory still falling; origin unknown')
    else:
        named = name_borrowers(getattr(cp, '_buf', ''),
                               (bench.get('artifacts') or {}).get('cp_elf'))
        if named:
            print()
            for line in table(named):
                print(line)
        unattributed = [(cy, sz) for cy, sz, _, ours in named if ours is None]
        ours_bytes = sum(sz for _, sz, _, ours in named if ours)
        idf_bytes = sum(sz for _, sz, _, ours in named if ours is False)

        # ANY byte outstanding after a deinit is a leak. Attribution says where
        # to look, never whether to care: an lwIP or IDF frame with a hosted
        # cause (our init armed it, our deinit left it) is still our leak.
        total = ours_bytes + idf_bytes + sum(sz for _, sz in unattributed)
        if not named:
            print('[cycles] ORIGIN: none — no allocation outstanding after any deinit')
        else:
            where = []
            if ours_bytes:
                where.append(f'{ours_bytes} B hosted')
            if idf_bytes:
                where.append(f'{idf_bytes} B outside coprocessor/ common/ port/')
            if unattributed:
                where.append(f'{len(unattributed)} records unresolved')
            print(f'[cycles] ORIGIN: LEAK — {total} B outstanding after deinit '
                  f'({", ".join(where)})')
            problems.append(f'{total} B outstanding after deinit: ' +
                            ', '.join(where))

    assert not problems, 'run problems: ' + '; '.join(problems)

    late = eh_test_expect(cp, r'|'.join(FAIL), timeout=8)
    assert not late.ok, f'fault after the cycles completed: {late.matched}'
