"""SPI half-duplex transport validation: P4 host <--SPI-HD--> C6 CP.

Ground-truth check for the SPI-HD path. Reaching the api_exerciser 'ready' line
means the half-duplex transport worked end-to-end:
  - the host reads SLAVE_READY (0xEE) over RDBUF, opens the datapath (WRBUF
    DATAPATH_ON), then the CP asserts data-ready and the host drains the CP's
    startup event + caps over RDDMA,
  - the host clocks its own caps out over WRDMA,
  - RPC is negotiated.
A control-plane RPC round-trip then proves data flows both ways at runtime.

Parametrized over the data-line count: plain `spi_hd` (Kconfig default = 4-line),
plus explicit 1/2/4-line builds. Each is a distinct FW build (distinct CONFIG
overlay -> distinct build-cache key) and its own emu launch.

Runs on the emu substrate (`eh.py test emu`) and on real HW (`eh.py test hw`)
when the bench declares "spi_hd" (or spi_hd_1/2/4) in tests/lab.local.json. On a
substrate that cannot provide the wire, the bench factory SKIPs; on the emu, if
the resolved esp-emu lacks --hosted-spi-hd it SKIPs too.
"""
import os
import re
import sys
import time

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

FAIL = FATAL_PATTERNS + ['bring-up timed out']
EXAMPLE = 'system/api_exerciser'

# Plain 'spi_hd' == Kconfig default (4-line); the numbered variants pin the
# data-line count explicitly on both CP and host.
LINE_MODES = ['spi_hd', 'spi_hd_1', 'spi_hd_2', 'spi_hd_4']


@pytest.mark.system
@pytest.mark.retired("Subsumed by test_spi_hd_rpc_roundtrip below: its first "
                     "assertion is the same 'ready' bring-up, then an RPC.")
@pytest.mark.parametrize('transport', LINE_MODES)
def test_spi_hd_bringup(bench, transport):
    """P4<->C6 SPI-HD reaches api_exerciser 'ready' — half-duplex transport up."""
    b = bench(EXAMPLE, 'mcu_host', transport, timeout='150s')
    r = eh_test_expect(b['host'], r'EH api_exerciser ready', fail=FAIL, timeout=120)
    assert r.ok, f'[{transport}] SPI-HD bring-up to ready: {r.matched}'


# 'spi_hd' (Kconfig default) == explicit 'spi_hd_4'; each numbered config is
# unique here: the emu trace proves the bus runs in that line mode.
_HD_RPC_MODES = [
    pytest.param('spi_hd',   marks=pytest.mark.retired(
        "4-line default == spi_hd_4 below.")),
    'spi_hd_1',
    'spi_hd_2',
    'spi_hd_4',
]

# High nibble of the SPI-HD command byte = its line mode
# (IDF spi_ll_get_slave_hd_command): 0x0_ 1-line, 0x5_ DIO, 0xA_ QIO.
_CMD_MODE = {'1': '0', '2': '5', '4': 'A'}
_EMU_SPI_TRACE = 'info,esp_emu::periph::esp32p4::gpspi_master=trace'


@pytest.mark.system
@pytest.mark.parametrize('transport', _HD_RPC_MODES)
def test_spi_hd_rpc_roundtrip(bench, transport, substrate, lab_tmp, monkeypatch):
    """A control-plane RPC (sys_fw_version) round-trips over SPI-HD: the request
    leaves the host (WRDMA) and the CP's response returns (RDDMA). The host
    must settle on this build's line count once, and keep using it."""
    lines = transport.rsplit('_', 1)[1] if transport != 'spi_hd' else '4'
    emu = substrate.startswith('emu')
    if emu:
        monkeypatch.setenv('RUST_LOG', _EMU_SPI_TRACE)
    b = bench(EXAMPLE, 'mcu_host', transport, timeout='150s')
    host = b['host']
    # Only a change is logged: 1-line never switches; 2/4-line switch once.
    # Any other count, or a second switch, is a wrong or stale value.
    if lines != '1':
        r = eh_test_expect(host, rf'SPI-HD data lines: {lines} ',
                           fail=FAIL + [rf'SPI-HD data lines: (?!{lines} )\d'],
                           timeout=120)
        assert r.ok, f'[{transport}] data-line switch to {lines}: {r.matched}'
    settled = FAIL + [r'SPI-HD data lines: \d']
    r = eh_test_expect(host, r'EH api_exerciser ready', fail=settled, timeout=120)
    assert r.ok, f'[{transport}] ready: {r.matched}'
    host.write('sys_fw_version')
    r = eh_test_expect(host, r'EH rc=0 cmd=sys_fw_version ver=\d+\.\d+\.\d+',
                       fail=settled, timeout=20)
    assert r.ok, f'[{transport}] sys_fw_version round-trip: {r.matched}'

    if emu:
        # The RPC just ran: the latest transfers must all be in this line mode.
        time.sleep(1)
        log = (lab_tmp / f'host_{transport}.log').read_text(errors='replace')
        cmds = re.findall(r'GPSPI2 master: xfer .*?cmd=0x([0-9A-F]{2})', log)[-10:]
        assert cmds, f'[{transport}] no SPI master trace captured'
        bad = [c for c in cmds if c[0] != _CMD_MODE[lines]]
        assert not bad, (f'[{transport}] transfers not in {lines}-line mode: '
                         f'cmd bytes {cmds}')
