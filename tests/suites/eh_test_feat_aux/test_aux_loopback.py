# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""ESP_AUX_IF loopback over every emulated transport.

The CP echoes ESP_AUX_OT frames in place of the OpenThread RCP
(tests/fw/cp_aux_loopback); the host sends frames of several sizes plus an
in-order burst and checks each echo byte for byte (tests/fw/host_aux_loopback).
The CP also sends one frame with an aux_type the host has no channel for, and
the host sends one with an aux_type the CP has no handler for. Each peer must
drop and log it and keep the link working.

Covers: aux_type carried in the header byte on every transport encoder and
decoder (SDIO uses its own), the CP aux table and dispatch, the host aux
channel and dispatch, and the ESP_EXT_CAP_AUX_IF capability bit.
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

_FW = os.path.join(os.path.dirname(__file__), '..', '..', 'fw')


def _inject(name, files):
    return {f'components/{name}/{f}': os.path.abspath(os.path.join(_FW, name, f))
            for f in files}


CP_INJECT = _inject('cp_aux_loopback',
                    ('cp_aux_loopback.c', 'CMakeLists.txt', 'Kconfig.projbuild'))
HOST_INJECT = _inject('host_aux_loopback',
                      ('host_aux_loopback.c', 'CMakeLists.txt', 'Kconfig.projbuild'))

EXAMPLE = 'system/get_cp_fw_version'

FAIL = FATAL_PATTERNS + [r'\[aux-loop\] FAIL']


@pytest.mark.system
@pytest.mark.parametrize('transport', [
    pytest.param('sdio', marks=pytest.mark.sanity),
    'spi_fd', 'spi_hd', 'uart'])
def test_aux_loopback(bench, transport):
    b = bench(EXAMPLE, 'mcu_host', transport, timeout='180s',
              cp_overlay=['CONFIG_EH_TEST_CP_AUX_LOOPBACK=y'],
              overlay=['CONFIG_EH_TEST_HOST_AUX_LOOPBACK=y'],
              cp_inject=CP_INJECT, host_inject=HOST_INJECT)
    host, cp = b['host'], b['cp']

    r = eh_test_expect(cp, r'\[aux-loop\] ready', fail=FAIL, timeout=60)
    assert r.ok, f'[{transport}] CP echo registered: {r.matched}'

    # Match the end of the line only: the emulator can print into the middle
    # of a console line.
    r = eh_test_expect(cp, r'no handler for aux_type=85 ', fail=FAIL, timeout=60)
    assert r.ok, f'[{transport}] CP drops an aux_type it has no handler for: {r.matched}'

    r = eh_test_expect(host, r'no channel for aux_type=85 ', fail=FAIL, timeout=60)
    assert r.ok, f'[{transport}] host drops an aux_type it has no channel for: {r.matched}'

    r = eh_test_expect(host, r'\[aux-loop\] all echoes intact', fail=FAIL, timeout=90)
    assert r.ok, f'[{transport}] every size and the burst echo intact, in order: {r.matched}'
