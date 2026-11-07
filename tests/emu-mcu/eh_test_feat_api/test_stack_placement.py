# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Transport task stacks stay out of LP RAM, whatever the app's static size.

GH esp-hosted-mcu#256: with auto-init started from boot, a large app .bss pushed the SDIO
task stacks into LP RAM (APB clock) and cut throughput 2-6x. The host app pads
.bss by EH_TEST_BSS_PAD_BYTES and reports each stack's region."""

import os
import re
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

APP = '../tests/fw/host_stack_place'
# Reporter's config: SDIO DMA buffers in PSRAM, so only task stacks compete
# for the internal heap left at boot.
PSRAM = ['CONFIG_SPIRAM=y', 'CONFIG_EH_HOST_PORT_DMA_PREFER_SPIRAM=y',
         'CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=0']
TASKS = ('sdio_read', 'sdio_write', 'sdio_process_rx', 'sdio_rx_buf')


@pytest.mark.system
@pytest.mark.parametrize('pad', [475136, 483328, 491520, 499712, 507904])
def test_sdio_stacks_not_in_lp_ram(emu_bench, pad):
    b = emu_bench(APP, 'mcu_host', 'sdio', timeout='60s',
                  extra_ovl=PSRAM + [f'CONFIG_EH_TEST_BSS_PAD_BYTES={pad}'])
    host = b['host']
    r = eh_test_expect(host, r'stack_place: done', fail=FATAL_PATTERNS, timeout=50)
    assert r.ok, f'[pad={pad}] no report: {r.matched}'
    log = getattr(host, '_buf', '')
    got = dict(re.findall(r'stack_place: task (\w+) stack=\S+ region=(\w+)', log))
    assert set(got) == set(TASKS), f'[pad={pad}] tasks reported: {got}'
    bad = {t: reg for t, reg in got.items() if reg == 'lp_ram'}
    assert not bad, f'[pad={pad}] stacks in LP RAM: {bad} (all: {got})'
