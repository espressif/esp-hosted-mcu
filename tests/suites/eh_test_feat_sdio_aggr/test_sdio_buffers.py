# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""MCU-host SDIO buffer reservation: RX staging prealloc and the packet mempool.

Streaming RX staging slots are sized once from the co-processor INIT event
(the exact E2H aggregate under SW_AGGR, else one max frame times the
RX-buffer count the co-processor advertises) and logged as
`preallocated N SDIO RX staging slot(s)`. The packet mempool is a
separate option (`ESP_HOSTED_HOST_SDIO_USE_MEMPOOL`): off, each frame comes off
the DMA heap and that log is absent; on, transport start reserves
(host RX queue + 11) blocks of 1536 bytes and logs the size.

SDIO-only. The datapath and both logs live in eh_host_bus_sdio.c.
"""
import os
import re
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

# eh_host_bus_sdio.c: MIN_MEMPOOL_{BT,SERIAL,NET}_PACKETS and MAX_SDIO_BUFFER_SIZE.
_MEMPOOL_EXTRA = 3 + 3 + 5
_MAX_FRAME = 1536
_STREAM_Q = 14  # SDIO_STREAM_QUEUE_SIZE in eh_cp_transport_sdio.c
_BLOCK = 512

EXAMPLE = 'system/get_cp_fw_version'
SW_AGGR_CP = ['CONFIG_EH_TRANSPORT_CP_SDIO_MODE_SW_AGGR=y']
FAIL = FATAL_PATTERNS + [
    'bring-up timed out',
    'malformed SDIO buf-config',
    'SDIO mempool create failed',
    'failed to preallocate SDIO RX',
    'SDIO RX staging alloc failed',
    'mempool OOM',
    'dropping read',
]

_ANSI = re.compile(r'\x1b\[[0-9;]*m')
_PREALLOC = re.compile(
    r'preallocated (\d+) SDIO RX staging slot\(s\), (\d+) bytes each')
_BUFCFG = re.compile(
    r'SDIO buf-config: e2h mode=(\d+) bufsz=(\d+)B')
_MEMPOOL = re.compile(r'SDIO mempool: (\d+) blocks x (\d+) bytes')


def _block_round(n):
    return ((n + _BLOCK - 1) // _BLOCK) * _BLOCK


def _host_log(lab_tmp):
    """Host log from the last boot only: a hardware bench boots the host twice
    (port open, then reset), so the per-boot lines would otherwise repeat."""
    for name in ('host_sdio.log', 'host.log'):
        path = lab_tmp / name
        if path.is_file():
            log = _ANSI.sub('', path.read_text(errors='replace'))
            return log.rsplit('ESP-ROM:', 1)[-1]
    pytest.fail(f'no host log under {lab_tmp}')


def _only(log, pattern, what):
    found = pattern.findall(log)
    assert len(found) == 1, f'{what}: expected one match, found {found!r}'
    return found[0]


def _rpc_up(bench, overlay=None, cp_overlay=None):
    b = bench(EXAMPLE, 'mcu_host', 'sdio', overlay=overlay,
              cp_overlay=SW_AGGR_CP if cp_overlay is None else cp_overlay)
    r = eh_test_expect(b['host'], r'CP firmware: \d+\.\d+\.\d+', fail=FAIL, timeout=45)
    assert r.ok, f'RPC over the reserved buffers: {r.matched}'
    return b


def _assert_prealloc(log, slots, nbytes):
    got_slots, got_bytes = _only(log, _PREALLOC, 'RX staging prealloc')
    assert int(got_slots) == slots, f'staging slots: want {slots}, log says {got_slots}'
    assert int(got_bytes) == nbytes, (
        f'staging bytes: want {nbytes}, log says {got_bytes}')
    assert 'stay dynamic' not in log, 'staging stayed dynamic; prealloc did not run'


def _advertised_e2h(log):
    mode, bufsz = _only(log, _BUFCFG, 'SDIO buf-config')
    return int(mode), _block_round(int(bufsz))


@pytest.mark.system
@pytest.mark.sanity
def test_sdio_rx_staging_preallocated(bench, lab_tmp):
    """Default host (mempool off, 2 slots) reserves streaming RX buffers from
    the SW_AGGR aggregate the co-processor advertised, and does not reserve a
    packet mempool."""
    _rpc_up(bench)
    log = _host_log(lab_tmp)

    mode, nbytes = _advertised_e2h(log)
    assert mode == 0, f'SW_AGGR e2h mode: want 0, log says {mode}'
    _assert_prealloc(log, slots=2, nbytes=nbytes)
    assert 'SDIO mempool:' not in log, 'mempool reserved while the option is off'


@pytest.mark.system
def test_sdio_rx_staging_two_slots(bench, lab_tmp):
    """The default two-slot ring preallocates after INIT and sustains an E2H
    flood without a busy-slot drop or deferred-allocation failure."""
    fail = FAIL + ['rx_get_buffer', 'Rx slot busy, dropped',
                   'SDIO RX read', 'TRANSPORT_FAILURE']
    b = bench('system/api_exerciser', 'mcu_host', 'sdio', timeout='180s',
              cp_overlay=SW_AGGR_CP)
    host = b['host']

    r = eh_test_expect(host, r'EH api_exerciser ready', fail=fail, timeout=90)
    assert r.ok, f'bring-up with two staging slots: {r.matched}'

    log = _host_log(lab_tmp)
    _mode, nbytes = _advertised_e2h(log)
    _assert_prealloc(log, slots=2, nbytes=nbytes)

    host.write('raw_tp start rx')
    r = eh_test_expect(host, r'EH rc=0 cmd=raw_tp', fail=fail, timeout=15)
    assert r.ok, f'raw_tp start rx accepted: {r.matched}'
    r = eh_test_expect(host, r'sec Tx:\d+ Rx:[1-9]\d* Kbps', fail=fail, timeout=30)
    assert r.ok, f'E2H flood through two slots: {r.matched}'


@pytest.mark.system
def test_sdio_rx_staging_one_slot(bench, lab_tmp):
    """CONFIG_ESP_HOSTED_HOST_SDIO_RX_STAGING_SLOTS=1 preallocates one staging
    buffer. The read task waits until that buffer is copied out; an E2H flood
    still arrives, and a busy slot is not dropped."""
    one = ['CONFIG_ESP_HOSTED_HOST_SDIO_RX_STAGING_SLOTS=1']
    fail = FAIL + ['rx_get_buffer', 'Rx slot busy, dropped']
    b = bench('system/api_exerciser', 'mcu_host', 'sdio', timeout='180s',
              overlay=one, cp_overlay=SW_AGGR_CP)
    host = b['host']

    r = eh_test_expect(host, r'EH api_exerciser ready', fail=fail, timeout=90)
    assert r.ok, f'bring-up with one staging slot: {r.matched}'

    log = _host_log(lab_tmp)
    _mode, nbytes = _advertised_e2h(log)
    _assert_prealloc(log, slots=1, nbytes=nbytes)

    host.write('raw_tp start rx')
    r = eh_test_expect(host, r'EH rc=0 cmd=raw_tp', fail=fail, timeout=15)
    assert r.ok, f'raw_tp start rx accepted: {r.matched}'
    r = eh_test_expect(host, r'sec Tx:\d+ Rx:[1-9]\d* Kbps', fail=fail, timeout=30)
    assert r.ok, f'E2H flood through the single slot: {r.matched}'


@pytest.mark.system
def test_sdio_stream_staging_preallocated(bench, lab_tmp):
    """A STREAM CP advertises its send-queue depth, so each staging slot is
    one max frame times that depth, independent of the advertised e2h."""
    _rpc_up(bench, cp_overlay=['CONFIG_EH_TRANSPORT_CP_SDIO_MODE_STREAM=y'])
    log = _host_log(lab_tmp)

    mode, _e2h = _advertised_e2h(log)
    assert mode == 1, f'stream e2h mode: want 1, log says {mode}'
    assert 'CP without SDIO SW_AGGR' in log
    _assert_prealloc(log, slots=2, nbytes=_block_round(_MAX_FRAME * _STREAM_Q))


@pytest.mark.system
@pytest.mark.sanity
def test_sdio_mempool_enabled(bench, lab_tmp):
    """With the mempool on, transport start reserves (RX queue + 11) frames of
    1536 bytes. Staging slots are still preallocated beside that pool."""
    rx_q = 8
    _rpc_up(bench, overlay=[
        'CONFIG_ESP_HOSTED_HOST_SDIO_USE_MEMPOOL=y',
        f'CONFIG_ESP_HOSTED_HOST_SDIO_RX_Q_SIZE={rx_q}',
    ])
    log = _host_log(lab_tmp)

    blocks, block = _only(log, _MEMPOOL, 'packet mempool')
    assert int(blocks) == rx_q + _MEMPOOL_EXTRA, (
        f'mempool blocks: want {rx_q + _MEMPOOL_EXTRA}, log says {blocks}')
    assert int(block) == _MAX_FRAME, f'mempool block: want {_MAX_FRAME}, log says {block}'

    _mode, nbytes = _advertised_e2h(log)
    _assert_prealloc(log, slots=2, nbytes=nbytes)
