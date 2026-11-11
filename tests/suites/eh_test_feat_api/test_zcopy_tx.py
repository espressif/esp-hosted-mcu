# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""esp_hosted_tx() with buff_zerocopy=1 frees the buffer on return.

The host sends N numbered ESP_TEST_IF frames and zeroes each buffer in its free
callback. A bus that sends after the call returns would put seq 0 or freed heap
on the wire, and the CP raw_tp seq counter would not reach N with no gaps.
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

FAIL = FATAL_PATTERNS + ['bring-up timed out']
EXAMPLE = 'system/api_exerciser'
N = 200


@pytest.mark.system
@pytest.mark.parametrize('transport,cp_overlay', [
    pytest.param('sdio', [], id='sdio'),
    pytest.param('sdio', ['CONFIG_EH_TRANSPORT_CP_SDIO_MODE_PACKET=y'], id='sdio_packet'),
    pytest.param('sdio', ['CONFIG_EH_TRANSPORT_CP_SDIO_MODE_STREAM=y'], id='sdio_stream'),
    pytest.param('spi_fd', [], id='spi_fd'),
    pytest.param('spi_hd', [], id='spi_hd'),
    pytest.param('uart', [], id='uart'),
])
def test_zcopy_tx_early_free(bench, transport, cp_overlay):
    b = bench(EXAMPLE, 'mcu_host', transport, timeout='180s', cp_overlay=cp_overlay)
    host, cp = b['host'], b['cp']

    r = eh_test_expect(host, r'EH api_exerciser ready', fail=FAIL, timeout=60)
    assert r.ok, f'[{transport}] ready: {r.matched}'

    # Starts the CP raw_tp report timer, which logs the RX seq counter.
    host.write('raw_tp start rx')
    r = eh_test_expect(host, r'EH rc=0 cmd=raw_tp', fail=FAIL, timeout=15)
    assert r.ok, f'[{transport}] raw_tp start rx: {r.matched}'

    host.write(f'raw_tp_zcopy {N}')
    r = eh_test_expect(host, rf'EH raw_tp_zcopy sent={N} ok={N}', fail=FAIL, timeout=30)
    assert r.ok, f'[{transport}] all frames accepted: {r.matched}'

    r = eh_test_expect(cp, rf'raw_tp seq: rx frames={N} lost=0 restarts=0',
                       fail=FATAL_PATTERNS, timeout=35)
    assert r.ok, f'[{transport}] CP got every frame intact: {r.matched}'

    host.write('raw_tp stop')
    r = eh_test_expect(host, r'EH rc=0 cmd=raw_tp', fail=FAIL, timeout=15)
    assert r.ok, f'[{transport}] raw_tp stop: {r.matched}'
