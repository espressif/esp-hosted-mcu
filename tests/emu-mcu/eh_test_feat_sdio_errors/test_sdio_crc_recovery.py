# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""SDIO host behaviour when CMD53 reads fail with a data CRC error (0x109).

The emu injects DCRC on short CMD53 reads: by default 4-byte reads only (the TX
credit read, TOKEN_RDATA); with max_len 32 also the interrupt / combined register
read. Field reference: https://github.com/espressif/esp-hosted-mcu/issues/167 (P4 + C6).

ESP_EMU_SDMMC_CRC_BURST=<errs>,<burst_ms>,<gap_ms>[,<delay_ms>[,<max_len>[,<min_len>]]]:
after delay_ms, every burst_ms + gap_ms of wall time, up to errs reads of min_len to
max_len bytes in the first burst_ms fail. Needs an esp-emu with this hook.

Manual: run by naming this file, e.g.
  python3 tools/eh.py test emu-mcu emu-mcu/eh_test_feat_sdio_errors/test_sdio_crc_recovery.py
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

pytestmark = [pytest.mark.manual(reason='needs esp-emu SDMMC CRC injection'),
              pytest.mark.negative]

CRC_ENV = 'ESP_EMU_SDMMC_CRC_BURST'
RESET = r'TRANSPORT_FAILURE: forcing host reset'
PASS = r'Result:\s+\S+ PASS'
FAIL = r'Result:\s+\S+ FAIL'


def _peer_data(bench, monkeypatch, crc, timeout='240s'):
    monkeypatch.setenv(CRC_ENV, crc)
    return bench('peer_data_transfer', 'mcu_host', 'sdio', timeout=timeout)['host']


@pytest.mark.system
def test_sdio_crc_bursts_do_not_stall_tx(bench, monkeypatch):
    """Field-like bursts on the credit read (14 errors in 134 ms, every 1.3 s):
    every peer-data round trip completes."""
    host = _peer_data(bench, monkeypatch, '14,134,1300,3000')
    r = eh_test_expect(host, PASS, fail=FATAL_PATTERNS + [RESET, FAIL], timeout=200)
    assert r.ok, f'peer-data under credit-read CRC bursts: {r.matched}'


@pytest.mark.system
def test_sdio_crc_on_interrupt_read_does_not_reset_host(bench, monkeypatch):
    """Bursts of 2 errors on every register read up to 32 B (credit, interrupt,
    combined): the re-read in the port absorbs them. No reset."""
    host = _peer_data(bench, monkeypatch, '2,200,500,3000,32')
    r = eh_test_expect(host, PASS, fail=FATAL_PATTERNS + [RESET, FAIL], timeout=200)
    assert r.ok, f'peer-data under register-read CRC bursts: {r.matched}'


@pytest.mark.system
def test_sdio_credit_crc_shorter_than_window_recovers(bench, monkeypatch):
    """Every credit read fails for 1.5 s of wall time (shorter than one credit
    wait in firmware time), then the fault clears: no reset, the run completes."""
    host = _peer_data(bench, monkeypatch, '1000000,1500,600000,3000')
    r = eh_test_expect(host, PASS, fail=FATAL_PATTERNS + [RESET, FAIL], timeout=200)
    assert r.ok, f'credit-read CRC for 1.5 s: {r.matched}'


@pytest.mark.system
def test_sdio_persistent_credit_crc_reports_transport_failure(bench, monkeypatch):
    """Every credit read fails from 3 s on, interrupt reads still pass: a whole
    credit wait (~0.5 s) with no good read reports a transport failure."""
    host = _peer_data(bench, monkeypatch, '1000000,600000,1,3000', timeout='180s')
    host.crash_expected = True
    r = eh_test_expect(host, r'TX credit read keeps failing', fail=FATAL_PATTERNS, timeout=60)
    assert r.ok, f'TX path reports persistent credit-read CRC: {r.matched}'
    r = eh_test_expect(host, RESET, fail=FATAL_PATTERNS, timeout=10)
    assert r.ok, f'transport failure reported: {r.matched}'


@pytest.mark.system
def test_sdio_persistent_register_crc_reports_transport_failure(bench, monkeypatch):
    """Every register read up to 32 B fails from 3 s on: the host reports a
    transport failure (TX or RX path, whichever limit is reached first)."""
    host = _peer_data(bench, monkeypatch, '1000000,600000,1,3000,32', timeout='180s')
    host.crash_expected = True
    r = eh_test_expect(host, RESET, fail=FATAL_PATTERNS, timeout=60)
    assert r.ok, f'transport failure reported on persistent register-read CRC: {r.matched}'



@pytest.mark.system
def test_sdio_data_read_crc_reports_transport_failure(bench, monkeypatch):
    """One data read (64 B or more) fails CRC: the host keeps its RX count, reports
    a transport failure and resets. On HW (P4 + C6 or C5, CRC forced by the slave's
    NSEND timing) the slave still holds the bytes and re-reads do not clear the bus;
    the emu checks only the host reaction."""
    host = _peer_data(bench, monkeypatch, '1,1000,600000,3000,65536,64', timeout='180s')
    host.crash_expected = True
    r = eh_test_expect(host, RESET, fail=FATAL_PATTERNS, timeout=60)
    assert r.ok, f'transport failure reported on a data-read CRC: {r.matched}'
