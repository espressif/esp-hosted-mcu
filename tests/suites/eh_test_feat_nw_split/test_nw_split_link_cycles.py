"""Network-split link drop/reconnect cycles — host IP events, counted exactly.

The CP re-associates and the AP returns the SAME lease (esp-emu always hands out
192.168.4.2). The host must report that lease once per association:

  per cycle:  sta disconnected → sta connected ×1 → got ip ×1 → ping OK
              no second got ip, and no lost ip while the drop is shorter than
              CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL (IDF parity)

A drop with no reconnect must post lost ip exactly once, even though STA_STOP,
STA_DISCONNECTED and the CP's DOWN status all signal the same link-down.

The CP and the host share one MAC, so exactly one DHCP client may run:
  CP-owned modes (app_external, internal_static): the host client never binds.
  host-owned mode (internal_dhcp + CP DHCP owner HOST): the host pushes its lease
  to the CP, so the CP answers ARP and the host is reachable from outside.

Counts come from the full host log (EmuDut._buf), not from expect(): expect()
stops at the first match, so a duplicate event would pass unseen.
"""
import os
import re
import sys
import time

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))  # tests/
from infra.expect_helper import eh_test_expect, FATAL_PATTERNS

FAIL = FATAL_PATTERNS + ['bring-up timed out', 'connect to the AP fail']

IPF = 'network_split/iperf'
GW = '192.168.4.1'
CYCLES = 5
LOST_IP_S = 10                   # host CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL
T_CONSOLE, T_IP, T_NOIP, T_PING = 30, 45, 8, 20

TAG = 'example_nw_split_iperf: '
GOT, LOST = TAG + 'got ip:', TAG + 'lost ip'
CONN, DISC = TAG + 'sta connected', TAG + 'sta disconnected'

CP_AUTO_ON = 'CONFIG_ESP_HOSTED_CP_FEAT_NW_SPLIT_WIFI_AUTO_CONNECT_ON_STA_START=y'
CP_AUTO_OFF = 'CONFIG_ESP_HOSTED_CP_FEAT_NW_SPLIT_WIFI_AUTO_CONNECT_ON_STA_START=n'
HOST_AUTO_OFF = 'CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_CONNECT_ON_START=n'
HOST_RECONNECT_ON = 'CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_RECONNECT_ON_DISCONNECT=y'
LOST_TIMER = f'CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL={LOST_IP_S}'

# Host netif mode -> (host overlay, CP overlay). Each pair is a supported one,
# pinned explicitly so no expectation rests on a Kconfig default:
#   app_external / internal_static: the CP runs DHCP and reports the lease.
#   internal_dhcp: the host runs DHCP and pushes the lease to the CP.
CP_DHCP_BOTH = 'CONFIG_ESP_HOSTED_DEFAULT_NETWORK_STACK_DHCP_BOTH=y'
CP_DHCP_HOST = 'CONFIG_ESP_HOSTED_DEFAULT_NETWORK_STACK_DHCP_HOST=y'
HOST_DHCP_DEBUG = ['CONFIG_LWIP_DEBUG=y', 'CONFIG_LWIP_DHCP_DEBUG=y']
# lwIP dhcp.c trace lines: the client sent a REQUEST, or bound a lease.
DHCP_REQUESTED, DHCP_BOUND = 'dhcp_select: REQUESTING', 'dhcp_bind(netif='
ECHO_PORT = 49999                # host range -> host stack
ECHO = ['CONFIG_EXAMPLE_NW_SPLIT_ECHO_SERVER=y', f'CONFIG_EXAMPLE_NW_SPLIT_ECHO_PORT={ECHO_PORT}']
LEASE_REJECTED = 'CP rejected the host lease'
MODES = {
    'app_external':    (['CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_APP_EXTERNAL=y',
                         *HOST_DHCP_DEBUG],
                        [CP_DHCP_BOTH]),
    'internal_static': (['CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_STATIC=y',
                         'CONFIG_WIFI_CMD_NO_LWIP=y'],
                        [CP_DHCP_BOTH]),
    'internal_dhcp':   (['CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_DHCP=y',
                         'CONFIG_WIFI_CMD_NO_LWIP=y'],
                        [CP_DHCP_HOST]),
}

# Who drops the link and who brings it back.
#   cli        host sta_disconnect, then host sta_connect
#   feat_auto  host sta_disconnect, host nw_split feature reconnects by itself
#   stop_start host wifi stop / wifi start, CP auto-connects on STA_START
# The CP example app retries the connect on every disconnect by default, which
# would add a second reconnect owner to cli and feat_auto.
CP_APP_NO_RETRY = 'CONFIG_EH_EXAMPLE_WIFI_MAXIMUM_RETRY=0'
TRIGGERS = {
    'cli':        dict(host=[HOST_AUTO_OFF], cp=[CP_AUTO_OFF, CP_APP_NO_RETRY],
                       cli_connect=True),
    'feat_auto':  dict(host=[HOST_AUTO_OFF, HOST_RECONNECT_ON],
                       cp=[CP_AUTO_OFF, CP_APP_NO_RETRY], cli_connect=True),
    'stop_start': dict(host=[HOST_AUTO_OFF], cp=[CP_AUTO_ON], cli_connect=False),
}


def _creds(ap):
    return [f'CONFIG_EH_EXAMPLE_WIFI_SSID="{ap["ssid"]}"',
            f'CONFIG_EH_EXAMPLE_WIFI_PASSWORD="{ap["password"]}"']


def _mark(dut):
    with dut._lock:
        return len(dut._buf)


def _since(dut, mark):
    with dut._lock:
        return dut._buf[mark:]


def _ping(host, ok=True):
    host.write(f'ping {GW} -c 4 -i 0.5')
    want = r'\d+ packets transmitted, [1-9]\d* received' if ok \
        else r'\d+ packets transmitted, 0 received'
    return eh_test_expect(host, want, fail=FAIL, timeout=T_PING)


def _connect(host, ap):
    host.write(f'sta_connect {ap["ssid"]} {ap["password"]}')


def _drop(host, trigger):
    if trigger == 'stop_start':
        host.write('wifi stop')
    else:
        host.write('sta_disconnect')


def _restore(host, trigger, ap):
    if trigger == 'stop_start':
        host.write('wifi start')
    elif trigger == 'cli':
        _connect(host, ap)
    # feat_auto: the host feature reconnects on STA_DISCONNECTED by itself.


def _bring_up(bench, wifi_ap, transport, mode, trigger, extra_host=(), cp_ovl=None,
              want_net=False):
    t = TRIGGERS[trigger]
    host_ovl, mode_cp = MODES[mode]
    cp = [*(mode_cp if cp_ovl is None else cp_ovl), *t['cp'],
          *(_creds(wifi_ap) if CP_AUTO_ON in t['cp'] else [])]
    extra = dict(hostfwd=(ECHO_PORT,)) if want_net else {}
    b = bench(IPF, 'mcu_host', transport, timeout='900s', wake=True,
              overlay=[*host_ovl, *t['host'], LOST_TIMER, *(ECHO if want_net else ()),
                       *extra_host],
              cp_overlay=cp, **extra)
    host = b['host']
    host.cp = b['cp']
    if want_net:
        host.net = b['net']
    if not hasattr(host, '_buf'):
        pytest.skip('event counting needs the emu DUT log buffer')
    r = eh_test_expect(host, r'network-split-iperf>', fail=FAIL, timeout=T_CONSOLE)
    assert r.ok, f'[{transport}/{mode}] console ready: {r.matched}'
    if t['cli_connect']:
        _connect(host, wifi_ap)
    # Whole log: a CP-owned connect can land before the console prompt.
    return host, 0


class Ledger:
    """Per-phase event counts. A failed phase is recorded and the run goes on, so
    one run yields the whole table instead of stopping at the first miss."""

    def __init__(self, label):
        self.label, self.rows, self.bad = label, [], []

    def phase(self, host, mark, name, got=1, conn=1, lost=0, disc=None, ping=True):
        """got=None: one got ip per sta connected (boot may associate twice)."""
        r = eh_test_expect(host, GOT, fail=FAIL, timeout=T_IP) if got != 0 else None
        if r is not None and r.ok:
            eh_test_expect(host, GOT, fail=[], timeout=T_NOIP)   # room for a duplicate
        seg = _since(host, mark)
        have = dict(got=seg.count(GOT), conn=seg.count(CONN), lost=seg.count(LOST),
                    disc=seg.count(DISC))
        p_ok = None
        if ping is not None:
            p_ok = _ping(host, ok=ping).ok
        want = dict(got=got, conn=conn, lost=lost, disc=disc)
        if got is None:
            want['got'] = max(have['conn'], 1)
            want['conn'] = None
        miss = [f'{k} {have[k]}/{want[k]}' for k in want
                if want[k] is not None and have[k] != want[k]]
        if p_ok is False:
            miss.append('ping ' + ('fail' if ping else 'replied while down'))
        self.rows.append((name, have, p_ok, miss))
        if miss:
            self.bad.append(f'{name}: ' + ', '.join(miss))

    def verdict(self):
        lines = [f'{self.label}  phase: got/conn/lost/disc ping']
        for name, h, p, miss in self.rows:
            lines.append(f'  {name:<14} {h["got"]}/{h["conn"]}/{h["lost"]}/{h["disc"]} '
                         f'{"-" if p is None else ("ok" if p else "FAIL")}'
                         f'{"   <-- " + "; ".join(miss) if miss else ""}')
        print('\n'.join(lines))
        assert not self.bad, '\n'.join(lines)


def _check_whole_run(led, host, mode):
    """Invariants over the whole host log, not per phase."""
    log = _since(host, 0)
    if mode == 'app_external':
        # The CP owns DHCP: the host client must never REQUEST or bind, or its stop
        # would RELEASE the lease the CP holds. esp_netif must not see a cleared
        # static address either.
        for bad in (DHCP_REQUESTED, DHCP_BOUND, 'invalid static ip'):
            if bad in log:
                led.bad.append(f'host log has "{bad}"')
    if mode == 'internal_dhcp':
        cp = _since(host.cp, 0)
        # The CP runs no DHCP client here, so its only address is the pushed lease.
        if 'esp_netif_handlers: sta ip:' not in cp:
            led.bad.append('CP never took the host lease')
        if LEASE_REJECTED in log:
            led.bad.append('CP rejected the host lease')


def _check_inbound(led, host):
    """An outside peer reaches a host-range port: the CP answers ARP for the
    shared address and routes the port to the host."""
    if f'echo server listening on host port {ECHO_PORT}' not in _since(host, 0):
        led.bad.append('host echo server never started')
        return
    probe = b'nw-split-inbound-probe'
    resp = host.net.request(ECHO_PORT, probe)
    if probe not in resp:
        led.bad.append(f'inbound :{ECHO_PORT} echo failed, got {resp!r}')


# ── Positive: repeated drop + same-lease reconnect ──────────────────────────
@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('transport,mode,trigger', [
    *[('sdio', m, t) for m in ('app_external', 'internal_static') for t in TRIGGERS],
    ('sdio', 'internal_dhcp', 'cli'),
    *[(tr, 'internal_static', 'cli') for tr in ('uart', 'spi_hd', 'spi_fd')],
])
def test_nwsplit_link_cycles(bench, wifi_ap, transport, mode, trigger):
    if not wifi_ap:
        pytest.skip(f'[{transport}] bench has no associable AP')
    led = Ledger(f'[{transport}/{mode}/{trigger}]')
    # Inbound probe on sdio only: the TCP echo through esp-emu user-net is a known
    # flake on spi_fd (see test_nw_split.py).
    inbound = transport == 'sdio'
    host, mark = _bring_up(bench, wifi_ap, transport, mode, trigger, want_net=inbound)
    led.phase(host, mark, 'initial', got=None, conn=None)

    cycles = 1 if mode == 'internal_dhcp' else CYCLES
    for c in range(1, cycles + 1):
        mark = _mark(host)
        _drop(host, trigger)
        eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
        _restore(host, trigger, wifi_ap)
        led.phase(host, mark, f'cycle {c}/{cycles}')
    if inbound:
        _check_inbound(led, host)
    _check_whole_run(led, host, mode)
    led.verdict()


# ── Negative: host DHCP with a CP that runs its own client — must fail loud ──
@pytest.mark.nw_split
@pytest.mark.wifi
def test_nwsplit_internal_dhcp_needs_cp_dhcp_host(bench, wifi_ap):
    """INTERNAL_DHCP with the CP DHCP owner left at BOTH puts two DHCP clients
    behind one MAC. The CP must refuse the host lease, and the host must say which
    CP option is wrong."""
    if not wifi_ap:
        pytest.skip('bench has no associable AP')
    host, _ = _bring_up(bench, wifi_ap, 'sdio', 'internal_dhcp', 'cli', cp_ovl=[CP_DHCP_BOTH])
    r = eh_test_expect(host, LEASE_REJECTED, fail=FAIL, timeout=T_IP)
    assert r.ok, f'no "{LEASE_REJECTED}" error for internal_dhcp + CP DHCP owner BOTH'
    assert 'DEFAULT_NETWORK_STACK_DHCP_HOST' in _since(host, 0)


# ── Negative: connected and quiet — nothing may be re-posted ────────────────
@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', ['app_external', 'internal_static'])
def test_nwsplit_quiet_no_events(bench, wifi_ap, mode):
    if not wifi_ap:
        pytest.skip('bench has no associable AP')
    led = Ledger(f'[sdio/{mode}/quiet]')
    host, mark = _bring_up(bench, wifi_ap, 'sdio', mode, 'cli')
    led.phase(host, mark, 'initial', got=None, conn=None)
    mark = _mark(host)
    time.sleep(30)
    led.phase(host, mark, 'quiet 30s', got=0, conn=0, lost=0, disc=0)
    led.verdict()


# ── Negative: drop with no reconnect — lost ip exactly once, no got ip ───────
@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', ['app_external', 'internal_static'])
# stop_start only: after sta_disconnect the CP example app (eh_example_sta)
# retries the connect itself, so the link does not stay down.
@pytest.mark.parametrize('trigger', ['stop_start'])
def test_nwsplit_drop_without_reconnect(bench, wifi_ap, mode, trigger):
    if not wifi_ap:
        pytest.skip('bench has no associable AP')
    led = Ledger(f'[sdio/{mode}/{trigger}/no-reconnect]')
    host, mark = _bring_up(bench, wifi_ap, 'sdio', mode, trigger)
    led.phase(host, mark, 'initial', got=None, conn=None)

    # CP auto-connect fires on STA_START only, so a stopped STA stays down.
    mark = _mark(host)
    _drop(host, trigger)
    eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
    time.sleep(LOST_IP_S + 10)
    led.phase(host, mark, 'down 20s', got=0, conn=0, lost=1, ping=False)

    # Back up once: one got ip, and the earlier lost ip is not repeated.
    mark = _mark(host)
    _restore(host, trigger, wifi_ap)
    led.phase(host, mark, 'restore')
    led.verdict()


# ── S42: config set while Wi-Fi is stopped ──────────────────────────────────
@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('split', ['split_off', 'split_on'])
def test_nwsplit_set_config_while_stopped(bench, wifi_ap, split):
    """wifi stop -> sta_connect x2 -> wifi start: IDF takes a config while stopped
    and connects once started. The CP must not refuse it with
    ESP_ERR_WIFI_NOT_STARTED (12290)."""
    if not wifi_ap:
        pytest.skip('bench has no associable AP')
    led = Ledger(f'[sdio/app_external/{split}/set-config-while-stopped]')
    off = dict(extra_host=['CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT=n'],
               cp_ovl=[CP_DHCP_HOST]) if split == 'split_off' else {}
    host, mark = _bring_up(bench, wifi_ap, 'sdio', 'app_external', 'cli', **off)
    led.phase(host, mark, 'initial', got=None, conn=None)
    mark, cp_mark = _mark(host), _mark(host.cp)
    host.write('wifi stop')
    eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
    time.sleep(2)
    _connect(host, wifi_ap)          # set_config + connect while stopped
    time.sleep(2)
    _connect(host, wifi_ap)          # again: a connect is already deferred to start
    time.sleep(2)
    host.write('wifi start')
    led.phase(host, mark, 'restart', disc=1, lost=None)
    # disconnect while stopped returns 12290 as in IDF; only set_config must not.
    if 'NOT_STARTED' in _since(host, mark) or \
            re.search(r'req_wifi_set_config.*\[12290\]', _since(host.cp, cp_mark)):
        led.bad.append('set_config refused while stopped')
    led.verdict()
