# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Concurrent first eh_host_connect_to_slave() in manual bring-up.

The super host's init_race scenario calls eh_host_init() once, then releases two
tasks on different cores into eh_host_connect_to_slave() together, with no lock
pre-created by a constructor. Exactly one bring-up may run: one TRANSPORT_UP, both
callers return 0, and the host resets the CP once."""

import os
import re
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

FAIL = FATAL_PATTERNS + ['bring-up timed out', 'hosted start failed']
HOST_RESET = r'eh_host_port_reset: reset done'


@pytest.mark.system
@pytest.mark.parametrize('transport', ['sdio', 'uart', 'spi_fd', 'spi_hd'])
def test_concurrent_first_connect_runs_one_bringup(emu_bench, transport):
    b = emu_bench('test/init_race', 'mcu_host', transport, timeout='120s')
    host = b['host']
    r = eh_test_expect(host, r'init_race: done transport_up=\d+', fail=FAIL, timeout=90)
    assert r.ok, f'[{transport}] race did not finish: {r.matched}'
    log = getattr(host, '_buf', '')
    up = int(re.findall(r'init_race: done transport_up=(\d+)', log)[-1])
    rcs = sorted(re.findall(r'init_race: connect\d rc=(-?\d+)', log))
    resets = len(re.findall(HOST_RESET, log))
    assert rcs == ['0', '0'], f'[{transport}] connect return codes {rcs}'
    assert up == 1, f'[{transport}] {up} bring-ups for one transport (expected 1)'
    assert resets == 1, f'[{transport}] host reset the CP {resets} times (expected 1)'
