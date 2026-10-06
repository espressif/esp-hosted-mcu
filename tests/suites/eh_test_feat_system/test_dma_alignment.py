"""Host DMA buffers in PSRAM, across L2 cache line sizes (ESP32-P4).

A PSRAM buffer is reached through the L2 cache, so a DMA transfer's address and
its length must both be whole cache lines, and the driver must be told the buffer
is external. Two separate failures follow from getting either wrong, and both
were reproduced on the emulator:

  no SPI_TRANS_DMA_USE_PSRAM   -> "TX addr&len not align to 1, or not dma_capable"
                                  at EVERY line size, including the P4 default 64
  transfer length 1600         -> "RX addr&len not align to 128"
                                  at 128 B lines only (1600 % 128 == 64)

Both end in a load-access-fault panic. spi_common.c:483 checks the TRANSACTION
length, which is why rounding the allocation up does not cover it.

These run under the emu PSRAM write-back cache model (ESP_EMU_PSRAM_CACHE), where
the CPU sees cached lines and DMA sees memory, so a buffer sharing a line with
other live data loses that data on invalidate instead of silently working.
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

FAIL = FATAL_PATTERNS + [
    'bring-up timed out',
    'addr&len not align',     # spi_common.c:488
    'cannot allocate',
]

PSRAM = ['CONFIG_SPIRAM=y', 'CONFIG_EH_HOST_PORT_DMA_PREFER_SPIRAM=y']
# 64 B is the P4 default; CACHE_L2_CACHE_LINE_128B has no depends, so it is
# selectable at the default 128 KB L2 without also moving the cache size.
LINE_OVL = {64: [], 128: ['CONFIG_CACHE_L2_CACHE_LINE_128B=y']}


@pytest.mark.system
@pytest.mark.parametrize('transport', ['sdio', 'spi_fd', 'spi_hd', 'uart'])
@pytest.mark.parametrize('line', [
    64,
    pytest.param(128, marks=pytest.mark.sanity),
])
def test_psram_dma_cache_line(bench, transport, line, monkeypatch, lab_tmp, substrate):
    """Link comes up and RPCs round-trip with host DMA buffers in cached PSRAM."""
    monkeypatch.setenv('ESP_EMU_PSRAM_CACHE', 'on')
    b = bench('system/api_exerciser', 'mcu_host', transport, timeout='240s',
              overlay=PSRAM + LINE_OVL[line])
    host = b['host']

    r = eh_test_expect(host, r'EH api_exerciser ready', fail=FAIL, timeout=150)
    assert r.ok, f'bring-up at {line} B lines: {r.matched}'

    for i in range(5):
        host.write('sys_fw_version')
        r = eh_test_expect(host, r'EH rc=0 cmd=sys_fw_version ver=\d+\.\d+\.\d+',
                           fail=FAIL, timeout=30)
        assert r.ok, f'RPC {i} at {line} B lines: {r.matched}'

    if substrate == 'emu':
        # The model announces itself on the emu's own log channel, which
        # infra/emu_dut.py keeps out of the expect buffer. Read the file.
        # On HW the cache is real and there is nothing to confirm.
        txt = (lab_tmp / f'host_{transport}.log').read_text(errors='replace')
        want = f'PSRAM cache model: {line} B lines'
        assert want in txt, f'emu did not model {line} B lines (ESP_EMU_PSRAM_CACHE)'


@pytest.mark.system
@pytest.mark.parametrize('transport', ['spi_fd', 'spi_hd', 'uart'])
def test_transfer_size_tlv_applied(bench, transport):
    """0x49 (SLV_CONFIG_SET_TRANSFER_SIZE) reaches the CP and is accepted, so the
    CP stops queueing bytes a 1536-byte host will never clock. SDIO is excluded:
    it is 1536 on both sides already and installs no setter.

    A CP that predates the tag ignores it and stays at its own default; that
    pairing is checked by hand (the builder has no cross-revision support)."""
    b = bench('system/api_exerciser', 'mcu_host', transport, timeout='240s')
    cp = b['cp']

    r = eh_test_expect(cp, r'Transfer size set to 1536', fail=FAIL, timeout=150)
    assert r.ok, f'CP applied the host transport size: {r.matched}'
