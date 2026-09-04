"""Phase 1: a single esp_hosted_deinit() stops every datapath task.

The interesting case is a deinit while the datapath is OPEN and the tasks are
live, because recv_task then sits in or around if_ops->read(). A CP whose
datapath never opened takes the easy exit path, so the preconditions below are
asserted, not assumed:

  1. the datapath is open and has carried traffic (the slave-up event), so
     recv_task and host_reset_task have both done real work. The emulator has
     no AP, so association is not available as a precondition here.
  2. task-dump lists the datapath tasks this build actually creates

Postconditions are positive, not an absence of crashes:

  3. deinit reports the exact set of tasks it stopped
  4. task-dump no longer lists any of the three
  5. no fault in a quiet window afterwards
"""
import os, re, sys, time
import pytest
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

_FW = os.path.join(os.path.dirname(__file__), '..', '..', 'fw', 'cp_deinit_init_cycles')
CP_INJECT = {f'components/cp_deinit_init_cycles/{f}':
             os.path.abspath(os.path.join(_FW, f))
             for f in ('cp_deinit_init_cycles.c', 'CMakeLists.txt',
                       'Kconfig.projbuild')}

EXAMPLE = 'power_save/host+cp/network_split__host_deep_sleep_cp_light_sleep'
# send_task exists only when the TX priority queues are compiled in
# (BYPASS_TX_PRIORITY_Q is 1 today), so the set is derived from the dump.
ALWAYS = ('recv_task', 'host_reset_task')
OPTIONAL = ('send_task',)
FAIL = FATAL_PATTERNS + ['CORRUPT HEAP', 'Firmware abort', 'tasks still running']


def _task_dump(cp, tag):
    """Sync on the dump's own header, not the prompt: the CP prompt carries no
    trailing newline, so a line-based expect can miss it."""
    seen = len(getattr(cp, '_buf', ''))
    cp.write('task-dump')
    r = eh_test_expect(cp, r'Name\s+Number\s+Priority\s+StackWaterMark',
                       fail=FAIL, timeout=20)
    assert r.ok, f'task-dump at {tag}: {r.matched}'
    time.sleep(2)   # let the table finish printing
    out = getattr(cp, '_buf', '')[seen:]
    assert 'configUSE_TRACEFACILITY' not in out, 'trace facility not enabled in this build'
    assert 'IDLE' in out, f'task-dump at {tag} looks truncated: {out[-200:]!r}'
    return out


@pytest.mark.power_save
@pytest.mark.sanity
def test_cp_deinit_stops_every_task(emu_bench):
    # The hosted-deinit command lives in the test harness component, not the
    # product CLI. Cycles stay 0, so it registers the command and runs no task.
    b = emu_bench(EXAMPLE, 'esp_host', 'sdio', timeout='300s',
                  cp_extra_ovl=['CONFIG_FREERTOS_USE_TRACE_FACILITY=y'],
                  cp_inject=CP_INJECT)
    host, cp = b['host'], b['cp']

    # Precondition 1: datapath open, and traffic has crossed it.
    assert eh_test_expect(cp, r'Open Data Path', fail=FAIL, timeout=90).ok, 'datapath never opened'
    assert eh_test_expect(cp, r'Send slave up event', fail=FAIL, timeout=90).ok, \
        'transport never carried the slave-up event'
    assert eh_test_expect(host, r'host>', fail=FAIL, timeout=90).ok, 'host CLI never came up'

    # Precondition 2: the tasks this fix stops are actually running.
    before = _task_dump(cp, 'before deinit')
    missing = [t for t in ALWAYS if t not in before]
    assert not missing, f'tasks absent before deinit: {missing}'
    running = list(ALWAYS) + [t for t in OPTIONAL if t in before]
    print(f'[deinit] tasks running before deinit: {running}')

    cp.write('hosted-deinit')
    # Postcondition 3: deinit states the set it stopped.
    r = eh_test_expect(cp, r'deinit: tasks stopped \(0x[0-9a-f]{2}\)', fail=FAIL, timeout=30)
    assert r.ok, f'deinit did not report stopping tasks: {r.matched}'
    assert eh_test_expect(cp, r'CP deinitialized successfully', fail=FAIL, timeout=30).ok

    time.sleep(5)
    # Postcondition 4: the tasks are gone, not merely quiet.
    after = _task_dump(cp, 'after deinit')
    still = [t for t in running if t in after]
    assert not still, f'tasks still listed after deinit: {still}'

    # Postcondition 5: no late fault.
    time.sleep(8)
    tail = getattr(cp, '_buf', '')
    tail = tail[tail.rfind('hosted-deinit'):]
    for bad in ('Backtrace:', 'Firmware abort', 'Guru'):
        assert bad not in tail, f'{bad} after deinit'
    print(f'[deinit] datapath open + traffic crossed; deinit stopped {running}; none listed after')
