"""RF certification test — console contract over the hosted link.

The emulator stubs the PHY registers (esp-emu src/periph/wifi_phy.rs), so these
rows prove the plumbing — dispatch, compose, parse, lifecycle and the result
line — not the RF. Real transmit belongs on a bench with an instrument.

The lifecycle row is a regression guard: Req_FeatureControl compose/decode is
gated on EH_HOST_FEAT_FEATURE_CONTROL_READY, and an RF-cert-only build once
fell outside that gate, which silently broke init, deinit and status.

Commands use the phy_cert_ names, which is all the console carries unless
EH_HOST_FEAT_RF_CERT_CLI_IDF_NAMES adds cert_test's own names too.
"""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

FAIL = FATAL_PATTERNS + ['bring-up timed out']


@pytest.mark.system
@pytest.mark.sanity
def test_rf_cert_console(bench):
    b = bench('rf_cert', 'mcu_host', 'sdio', timeout='240s')
    host = b['host']

    assert eh_test_expect(host, r'RF certification test', fail=FAIL,
                          timeout=90).ok, 'banner'

    # lifecycle rides Req_FeatureControl — the gate that once excluded it
    host.write('phy_cert_query')
    assert eh_test_expect(host, r'cert support: yes', fail=FAIL,
                          timeout=30).ok, 'coprocessor reports no cert support'
    assert eh_test_expect(host, r'cert mode entered: yes', fail=FAIL,
                          timeout=30).ok, 'start-up init did not enter cert mode'

    host.write('phy_cert_esp_tx -n 1 -r 0 -l 100 -d 1000 -c 5')
    assert not eh_test_expect(host, r'rf_cert_rpc', timeout=15).ok, 'phy_cert_esp_tx'

    # a second start without a stop must be refused, or blocked PHY tasks pile up
    host.write('phy_cert_esp_rx -n 1 -r 0')
    assert eh_test_expect(host, r'rf_cert_rpc', fail=FAIL,
                          timeout=15).ok, 'second start was not refused'

    host.write('phy_cert_cmdstop')
    assert not eh_test_expect(host, r'rf_cert_rpc', timeout=15).ok, 'phy_cert_cmdstop'

    # cert_test's own result line, verbatim
    host.write('phy_cert_get_rx_result')
    assert eh_test_expect(host, r'Correct: \d+, Desired: \d+, RSSI:', fail=FAIL,
                          timeout=30).ok, 'get_rx_result line'

    # and a start is allowed again once a stop has been issued
    host.write('phy_cert_esp_ble_tx -n 0 -l 37 -t 2 -m 5')
    assert not eh_test_expect(host, r'rf_cert_rpc',
                              timeout=15).ok, 'start refused after cmdstop'
