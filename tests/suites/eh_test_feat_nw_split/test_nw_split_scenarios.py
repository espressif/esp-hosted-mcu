# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Network-split host IP lifecycle: every supported and unsupported pairing.

The CP and the host share one MAC and one IPv4 address. Each test pins the whole
pairing it means, so no expectation rests on a Kconfig default:

  host split (on/off) x host netif mode x CP split (on/off) x CP DHCP owner
  x who creates WIFI_STA_DEF x who reconnects x host power save x CP lease events

App-visible contract, for every valid pairing:
  per association with a lease : sta connected x1, got ip x1, ping OK
  per drop < lost-ip interval  : sta disconnected x1, lost ip x0
  per drop > lost-ip interval  : sta disconnected x1, lost ip x1
  no link change               : no events
  one DHCP client per MAC      : the host client never REQUESTs in CP-owned modes
The CP owns the IP policy: the host asks for an owner at every CP connection and
runs with the answer. A refused request or a one-sided split is loud (an error
log, an event) and the node stays up; Wi-Fi is never refused.

Scenario IDs (Sxx) match nw_split_flows.md. Counts come from the full host log
(EmuDut._buf): expect() stops at the first match, so a duplicate would pass unseen.
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
GW, LEASE, LEASE_NEW = '192.168.4.1', '192.168.4.2', '192.168.4.3'
CYCLES = 5
LOST_IP_S = 10                   # host CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL
T_CONSOLE, T_IP, T_NOIP, T_PING = 30, 45, 8, 20

TAG = 'example_nw_split_iperf: '
GOT, LOST = TAG + 'got ip:', TAG + 'lost ip'
CONN, DISC = TAG + 'sta connected', TAG + 'sta disconnected'
HOST_ERR = re.compile(r'E \(\d+\) eh_nw_split:')
CP_ERR = re.compile(r'E \(\d+\) ehcp_nw_split:')
# IP owner answer on the host; the CP's decision and its drop of host DHCP client frames.
GRANTED = 'eh_nw_split: ip owner {} granted'
REFUSED = 'eh_nw_split: CP refused ip owner {}'
CP_DROPS_HOST_DHCP = 'host DHCP client frame dropped: the CP owns the IP'

# ── Host side ────────────────────────────────────────────────────────────────
HOST_SPLIT_OFF = 'CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT=n'
APP_NETIF = {
    'before': 'CONFIG_EXAMPLE_APP_STA_NETIF_BEFORE_HOSTED_INIT=y',
    'after':  'CONFIG_EXAMPLE_APP_STA_NETIF_AFTER_HOSTED_INIT=y',
    'none':   'CONFIG_EXAMPLE_APP_STA_NETIF_NONE=y',
}
HOST_DHCP_DEBUG = ['CONFIG_LWIP_DEBUG=y', 'CONFIG_LWIP_DHCP_DEBUG=y']
# lwIP dhcp.c trace lines: the client sent a REQUEST, or bound a lease.
DHCP_REQUESTED, DHCP_BOUND = 'dhcp_select: REQUESTING', 'dhcp_bind(netif='
# Host netif mode -> (host overlay, documented app behaviour for WIFI_STA_DEF).
MODES = {
    'app_external':    ('CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_APP_EXTERNAL=y', 'before'),
    'internal_static': ('CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_STATIC=y', 'none'),
    'internal_dhcp':   ('CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT_NETIF_INTERNAL_DHCP=y', 'none'),
}
HOST_AUTO_START_OFF = 'CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_CONNECT_ON_START=n'
HOST_AUTO_START_ON = 'CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_CONNECT_ON_START=y'
HOST_RECONNECT_OFF = 'CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_RECONNECT_ON_DISCONNECT=n'
HOST_RECONNECT_ON = 'CONFIG_ESP_HOSTED_HOST_FEAT_WIFI_STA_AUTO_RECONNECT_ON_DISCONNECT=y'
LOST_TIMER = f'CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL={LOST_IP_S}'
ECHO_PORT = 49999                # host range -> host stack
ECHO = ['CONFIG_EXAMPLE_NW_SPLIT_ECHO_SERVER=y', f'CONFIG_EXAMPLE_NW_SPLIT_ECHO_PORT={ECHO_PORT}']

# ── CP side ──────────────────────────────────────────────────────────────────
CP_SPLIT_OFF = 'CONFIG_ESP_HOSTED_CP_FEAT_NW_SPLIT=n'
OWNERS = {
    'both': 'CONFIG_ESP_HOSTED_DEFAULT_NETWORK_STACK_DHCP_BOTH=y',
    'cp':   'CONFIG_ESP_HOSTED_DEFAULT_NETWORK_STACK_DHCP_COPROCESSOR=y',
    'host': 'CONFIG_ESP_HOSTED_DEFAULT_NETWORK_STACK_DHCP_HOST=y',
}
# The CP policy each netif mode is granted under (ip-ownership.md).
VALID_OWNER = {'app_external': 'both', 'internal_static': 'both', 'internal_dhcp': 'host'}
CP_AUTO_ON = 'CONFIG_ESP_HOSTED_CP_FEAT_NW_SPLIT_WIFI_AUTO_CONNECT_ON_STA_START=y'
CP_AUTO_OFF = 'CONFIG_ESP_HOSTED_CP_FEAT_NW_SPLIT_WIFI_AUTO_CONNECT_ON_STA_START=n'
# The CP example app retries the connect on every disconnect unless this is 0.
CP_APP_NO_RETRY = 'CONFIG_EH_EXAMPLE_WIFI_MAXIMUM_RETRY=0'
CP_APP_RETRY = 'CONFIG_EH_EXAMPLE_WIFI_MAXIMUM_RETRY=6'


# Who drops the link and who brings it back. Each trigger has exactly one
# reconnect owner, except 'two_owners', which is the scenario itself.
#   cli          host sta_disconnect, then host sta_connect
#   feat_auto    the CP drops the link (lease drop); the host feature reconnects it.
#                An app's own sta_disconnect is not reconnected (S45).
#   stop_start   host wifi stop / wifi start, CP auto-connects on STA_START
#   host_start   host wifi stop / wifi start, host feature connects on STA_START
#   two_owners   the CP drops the link; host feature AND CP example app both reconnect
TRIGGERS = {
    'cli':        dict(host=[HOST_AUTO_START_OFF, HOST_RECONNECT_OFF],
                       cp=[CP_AUTO_OFF, CP_APP_NO_RETRY], cli_connect=True),
    'feat_auto':  dict(host=[HOST_AUTO_START_OFF, HOST_RECONNECT_ON],
                       cp=[CP_AUTO_OFF, CP_APP_NO_RETRY], cli_connect=True),
    'stop_start': dict(host=[HOST_AUTO_START_OFF, HOST_RECONNECT_OFF],
                       cp=[CP_AUTO_ON], cli_connect=False),
    'host_start': dict(host=[HOST_AUTO_START_ON, HOST_RECONNECT_OFF],
                       cp=[CP_AUTO_OFF, CP_APP_NO_RETRY], cli_connect=True),
    'two_owners': dict(host=[HOST_AUTO_START_OFF, HOST_RECONNECT_ON],
                       cp=[CP_AUTO_OFF, CP_APP_RETRY], cli_connect=True),
    'host_policy': dict(host=[HOST_AUTO_START_ON, HOST_RECONNECT_OFF],
                        cp=[CP_AUTO_OFF, CP_APP_NO_RETRY], cli_connect=False),
}


def _creds(ap):
    return [f'CONFIG_EH_EXAMPLE_WIFI_SSID="{ap["ssid"]}"',
            f'CONFIG_EH_EXAMPLE_WIFI_PASSWORD="{ap["password"]}"']


def _mark(dut):
    with dut._lock:
        return len(dut._buf)


def _since(dut, mark=0):
    with dut._lock:
        return dut._buf[mark:]


def _gw(host):
    """The gateway of the host's latest lease: GW on the emu, the AP's on a bench."""
    with host._lock:
        gws = re.findall(r'gw: (\d+\.\d+\.\d+\.\d+)', host._buf)
    return gws[-1] if gws else GW


def _ping(host, ok=True):
    host.write(f'ping {_gw(host)} -c 4 -i 0.5')
    want = r'\d+ packets transmitted, [1-9]\d* received' if ok \
        else r'\d+ packets transmitted, 0 received'
    return eh_test_expect(host, want, fail=FAIL, timeout=T_PING)


def _connect(host, ap):
    host.write(f'sta_connect {ap["ssid"]} {ap["password"]}')


def _drop(host, trigger):
    if trigger in ('feat_auto', 'two_owners'):
        host.cp.write('lease drop')     # a drop the host app did not ask for
    else:
        host.write('wifi stop' if trigger in ('stop_start', 'host_start') else 'sta_disconnect')


def _restore(host, trigger, ap):
    if trigger in ('stop_start', 'host_start'):
        host.write('wifi start')
    elif trigger == 'cli':
        _connect(host, ap)
    # feat_auto, two_owners: reconnect without a command.


def _bring_up(bench, wifi_ap, *, transport='sdio', mode='app_external', owner=None,
              trigger='cli', app_netif=None, host_split=True, cp_split=True,
              host_extra=(), cp_extra=(), want_net=False,
              wait_console=True, ap=None):
    """Build and boot one pairing. owner=None means the valid owner for mode."""
    if not wifi_ap:
        pytest.skip('bench has no associable AP')
    t = TRIGGERS[trigger]
    mode_ovl, netif_default = MODES[mode]
    host = [*t['host'], LOST_TIMER, APP_NETIF[app_netif or netif_default], *host_extra]
    host += [mode_ovl] if host_split else [HOST_SPLIT_OFF]
    if mode == 'app_external' and host_split:
        host += HOST_DHCP_DEBUG
    if want_net:
        host += ECHO
    cp = [*t['cp'], *cp_extra]
    cp += [OWNERS[owner or VALID_OWNER[mode]]] if cp_split else [CP_SPLIT_OFF]
    cp += _creds(ap or wifi_ap)     # inert unless a CP connect owner uses them
    extra = dict(hostfwd=(ECHO_PORT,)) if want_net else {}
    b = bench(IPF, 'mcu_host', transport, timeout='900s', wake=True,
              overlay=host, cp_overlay=cp, **extra)
    h = b['host']
    h.cp, h.net = b['cp'], b.get('net')
    if not hasattr(h, '_buf'):
        pytest.skip('event counting needs the emu DUT log buffer')
    if wait_console:
        r = eh_test_expect(h, r'network-split-iperf>', fail=FAIL, timeout=T_CONSOLE)
        assert r.ok, f'console ready: {r.matched}'
        if t['cli_connect']:
            # The prompt can come before the app's own esp_wifi_start; a connect before
            # it is ESP_ERR_WIFI_NOT_STARTED, as IDF.
            r = eh_test_expect(h, TAG + 'sta start', fail=FAIL, timeout=T_CONSOLE)
            assert r.ok, f'app station start: {r.matched}'
            _connect(h, ap or wifi_ap)
    return h


class Ledger:
    """Per-phase event counts. A failed phase is recorded and the run goes on, so
    one run yields the whole table instead of stopping at the first miss."""

    def __init__(self, label, ev=None):
        self.label, self.rows, self.bad = label, [], []
        self.ev = ev or dict(got=GOT, conn=CONN, lost=LOST, disc=DISC)

    def phase(self, host, mark, name, got=1, conn=1, lost=0, disc=None, ping=True,
              got_ip=None):
        """got='any': no got ip expected nor refused (the link may not have moved).
        got=None: 1..N got ip for N sta connected. A boot may associate twice
        (the host's esp_wifi_init re-inits CP Wi-Fi), and an association that
        ends before a lease posts no got ip, as in plain ESP-IDF.
        got_ip: the address the last got ip must carry."""
        ev = self.ev
        r = eh_test_expect(host, ev['got'], fail=FAIL, timeout=T_IP) if got not in (0, 'any') else None
        if r is not None and r.ok:
            eh_test_expect(host, ev['got'], fail=[], timeout=T_NOIP)   # room for a duplicate
        seg = _since(host, mark)
        have = {k: seg.count(v) for k, v in ev.items()}
        p_ok = _ping(host, ok=ping).ok if ping is not None else None
        want = dict(got=got, conn=conn, lost=lost, disc=disc)
        miss = []
        if got == 'any':
            want['got'] = None
        elif got is None:
            if not 1 <= have['got'] <= max(have['conn'], 1):
                miss.append(f"got {have['got']}/1..{max(have['conn'], 1)}")
            want['got'] = want['conn'] = None
        miss += [f'{k} {have[k]}/{want[k]}' for k in want
                 if want[k] is not None and have[k] != want[k]]
        if got_ip:
            last = re.findall(re.escape(GOT) + r'(\S+)', seg)
            if not last or last[-1] != got_ip:
                miss.append(f'got ip {last[-1] if last else "-"} want {got_ip}')
        if p_ok is False:
            miss.append('ping ' + ('fail' if ping else 'replied while down'))
        self.rows.append((name, have, p_ok, miss))
        if miss:
            self.bad.append(f'{name}: ' + ', '.join(miss))

    def expect(self, ok, what):
        if not ok:
            self.bad.append(what)

    def verdict(self):
        lines = [f'{self.label}  phase: got/conn/lost/disc ping']
        for name, h, p, miss in self.rows:
            lines.append(f'  {name:<14} {h["got"]}/{h["conn"]}/{h["lost"]}/{h["disc"]} '
                         f'{"-" if p is None else ("ok" if p else "FAIL")}'
                         f'{"   <-- " + "; ".join(miss) if miss else ""}')
        print('\n'.join(lines))
        assert not self.bad, '\n'.join(lines + self.bad)


def _check_one_client(led, host, mode):
    """CP-owned modes: the host client must never REQUEST or bind, or its stop
    would RELEASE the lease the CP holds. Whatever it sent before the stop (a
    DISCOVER) the CP keeps off the air."""
    if mode != 'app_external':
        return
    log = _since(host)
    for bad in (DHCP_REQUESTED, DHCP_BOUND, 'invalid static ip'):
        led.expect(bad not in log, f'host log has "{bad}"')
    if 'sendto(DISCOVER' in log:
        led.expect(CP_DROPS_HOST_DHCP in _since(host.cp), 'host DISCOVER reached the air')


def _check_owner(led, host, owner, refused=None):
    """The host asked for its owner and runs with what the CP answered."""
    log = _since(host)
    led.expect(GRANTED.format(owner.upper()) in log or
               (refused and REFUSED.format(refused.upper()) in log),
               f'host never ran with ip owner {owner}')
    if refused:
        led.expect(REFUSED.format(refused.upper()) in log, f'refusal of {refused} not logged')
    else:
        led.expect('CP refused ip owner' not in log, 'CP refused the ip owner request')


def _check_cp_has_host_lease(led, host):
    """Host-owned: the CP runs no client, so its only address is the pushed one."""
    cp = _since(host.cp)
    led.expect('host lease on the CP netif: ' + LEASE in cp, 'CP never took the host lease')
    led.expect('refused, the CP owns the IP' not in cp, 'CP refused the host lease')


def _wait_for(dut, mark, text, timeout):
    """True once text appears in the dut log after mark."""
    end = time.time() + timeout
    while time.time() < end:
        if text in _since(dut, mark):
            return True
        time.sleep(0.5)
    return False


def _check_inbound(led, host):
    """An outside peer reaches a host-range port: the CP answers ARP for the
    shared address and routes the port to the host."""
    if f'echo server listening on host port {ECHO_PORT}' not in _since(host):
        led.bad.append('host echo server never started')
        return
    probe = b'nw-split-inbound-probe'
    led.expect(probe in host.net.request(ECHO_PORT, probe), f'inbound :{ECHO_PORT} echo failed')


def _cycles(led, host, trigger, ap, n):
    for c in range(1, n + 1):
        mark = _mark(host)
        _drop(host, trigger)
        eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
        _restore(host, trigger, ap)
        # IDF: esp_wifi_disconnect() / esp_wifi_stop() while connected posts one STA_DISCONNECTED.
        led.phase(host, mark, f'cycle {c}/{n}', disc=1)


# ── S01-S08, S33: drop + same-lease reconnect, valid pairings ──────────────
# sdio only: split traffic per wire is test_nw_split.py::test_nwsplit_cp_autoconnect_host_receives_ip.
LINK = [
    # (scenario, transport, mode, owner, trigger)
    *[('S01-03', 'sdio', 'app_external', 'both', t) for t in ('cli', 'feat_auto', 'stop_start')],
    *[('S04', 'sdio', 'internal_static', 'both', t) for t in ('cli', 'feat_auto', 'stop_start')],
    ('S06', 'sdio', 'internal_dhcp', 'host', 'cli'),
    *[('S07', 'sdio', 'internal_dhcp', 'host', t) for t in ('feat_auto', 'stop_start')],
    *[('S08', 'sdio', m, 'cp', 'cli') for m in ('app_external', 'internal_static')],
    *[('S33', 'sdio', m, VALID_OWNER[m], 'host_start') for m in MODES],
]


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('sid,transport,mode,owner,trigger', LINK,
                         ids=[f'{s}-{tr}-{m}-{o}-{t}' for s, tr, m, o, t in LINK])
def test_nwsplit_link_cycles(bench, wifi_ap, sid, transport, mode, owner, trigger):
    led = Ledger(f'[{sid} {transport}/{mode}/{owner}/{trigger}]')
    # Inbound probe on sdio only: the TCP echo through esp-emu user-net is a known
    # flake on spi_fd (see test_nw_split.py).
    inbound = transport == 'sdio'
    host = _bring_up(bench, wifi_ap, transport=transport, mode=mode, owner=owner,
                     trigger=trigger, want_net=inbound)
    led.phase(host, 0, 'initial', got=None, conn=None)
    _cycles(led, host, trigger, wifi_ap, CYCLES)
    if inbound:
        _check_inbound(led, host)
    if trigger == 'feat_auto':
        # The reconnect policy acts on drops the app did not ask for only.
        mark = _mark(host)
        host.write('sta_disconnect')
        eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
        eh_test_expect(host, CONN, fail=[], timeout=T_NOIP)     # none may come
        led.phase(host, mark, 'app disconnect', got=0, conn=0, disc=1, lost=None, ping=None)
    _check_one_client(led, host, mode)
    _check_owner(led, host, 'host' if mode == 'internal_dhcp' else 'cp')
    if mode == 'internal_dhcp':
        _check_cp_has_host_lease(led, host)
    led.verdict()


# ── S32: CP connects at boot, host never asks — exact counts ───────────────
ONLY_IF_NEEDED = 'CONFIG_ESP_HOSTED_HOST_CP_RESET_STRATEGY_ONLY_IF_NECESSARY=y'
CP_RESET = re.compile(r'Reset co-processor|Attempt slave reset|Slave reset issued')

BOOT_NO_WIFI = 'CONFIG_EXAMPLE_BOOT_WIFI_NONE=y'
S32_ROWS = [*[(m, False, 'start') for m in MODES], ('app_external', True, 'start')]


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode,restart,boot', S32_ROWS)
def test_nwsplit_boot_cp_autoconnect(bench, wifi_ap, mode, restart, boot):
    """restart (B13): the host restarts with the CP kept (reset only if needed). Its
    esp_restart stops Wi-Fi first, as IDF; the new boot's start brings the CP link
    back: one connect, one got ip, no CP reset.
    boot start: the host app inits and starts Wi-Fi and never connects; the CP's
    link is adopted and its IP given (network split needs the app's start)."""
    led = Ledger(f'[S32 {mode}/boot-cp-auto{"/restart" if restart else ""}/{boot}]')
    extra = [ONLY_IF_NEEDED] if restart else []
    host = _bring_up(bench, wifi_ap, mode=mode, trigger='stop_start', host_extra=extra)
    led.phase(host, 0, 'boot')
    if restart:
        host.write('restart')
        eh_test_expect(host, CP_BOOT, fail=FAIL, timeout=T_CONSOLE)
        mark = _mark(host)
        eh_test_expect(host, r'network-split-iperf>', fail=FAIL, timeout=T_CONSOLE)
        led.phase(host, mark, 'after restart', disc=0)
        led.expect(not CP_RESET.search(_since(host, mark)), 'the CP was reset: needs a kept CP')
    led.verdict()


# ── S61: the host's Wi-Fi init re-inits a CP that already associated ───────
CP_ASSOC = '--- Station connected'
CP_REINIT = 'WiFi init config changed, reinitializing'
CP_BOOT = 'rst:0x'
# The host waits this long after the CP reset, so the CP app associates first.
HOST_SETTLE = 'CONFIG_ESP_HOSTED_HOST_CP_RESET_SETTLE_MS=5000'


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', list(MODES))
def test_nwsplit_boot_reinit_invisible(bench, wifi_ap, mode):
    """The CP app inits Wi-Fi with CP defaults and auto-connects. The host's
    esp_wifi_init differs, so the CP stops, deinits and inits again, which drops
    that link. The host app must not see it: one start and no stop (the CP's
    stop is reported before its init reply), no disconnect, then one connect and
    one got ip."""
    led = Ledger(f'[S61 {mode}/boot-reinit]')
    host = _bring_up(bench, wifi_ap, mode=mode, trigger='stop_start', host_extra=[HOST_SETTLE],
                     want_net=True)
    led.phase(host, 0, 'boot', got=1, conn=1, disc=0)
    boot = _since(host)
    for ev, want in (('sta start', 1), ('sta stop', 0)):
        n = boot.count(TAG + ev)
        led.expect(n == want, f'boot: {ev} x{n}, want {want}')
    _check_inbound(led, host)       # the split RX path survives the CP's Wi-Fi re-init
    cp = _since(host.cp)
    led.expect(CP_REINIT in cp, 'CP never re-initialised Wi-Fi for the host config')
    before = cp[:cp.find(CP_REINIT)]
    led.expect(CP_ASSOC in before[before.rfind(CP_BOOT):],
               'CP did not associate before the host init')
    led.verdict()


# ── S64: every attempt fails; each retry owner keeps retrying ──────────────
NO_AP = {'ssid': 'eh-no-such-ap', 'password': 'nopassword'}
CP_APP_GAVE_UP = 'connect FAILED after 6 tries'
HOST_NO_AP = re.compile(r'StaDisconnected ssid="eh-no-such-ap" reason=201')
APP_NO_AP = TAG + 'sta disconnected reason=201'


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('owner', ['cp_app', 'host_feat'])
def test_nwsplit_connect_retries_after_failure(bench, wifi_ap, owner):
    """No such AP. cp_app: the CP app retries from its own STA_DISCONNECTED, which
    reaches it before the CP station FSM; every retry must reach the driver. The host
    app never connected: those attempts are not its own, it sees none of them.
    host_feat: the host's reconnect policy retries each failed attempt, on the app's
    behalf: the app sees each one."""
    led = Ledger(f'[S64 {owner}/no-ap-retries]')
    if owner == 'cp_app':
        host = _bring_up(bench, wifi_ap, trigger='stop_start', cp_extra=[CP_APP_RETRY], ap=NO_AP)
        led.expect(eh_test_expect(host.cp, CP_APP_GAVE_UP, fail=FAIL, timeout=2 * T_IP).ok,
                   f'CP app retries stopped early: {_since(host.cp).count("retry ")} of 6 ran')
        n = _since(host).count(TAG + 'sta disconnected')
        led.expect(n == 0, f'host app saw {n} disconnect(s) of attempts it never made')
    else:
        host = _bring_up(bench, wifi_ap, trigger='feat_auto', ap=NO_AP)
        deadline = time.time() + T_IP
        while time.time() < deadline and len(HOST_NO_AP.findall(_since(host))) < 3:
            time.sleep(1)
        n = len(HOST_NO_AP.findall(_since(host)))
        led.expect(n >= 3, f'host reconnect policy stopped after {n} failed attempt(s)')
        n_app = _since(host).count(APP_NO_AP)
        led.expect(n_app >= 3, f'host app saw {n_app} of the policy\'s failed attempts (want >= 3)')
    led.verdict()


# ── S65: the link drops; the CP app's own retry brings it back ─────────────
@pytest.mark.nw_split
@pytest.mark.wifi
def test_nwsplit_cp_app_reconnects_after_drop(bench, wifi_ap):
    """CP code drops the link (driver disconnect, no host request). The CP app
    retries from its own STA_DISCONNECTED, which reaches it before the CP station
    FSM; the host's reconnect policy is off, so only that retry restores the link."""
    led = Ledger('[S65 cp-app-reconnect]')
    host = _bring_up(bench, wifi_ap, cp_extra=[CP_APP_RETRY])
    led.phase(host, 0, 'initial', got=None, conn=None)
    mark, cp_mark = _mark(host), _mark(host.cp)
    host.cp.write('lease drop')
    led.expect(_wait_for(host.cp, cp_mark, '[lease] drop rc=0', T_IP), 'CP did not drop the link')
    led.expect(_wait_for(host.cp, cp_mark, CP_ASSOC, T_IP), 'CP app retry did not restore the link')
    led.phase(host, mark, 'after drop', disc=1)
    led.verdict()


# ── S66: a new config while connected is a real connect ────────────────────
@pytest.mark.nw_split
@pytest.mark.wifi
def test_nwsplit_new_config_while_connected(bench, wifi_ap):
    """Connected, the app connects with another SSID: the link drops and the next
    attempt uses the new config (that AP is absent: one failed attempt, no loop).
    The CLI disconnects first (DISCONNECTED 8), then its attempt's outcome (201)
    reaches the app. Connecting back restores the link once."""
    led = Ledger('[S66 new-config-while-connected]')
    host = _bring_up(bench, wifi_ap)
    led.phase(host, 0, 'initial', got=None, conn=None)
    mark = _mark(host)
    _connect(host, NO_AP)
    led.expect(_wait_for(host, mark, 'StaDisconnected ssid="eh-no-such-ap" reason=201', T_IP),
               'no attempt with the new config')
    time.sleep(LOST_IP_S + 5)
    n = len(HOST_NO_AP.findall(_since(host, mark)))
    led.expect(n == 1, f'{n} failed attempts with no retry owner (want 1)')
    led.expect(CONN not in _since(host, mark), 'reconnected to the old AP after the switch')
    app = re.findall(r'sta disconnected reason=(\d+)', _since(host, mark))
    led.expect(app == ['8', '201'], f'app saw disconnects {app} (want 8 for the old link, then 201)')
    mark = _mark(host)
    _connect(host, wifi_ap)
    led.phase(host, mark, 'back to the AP', disc=None)
    led.verdict()


# ── S67: the policy connect fails; the FSM reports it once and waits ───────
@pytest.mark.nw_split
@pytest.mark.wifi
def test_nwsplit_policy_connect_fails(bench, wifi_ap):
    """Host auto-connect on start with no SSID anywhere: the connect RPC fails.
    One error, no retry storm, no Wi-Fi event to the app; a later app connect works."""
    led = Ledger('[S67 policy-connect-fails]')
    host = _bring_up(bench, wifi_ap, trigger='host_policy', ap={'ssid': '', 'password': ''})
    led.expect(_wait_for(host, 0, 'policy connect failed', T_IP), 'policy connect error not reported')
    time.sleep(LOST_IP_S)
    seg = _since(host)
    led.expect(seg.count('policy connect failed') == 1,
               f'policy connect failed {seg.count("policy connect failed")} times (want 1)')
    led.expect(CONN not in seg and DISC not in seg, 'the app saw a Wi-Fi event for a failed RPC')
    mark = _mark(host)
    _connect(host, wifi_ap)
    led.phase(host, mark, 'app connect', disc=0)
    led.verdict()


# ── S68: the app disconnects while attempts keep failing ───────────────────
@pytest.mark.nw_split
@pytest.mark.wifi
def test_nwsplit_app_disconnect_stops_retries(bench, wifi_ap):
    """Host reconnect policy on, the AP is absent: attempts keep failing. The app's
    esp_wifi_disconnect ends them, as in IDF; the policy must not reconnect after it."""
    led = Ledger('[S68 app-disconnect-stops-retries]')
    host = _bring_up(bench, wifi_ap, trigger='feat_auto', ap=NO_AP)
    deadline = time.time() + T_IP
    while time.time() < deadline and len(HOST_NO_AP.findall(_since(host))) < 3:
        time.sleep(1)
    led.expect(len(HOST_NO_AP.findall(_since(host))) >= 3, 'attempts did not repeat before the disconnect')
    host.write('sta_disconnect')
    time.sleep(3)                   # the attempt in flight may still report
    mark = _mark(host)
    time.sleep(LOST_IP_S + 10)
    n = len(HOST_NO_AP.findall(_since(host, mark)))
    led.expect(n == 0, f'{n} more attempts after the app disconnected')
    mark = _mark(host)
    _connect(host, wifi_ap)
    led.phase(host, mark, 'app connects again', disc=None)
    led.verdict()


# ── S73: the app stops Wi-Fi while connected, reconnect policy on ──────────
@pytest.mark.nw_split
@pytest.mark.wifi
def test_nwsplit_stop_with_reconnect_policy(bench, wifi_ap):
    """esp_wifi_stop while connected, host reconnect-on-drop on: as IDF, one
    STA_DISCONNECTED then one STA_STOP, and no reconnect (the stop is the app's,
    even when the CP's own reports of it arrive before the stop's answer)."""
    led = Ledger('[S73 stop-with-reconnect-policy]')
    host = _bring_up(bench, wifi_ap, trigger='feat_auto')
    led.phase(host, 0, 'boot', got=1, conn=1, disc=0)
    mark, cp_mark = _mark(host), _mark(host.cp)
    host.write('wifi stop')
    time.sleep(10)
    seg = _since(host, mark)
    for ev, want in (('sta disconnected', 1), ('sta stop', 1), ('sta connected', 0)):
        n = seg.count(TAG + ev)
        led.expect(n == want, f'after stop: {ev} x{n}, want {want}')
    led.expect(CP_ASSOC not in _since(host.cp, cp_mark), 'the CP associated again after the stop')
    led.verdict()


# ── S69: disconnect then connect, back to back ─────────────────────────────
@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', ['app_external', 'internal_static'])
def test_nwsplit_disconnect_connect_back_to_back(bench, wifi_ap, mode):
    """esp_wifi_disconnect immediately followed by esp_wifi_connect: the app sees
    the disconnect, then one connect and one got ip, as in IDF."""
    led = Ledger(f'[S69 {mode}/disconnect-connect]')
    host = _bring_up(bench, wifi_ap, mode=mode)
    led.phase(host, 0, 'initial', got=None, conn=None)
    for c in range(1, 4):
        mark = _mark(host)
        host.write('sta_disconnect')
        _connect(host, wifi_ap)
        led.phase(host, mark, f'cycle {c}', disc=1, lost=None)
    led.verdict()


# ── S70: the CP's lease status crosses the app's disconnect ────────────────
@pytest.mark.nw_split
@pytest.mark.wifi
def test_nwsplit_lease_status_vs_app_disconnect(bench, wifi_ap):
    """A CP lease status in flight when the app disconnects must not bring the app
    a STA_CONNECTED for a link that is down."""
    led = Ledger('[S70 lease-status-vs-disconnect]')
    host = _bring_up(bench, wifi_ap, mode='internal_static')
    led.phase(host, 0, 'initial', got=None, conn=None)
    for c in range(1, 6):
        mark = _mark(host)
        host.cp.write('lease change 1')
        host.write('sta_disconnect')
        led.expect(_wait_for(host, mark, DISC, T_IP), f'cycle {c}: no disconnect')
        time.sleep(5)
        seg = _since(host, mark)
        led.expect(CONN not in seg[seg.find(DISC):], f'cycle {c}: STA_CONNECTED after the app disconnected')
        mark = _mark(host)
        _connect(host, wifi_ap)
        led.phase(host, mark, f'cycle {c} back', disc=None, lost=None)
    led.verdict()


# ── S71: a CP lease status in flight when the CP drops the link ────────────
@pytest.mark.nw_split
@pytest.mark.wifi
def test_nwsplit_lease_status_vs_cp_drop(bench, wifi_ap):
    """The CP changes its lease and drops the link at once: the app sees the
    disconnect, never a STA_CONNECTED after it for a link that is down."""
    led = Ledger('[S71 lease-status-vs-cp-drop]')
    host = _bring_up(bench, wifi_ap, mode='internal_static')
    led.phase(host, 0, 'initial', got=None, conn=None)
    mark, cp_mark = _mark(host), _mark(host.cp)
    host.cp.write('lease change 1')
    led.expect(_wait_for(host.cp, cp_mark, '[lease] change rc=', T_IP), 'CP did not change its lease')
    host.cp.write('lease drop')     # the change's status is still on its way to the app
    led.expect(_wait_for(host, mark, DISC, T_IP), 'no disconnect')
    time.sleep(LOST_IP_S + 5)
    seg = _since(host, mark)
    led.expect(CONN not in seg[seg.find(DISC):], 'STA_CONNECTED after the CP dropped the link')
    led.verdict()


# ── S34: shipped defaults on both sides, only the credentials set ──────────
@pytest.mark.nw_split
@pytest.mark.wifi
def test_nwsplit_shipped_defaults(bench, wifi_ap):
    """No netif-mode, owner, reconnect or netif-creation pins: what a user gets."""
    if not wifi_ap:
        pytest.skip('bench has no associable AP')
    b = bench(IPF, 'mcu_host', 'sdio', timeout='900s', wake=True,
              overlay=[LOST_TIMER], cp_overlay=[])
    host = b['host']
    led = Ledger('[S34 defaults]')
    assert eh_test_expect(host, r'network-split-iperf>', fail=FAIL, timeout=T_CONSOLE).ok
    # A connect before the app's own esp_wifi_start is ESP_ERR_WIFI_NOT_STARTED, as IDF.
    assert eh_test_expect(host, TAG + 'sta start', fail=FAIL, timeout=T_CONSOLE).ok
    _connect(host, wifi_ap)
    led.phase(host, 0, 'initial', got=None, conn=None)
    _cycles(led, host, 'cli', wifi_ap, 3)
    led.verdict()


# ── S09-S14: the CP decides the IP owner; a one-sided split degrades ────────
# (sid, netif mode, CP policy, owner the host runs with, owner the CP refuses)
OWNER_ASK = [('S09', 'app_external', 'host', 'cp', None),
             ('S09', 'internal_static', 'host', 'cp', None),
             ('S10', 'internal_dhcp', 'both', 'host', None),
             ('S11', 'internal_dhcp', 'cp', 'cp', 'host')]


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('sid,mode,owner,runs,refused', OWNER_ASK,
                         ids=[f'{s}-{m}-{o}' for s, m, o, _, _ in OWNER_ASK])
def test_nwsplit_ip_owner_negotiated(bench, wifi_ap, sid, mode, owner, runs, refused):
    """The host asks for the owner its netif mode wants; the CP grants it or keeps
    its own. Either way: one association, one lease, one DHCP client."""
    led = Ledger(f'[{sid} {mode}/cp-policy-{owner}]')
    host = _bring_up(bench, wifi_ap, mode=mode, owner=owner)
    led.phase(host, 0, 'initial', got=None, conn=None)
    _cycles(led, host, 'cli', wifi_ap, 1)
    _check_owner(led, host, runs, refused)
    led.expect(bool(HOST_ERR.search(_since(host))) == bool(refused),
               'refusal must be the only host error' if refused else 'host nw_split logged an error')
    _check_one_client(led, host, mode)
    if runs == 'host':
        _check_cp_has_host_lease(led, host)
    led.verdict()


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', list(MODES))
def test_nwsplit_cp_split_off_degrades(bench, wifi_ap, mode):
    """S12/S13: host split on, CP split off: the host names the CP option and runs
    as plain hosted, with its own DHCP client; an INTERNAL_STATIC netif has none."""
    led = Ledger(f'[S12/13 {mode}/cp-split-off]')
    host = _bring_up(bench, wifi_ap, mode=mode, cp_split=False)
    has_client = mode != 'internal_static'
    led.expect(eh_test_expect(host, CONN, fail=FAIL, timeout=T_IP).ok, 'never associated')
    if not has_client:
        eh_test_expect(host, GOT, fail=[], timeout=T_NOIP)     # none may come
    led.phase(host, 0, 'initial', got=1 if has_client else 0, conn=1, disc=0,
              ping=True if has_client else None)
    errs = [l for l in _since(host).splitlines() if HOST_ERR.search(l)]
    led.expect(any('CONFIG_ESP_HOSTED_CP_FEAT_NW_SPLIT' in l for l in errs),
               f'no eh_nw_split error naming the CP option: {errs}')
    led.verdict()


@pytest.mark.nw_split
@pytest.mark.wifi
def test_nwsplit_host_split_off_degrades(bench, wifi_ap):
    """S14: CP split on, host split off: the host names the host option; the CP
    keeps the IP and keeps the host's DHCP client off the air (one client), so
    the host associates without one. No crash."""
    led = Ledger('[S14 host-split-off]')
    host = _bring_up(bench, wifi_ap, mode='app_external', host_split=False)
    led.expect(eh_test_expect(host, CONN, fail=FAIL, timeout=T_IP).ok, 'never associated')
    eh_test_expect(host, GOT, fail=[], timeout=T_NOIP)         # none may come
    # IDF's own lost-ip timer may fire for the host DHCP client that never binds.
    led.phase(host, 0, 'initial', got=0, conn=1, disc=0, lost=None, ping=None)
    led.expect(CP_DROPS_HOST_DHCP in _since(host.cp), 'host DHCP client reached the air')
    errs = [l for l in _since(host).splitlines() if HOST_ERR.search(l)]
    led.expect(any('CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT' in l for l in errs),
               f'no eh_nw_split error naming the host option: {errs}')
    led.verdict()


# ── S62: owner asked while the CP holds a lease: switch at the CP's disconnect ─
@pytest.mark.nw_split
@pytest.mark.wifi
def test_nwsplit_ip_owner_switch_at_disconnect(bench, wifi_ap):
    """CP policy BOTH. The CP associates and binds before the host asks for the
    IP, so the CP keeps its lease (no RELEASE of an address in use) and switches
    at its next disconnect: here the Wi-Fi re-init for the host's config, which
    the app never sees. The host learns it at its first connect and runs its own
    DHCP; one connect, one lease, and the CP carries the host's lease."""
    led = Ledger('[S62 internal_dhcp/both/switch-at-disconnect]')
    host = _bring_up(bench, wifi_ap, mode='internal_dhcp', owner='both', trigger='stop_start',
                     host_extra=[HOST_SETTLE], want_net=True)
    led.phase(host, 0, 'boot', got=1, conn=1, disc=0, got_ip=LEASE)
    log, cp = _since(host), _since(host.cp)
    led.expect('ip owner HOST granted at the next disconnect' in log,
               'host was not told the switch waits for the CP disconnect')
    led.expect('ip owner CP -> HOST' in log, 'host never switched to its own DHCP')
    led.expect('ip owner CP -> HOST' in cp, 'CP never switched the owner')
    led.expect(CP_ASSOC in cp[:cp.find('ip owner CP -> HOST')], 'CP switched before it associated')
    _check_cp_has_host_lease(led, host)
    mark = _mark(host)
    _drop(host, 'stop_start')
    eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
    _restore(host, 'stop_start', wifi_ap)
    led.phase(host, mark, 'cycle', disc=1, got_ip=LEASE)
    led.expect('ip owner HOST -> CP' not in _since(host, mark), 'owner switched back')
    _check_inbound(led, host)
    led.verdict()


# ── S63: the CP reboots under a running host ─────────────────────────────────
CP_INIT_SEEN = 'eh_init_evt: chip='


# feat_auto relies on the host's single-shot auto-reconnect after a CP restart: one
# connect RPC that times out (CP still booting) ends reconnection. That is open
# design (.meta2/changes/cp-restart, Q4/Q6), not what this test proves.
_REBOOT_RECONNECT_LIMIT = ('host auto-reconnect after a CP restart is single-shot (open '
                           'cp-restart design); non-blocking until that is decided')


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('transport', ['sdio', 'spi_hd', 'uart', 'spi_fd'])
@pytest.mark.parametrize('trigger', [
    pytest.param('feat_auto', marks=pytest.mark.allow_fail(reason=_REBOOT_RECONNECT_LIMIT)),
    'stop_start',
])
def test_nwsplit_cp_reboot_under_host(bench, wifi_ap, trigger, transport):
    """The CP reboots while the host app is connected; the host is never left
    CONNECTED on an association the CP lost. feat_auto: the CP does not reconnect,
    so the host sees it unlinked and reconnects (disc, conn, got ip once each).
    stop_start: the CP's own auto-connect races the host's check, either outcome."""
    led = Ledger(f'[S63 {transport}/{trigger}/cp-reboot]')
    host = _bring_up(bench, wifi_ap, transport=transport, mode='app_external',
                     trigger=trigger, want_net=transport == 'sdio')
    led.phase(host, 0, 'initial', got=None, conn=None)
    mark, cp_mark = _mark(host), _mark(host.cp)
    host.cp.write('reboot')
    led.expect(_wait_for(host.cp, cp_mark, CP_BOOT, T_IP), 'CP did not reboot')
    led.expect(_wait_for(host, mark, CP_INIT_SEEN, T_IP), 'host never saw the CP come back')
    if trigger == 'feat_auto':
        led.phase(host, mark, 'after reboot', got=1, conn=1, disc=1, lost=None)
    else:
        if DISC in _since(host, mark):
            eh_test_expect(host, GOT, fail=FAIL, timeout=T_IP)  # the link was rebuilt
        led.phase(host, mark, 'after reboot', got='any', conn=None, disc=None, lost=None)
    seg = _since(host, mark)
    led.expect(seg.rfind(CONN) > seg.rfind(DISC) if DISC in seg else CONN not in seg,
               f'app left disconnected (disc {seg.count(DISC)}, conn {seg.count(CONN)})')
    led.expect('ip owner' in _since(host, mark), 'host did not ask for the IP owner again')
    if transport == 'sdio':
        _check_inbound(led, host)
    led.verdict()


# ── S15/S16: drop with no reconnect — lost ip once, no got ip ──────────────
@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', list(MODES))
def test_nwsplit_drop_without_reconnect(bench, wifi_ap, mode):
    # stop_start: CP auto-connect fires on STA_START only, so a stopped STA stays down.
    led = Ledger(f'[S15/16 {mode}/no-reconnect]')
    host = _bring_up(bench, wifi_ap, mode=mode, trigger='stop_start')
    led.phase(host, 0, 'initial', got=None, conn=None)
    mark = _mark(host)
    _drop(host, 'stop_start')
    eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
    time.sleep(LOST_IP_S + 10)
    led.phase(host, mark, 'down 20s', got=0, conn=0, lost=1, ping=False)
    mark = _mark(host)
    _restore(host, 'stop_start', wifi_ap)
    led.phase(host, mark, 'restore')
    led.verdict()


# ── S47: stop + start back to back, no wait in between ─────────────────────
@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', ['app_external', 'internal_static'])
def test_nwsplit_stop_start_back_to_back(bench, wifi_ap, mode):
    """esp_wifi_stop() then esp_wifi_start() at once: per cycle one disconnected,
    one connected and one got ip, as in IDF. The CP's stop events trail the start."""
    led = Ledger(f'[S47 {mode}/stop-start back-to-back]')
    host = _bring_up(bench, wifi_ap, mode=mode, trigger='stop_start')
    led.phase(host, 0, 'initial', got=None, conn=None)
    for c in range(1, CYCLES + 1):
        mark = _mark(host)
        host.write('wifi stop')
        host.write('wifi start')
        led.phase(host, mark, f'cycle {c}/{CYCLES}', disc=1)
    _check_one_client(led, host, mode)
    led.verdict()


# ── S17/S18: connected and quiet — nothing re-posted ───────────────────────
@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', list(MODES))
def test_nwsplit_quiet_no_events(bench, wifi_ap, mode):
    """Connected and quiet: nothing re-posted. Then the soft-AP joins and leaves the
    shared radio (intent/idf-measured.md M22-M28): the station link, its lease and
    the split routing see only what IDF posts for each mode change. Last, the
    station's own disconnect, an idle disconnect and a scan get IDF's events only."""
    led = Ledger(f'[S17/18 {mode}/quiet+apsta]')
    host = _bring_up(bench, wifi_ap, mode=mode)
    led.phase(host, 0, 'initial', got=None, conn=None)
    mark = _mark(host)
    time.sleep(30)
    led.phase(host, mark, 'quiet 30s', got=0, conn=0, lost=0, disc=0)
    for name, cmd, counts, got, conn, disc, ping in APSTA_PHASES:
        mark = _mark(host)
        host.write(cmd)
        if name.startswith('M26'):
            eh_test_expect(host, TAG + 'sta start', fail=FAIL, timeout=T_IP)
            _connect(host, wifi_ap)
        elif name.startswith('M28'):
            eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
            _connect(host, wifi_ap)
        time.sleep(T_NOIP)
        led.phase(host, mark, name, got=got, conn=conn, lost=None, disc=disc, ping=ping)
        seg = _since(host, mark)
        for ev, n in counts.items():
            have = seg.count(TAG + ev)
            led.expect(have == n, f'{name}: {ev} x{have}, want {n}')
        if name.startswith('M25'):
            # A16: the host app runs a soft-AP only; CP code connects the station.
            mark = _mark(host)
            host.cp.write('lease cp_connect_apsta')
            eh_test_expect(host.cp, CP_ASSOC, fail=FAIL, timeout=T_IP)
            time.sleep(T_NOIP)
            seg = _since(host, mark)
            led.expect(not any(TAG + e in seg for e in ('sta start', 'sta connected', 'got ip')),
                       'A16: AP-only host app got station events')
            host.cp.write('lease drop')     # back to the AP-only state the next phase expects
            eh_test_expect(host.cp, r'\[lease\] drop rc=', fail=FAIL, timeout=T_IP)
            time.sleep(T_NOIP)
    led.verdict()


# (name, command, exact soft-AP/station start-stop counts, got, conn, disc, ping)
APSTA_PHASES = (
    ('M22 apsta on', 'wifi_mode apsta',
     {'ap start': 1, 'ap stop': 0, 'sta start': 0, 'sta stop': 0}, 0, 0, 0, True),
    ('M28 apsta drop', 'sta_disconnect',
     {'ap start': 0, 'ap stop': 0, 'sta start': 0, 'sta stop': 0}, 1, 1, 1, True),
    ('M25 ap only', 'wifi_mode ap',
     {'ap start': 0, 'ap stop': 0, 'sta start': 0, 'sta stop': 1}, 0, 0, 1, None),
    ('M26 apsta from ap', 'wifi_mode apsta',
     {'ap start': 0, 'ap stop': 0, 'sta start': 1, 'sta stop': 0}, 1, 1, 0, True),
    ('M27 sta only', 'wifi_mode sta',
     {'ap start': 0, 'ap stop': 1, 'sta start': 0, 'sta stop': 0}, 0, 0, 0, True),
    ('M12 disconnect', 'sta_disconnect', {'scan done': 0}, 0, 0, 1, None),
    ('M6 disconnect idle', 'sta_disconnect', {'scan done': 0}, 0, 0, 0, None),
    ('M35 scan', 'sta_scan', {'scan done': 1}, 0, 0, 0, None),
)


# ── S19/S20: CP lease changes or is lost while associated ──────────────────
LEASE_HOLD_S = LOST_IP_S + 10


def _lease_bring_up(bench, wifi_ap, mode):
    return _bring_up(bench, wifi_ap, mode=mode, cp_extra=[LOST_TIMER])


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', ['app_external', 'internal_static'])
def test_nwsplit_lease_changes(bench, wifi_ap, mode):
    """S19: the CP address moves to another one; the host follows, once, with
    ip_changed set, and posts no link events."""
    led = Ledger(f'[S19 {mode}/lease-change]')
    host = _lease_bring_up(bench, wifi_ap, mode)
    led.phase(host, 0, 'initial', got=None, conn=None, got_ip=LEASE)
    mark = _mark(host)
    host.cp.write('lease change')
    assert eh_test_expect(host.cp, r'\[lease\] change rc=0', fail=FAIL, timeout=T_IP).ok
    led.phase(host, mark, 'changed', got=1, conn=0, disc=0, ping=None, got_ip=LEASE_NEW)
    led.expect(f'{LEASE_NEW} changed=1' in _since(host, mark), 'got ip without ip_changed')
    led.verdict()


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', ['app_external', 'internal_static'])
def test_nwsplit_lease_lost_while_associated(bench, wifi_ap, mode):
    """S20: the CP lease goes away with the station still associated. The host
    must lose the address too (lost ip once), then get it back once."""
    led = Ledger(f'[S20 {mode}/lease-lost]')
    host = _lease_bring_up(bench, wifi_ap, mode)
    led.phase(host, 0, 'initial', got=None, conn=None)
    mark = _mark(host)
    host.cp.write(f'lease lose {LEASE_HOLD_S}')
    assert eh_test_expect(host.cp, r'\[lease\] lose rc=0', fail=FAIL, timeout=T_IP).ok
    eh_test_expect(host.cp, r'\[lease\] restore', fail=FAIL, timeout=LOST_IP_S + 30)
    led.phase(host, mark, 'lost', got=0, conn=0, disc=0, lost=1, ping=None)
    mark = _mark(host)
    led.phase(host, mark, 'restored', conn=0, disc=0)
    led.verdict()


# ── S60: the CP drops and restores the link itself, same lease ─────────────
CP_CYCLES = 3
CP_HOLD_S = 30


def _lease_of(host, mark=0):
    """The address of the latest got ip since mark."""
    got = re.findall(re.escape(GOT) + r'(\d+\.\d+\.\d+\.\d+)', _since(host, mark))
    return got[-1] if got else None


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', list(MODES))
def test_nwsplit_cp_reconnect_same_lease(bench, wifi_ap, mode):
    """CP code disconnects, sets the same STA config and connects again (a CP
    "set credentials" command). The host app sees each cycle as one drop: one
    disconnect, one connect, one got ip with the same address, no lost ip."""
    led = Ledger(f'[S60 {mode}/cp-reconnect]')
    host = _bring_up(bench, wifi_ap, mode=mode)
    led.phase(host, 0, 'initial', got=None, conn=None)
    lease = _lease_of(host)
    host.cp.write(f'lease reconnect {CP_HOLD_S} {CP_CYCLES}')
    for c in range(1, CP_CYCLES + 1):
        mark = _mark(host)
        r = eh_test_expect(host.cp, rf'\[lease\] reconnect {c} connect rc=0', fail=FAIL,
                           timeout=CP_HOLD_S + T_IP)
        if not r.ok:
            led.bad.append(f'cycle {c}: CP did not reconnect ({r.matched})')
            break
        led.phase(host, mark, f'cycle {c}/{CP_CYCLES}', disc=1, got_ip=lease)
    _check_one_client(led, host, mode)
    led.verdict()


# ── S24: CP deinit/init while the host stays up ────────────────────────────
@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('transport', ['sdio', 'spi_hd', 'uart', 'spi_fd'])
@pytest.mark.parametrize('mode', list(MODES))
def test_nwsplit_cp_deinit_init(bench, wifi_ap, mode, transport):
    """The CP tears its hosted stack down and brings it back; its Wi-Fi stays
    associated, so the app sees no link event and traffic keeps working."""
    led = Ledger(f'[S24 {transport}/{mode}/cp-deinit-init]')
    host = _bring_up(bench, wifi_ap, transport=transport, mode=mode, trigger='stop_start')
    led.phase(host, 0, 'initial', got=None, conn=None)
    mark, cmark = _mark(host), _mark(host.cp)
    host.cp.write('lease deinit_init 3')
    assert eh_test_expect(host.cp, r'\[lease\] init rc=0', fail=FAIL, timeout=T_IP).ok
    led.phase(host, mark, 'after init', got=0, conn=0, lost=0, disc=0)
    # The re-init must re-announce once and the host must take it once.
    cp_seg, h_seg = _since(host.cp, cmark), _since(host, mark)
    hs = {'CP core init': cp_seg.count('ESP-Hosted core initialized'),
          'CP slave-up': cp_seg.count('Send slave up event'),
          'host INIT': len(re.findall(r'eh_init_evt: chip=', h_seg))}
    led.expect(all(n == 1 for n in hs.values()),
               'handshake after re-init: ' + ' '.join(f'{k}={n}' for k, n in hs.items())
               + ' (want 1 each)')
    led.verdict()


# ── S21-S23: host deep sleep, CP keeps the link ────────────────────────────
WAKES = 3
HOST_PS = ['CONFIG_ESP_HOSTED_HOST_FEAT_POWER_SAVE=y',
           'CONFIG_ESP_HOSTED_HOST_FEAT_POWER_SAVE_DEEP_SLEEP_ALLOWED=y']
CP_PS = ['CONFIG_ESP_HOSTED_CP_FEAT_HOST_PS=y',
         'CONFIG_ESP_HOSTED_CP_FEAT_HOST_PS_DEEP_SLEEP=y']


# Every split mode on sdio; one mode on the other wires (one connect per wake even
# where the CP replays twice: D5); plain mode adopts the kept link and runs DHCP (B4).
WAKE_ROWS = [*[(m, 'sdio') for m in MODES],
             *[('app_external', t) for t in ('uart', 'spi_fd', 'spi_hd')],
             ('plain', 'sdio')]


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode,transport', WAKE_ROWS)
def test_nwsplit_host_deep_sleep_wake(bench, wifi_ap, mode, transport):
    """Every wake is a fresh host boot. The CP replays its status; the host must
    report the kept lease once per wake, with no lost ip, and fast: the CP's
    DHCP/DNS status gives the host its address within WAKE_IP_MS of the wake
    (host guest time, 'Host woke up' line to 'got ip' line)."""
    led = Ledger(f'[S21-23 {mode}/{transport}/deep-sleep]')
    plain = mode == 'plain'
    host = _bring_up(bench, wifi_ap, mode='app_external' if plain else mode,
                     transport=transport, host_split=not plain, cp_split=not plain,
                     host_extra=HOST_PS, cp_extra=CP_PS)
    led.phase(host, 0, 'initial', got=None, conn=None)
    for w in range(1, WAKES + 1):
        host.write('host_power_save')
        eh_test_expect(host, r'entering power-save', fail=FAIL, timeout=T_IP)
        time.sleep(3)
        mark = _mark(host)
        host.net.wake()
        if not eh_test_expect(host, r'Host woke up from power save', fail=FAIL,
                              timeout=60).ok:
            led.bad.append(f'wake {w}: host did not wake')
            break
        eh_test_expect(host, r'network-split-iperf>', fail=FAIL, timeout=T_CONSOLE)
        led.phase(host, mark, f'wake {w}/{WAKES}', disc=0)
        seg = _since(host, mark)
        led.expect(re.search(TAG + r'sta connected ssid=\S+ bssid=(?!00:00:00:00:00:00)', seg),
                   f'wake {w}: adopted STA_CONNECTED without the AP info')
        woke = re.search(r'\((\d+)\)[^\n]*Host woke up from power save', seg)
        got = re.search(r'\((\d+)\) ' + re.escape(GOT), seg)
        if woke and got:
            ms = int(got.group(1)) - int(woke.group(1))
            print(f'[S21-23 {mode}] wake {w}: got ip {ms} ms after wake')
            if mode not in ('internal_dhcp', 'plain'):     # host DHCP: no CP-lease fast path
                led.expect(ms <= WAKE_IP_MS, f'wake {w}: got ip {ms} ms after wake (> {WAKE_IP_MS})')
        else:
            led.bad.append(f'wake {w}: no wake/got ip timestamps')
    led.verdict()


# ── B7: the link drops while the host sleeps ───────────────────────────────
CP_NO_LIGHT_SLEEP = 'CONFIG_ESP_HOSTED_CP_LIGHT_SLEEP_POWER_DISABLED=y'


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', ['app_external', 'plain'])
def test_wake_after_link_lost(bench, wifi_ap, mode):
    """B7: CP code drops the link while the host sleeps; nothing reconnects it here.
    The woken app sees a started, unconnected station: STA_START only. The CP stays
    out of light sleep so its console takes the drop."""
    led = Ledger(f'[B7 {mode}/wake-link-lost]')
    plain = mode == 'plain'
    host = _bring_up(bench, wifi_ap, mode='app_external', host_split=not plain, cp_split=not plain,
                     host_extra=HOST_PS,
                     cp_extra=[*CP_PS, CP_NO_LIGHT_SLEEP])
    led.phase(host, 0, 'initial', got=None, conn=None)
    host.write('host_power_save')
    eh_test_expect(host, r'entering power-save', fail=FAIL, timeout=T_IP)
    time.sleep(3)
    mark = _mark(host)
    host.cp.write('lease drop')     # the CP may wake the host to report it
    led.expect(eh_test_expect(host.cp, r'\[lease\] drop rc=', fail=FAIL, timeout=T_IP).ok,
               'CP console did not take the drop')
    if not _wait_for(host, mark, 'Host woke up from power save', 10):
        host.net.wake()
    if _wait_for(host, mark, 'Host woke up from power save', 60):
        eh_test_expect(host, TAG + 'sta start', fail=FAIL, timeout=T_CONSOLE)
        time.sleep(T_NOIP)
        led.phase(host, mark, 'wake, link gone', got=0, conn=0, disc=0, lost=None, ping=None)
    else:
        led.bad.append('host did not wake')
    led.verdict()


# ── B5: plain, the app makes no Wi-Fi call (or init only) across a wake ─────
@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('boot', ['none', 'init'])
def test_plain_wake_app_quiet(bench, wifi_ap, boot):
    """Plain mode, CP code holds the link. A host app that has not started Wi-Fi sees
    nothing at boot nor after a wake (as IDF before esp_wifi_start); its start then
    adopts the link and its DHCP runs: STA_START, STA_CONNECTED, got ip."""
    led = Ledger(f'[B5 plain/wake/{boot}]')
    hook = {'none': BOOT_NO_WIFI, 'init': 'CONFIG_EXAMPLE_BOOT_WIFI_INIT=y'}[boot]
    host = _bring_up(bench, wifi_ap, mode='app_external', host_split=False, cp_split=False,
                     trigger='stop_start', app_netif='none' if boot == 'none' else None,
                     host_extra=[*HOST_PS, hook],
                     cp_extra=CP_PS)
    host.cp.write('lease cp_connect')
    eh_test_expect(host.cp, CP_ASSOC, fail=FAIL, timeout=T_IP)
    time.sleep(T_NOIP)
    led.phase(host, 0, 'boot, cp link', got=0, conn=0, disc=0, ping=None)
    host.write('host_power_save')
    eh_test_expect(host, r'entering power-save', fail=FAIL, timeout=T_IP)
    time.sleep(3)
    mark = _mark(host)
    host.net.wake()
    if not eh_test_expect(host, r'Host woke up from power save', fail=FAIL, timeout=60).ok:
        led.bad.append('host did not wake')
        led.verdict()
    eh_test_expect(host, r'network-split-iperf>', fail=FAIL, timeout=T_CONSOLE)
    time.sleep(T_NOIP)
    led.phase(host, mark, 'wake, quiet', got=0, conn=0, disc=0, ping=None)
    mark = _mark(host)
    fails = _wifi_cmd(host, *(['wifi init'] if boot == 'none' else []), 'wifi start')
    led.expect(not fails, f'init/start calls: {fails}')
    led.phase(host, mark, 'start adopts', got=1, conn=1, disc=0)
    led.verdict()


WAKE_IP_MS = 1500         # HW ~700-800 ms; the DHCP/DNS status fast path must hold


# ── S26: two reconnect owners ──────────────────────────────────────────────
@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', ['app_external', 'internal_static'])
def test_nwsplit_two_reconnect_owners(bench, wifi_ap, mode):
    """Host feature auto-reconnect and the CP example retry both on: the app still
    sees one association and one got ip per drop."""
    led = Ledger(f'[S26 {mode}/two-owners]')
    host = _bring_up(bench, wifi_ap, mode=mode, trigger='two_owners')
    led.phase(host, 0, 'initial', got=None, conn=None)
    _cycles(led, host, 'two_owners', wifi_ap, 3)
    led.verdict()


# ── S27: the app creates WIFI_STA_DEF whenever it likes ────────────────────
NETIF_ADOPTED = 'WIFI_STA_DEF from the app, used as is'
NETIF_CREATED = 'WIFI_STA_DEF created by network split'
NETIF_ORDER = [('internal_static', 'before'), ('internal_static', 'after'),
               ('internal_dhcp', 'before'), ('internal_dhcp', 'after'),
               ('app_external', 'after')]


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode,app_netif', NETIF_ORDER,
                         ids=[f'{m}-{n}' for m, n in NETIF_ORDER])
def test_nwsplit_app_netif_any_order(bench, wifi_ap, mode, app_netif):
    """The app is free to create the default STA netif before or after
    esp_hosted_init(); the library must adopt it, not refuse or crash."""
    led = Ledger(f'[S27 {mode}/app-netif-{app_netif}]')
    host = _bring_up(bench, wifi_ap, mode=mode, app_netif=app_netif, want_net=True)
    led.phase(host, 0, 'initial', got=None, conn=None)
    # One WIFI_STA_DEF, whoever made it first; APP_EXTERNAL never makes one.
    log = _since(host)
    adopted, created = log.count(NETIF_ADOPTED), log.count(NETIF_CREATED)
    led.expect(adopted + created == 1 and not (mode == 'app_external' and created),
               f'netif adopted/created {adopted}/{created}')
    _cycles(led, host, 'cli', wifi_ap, 2)
    # A Wi-Fi deinit/init leaves the netif in place and the split working.
    mark = _mark(host)
    host.write('wifi stop')
    eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
    fails = _wifi_cmd(host, 'wifi deinit', 'wifi init', 'wifi start')
    led.expect(not fails, f'deinit/init calls: {fails}')
    _connect(host, wifi_ap)
    led.phase(host, mark, 'after deinit/init')
    log = _since(host)
    led.expect(log.count(NETIF_ADOPTED) + log.count(NETIF_CREATED) == 1,
               'a second WIFI_STA_DEF appeared after Wi-Fi deinit/init')
    _check_inbound(led, host)
    led.expect(not HOST_ERR.search(_since(host)), 'host nw_split logged an error')
    led.verdict()


@pytest.mark.nw_split
@pytest.mark.wifi
def test_nwsplit_app_netif_after_lease(bench, wifi_ap):
    """B15: APP_EXTERNAL, the CP holds a link and a lease before the app creates
    WIFI_STA_DEF. The app then creates it and starts Wi-Fi, in IDF's order (an
    interface made after esp_wifi_start is never brought up, in IDF too): the start
    adopts the link and the host gets that lease, one connect, one got ip."""
    led = Ledger('[B15 app_external/netif-after-lease]')
    host = _bring_up(bench, wifi_ap, mode='app_external', app_netif='none', trigger='stop_start',
                     host_extra=[BOOT_NO_WIFI])
    eh_test_expect(host.cp, CP_ASSOC, fail=FAIL, timeout=T_IP)
    time.sleep(T_NOIP)
    mark = _mark(host)
    host.write('netif_create')
    eh_test_expect(host, r'netif_create: created', fail=FAIL, timeout=T_CONSOLE)
    fails = _wifi_cmd(host, 'wifi init', 'wifi start')
    led.expect(not fails, f'init/start calls: {fails}')
    led.phase(host, mark, 'netif, then start', got=1, conn=1, disc=0)
    led.verdict()


@pytest.mark.nw_split
@pytest.mark.wifi
def test_nwsplit_app_external_without_netif_fails_loud(bench, wifi_ap):
    """APP_EXTERNAL means the app owns the netif; without one the library must
    say so, not crash."""
    host = _bring_up(bench, wifi_ap, mode='app_external', app_netif='none',
                     wait_console=False)
    r = eh_test_expect(host, r'APP_EXTERNAL: no WIFI_STA_DEF at Wi-Fi start', fail=FAIL,
                       timeout=T_CONSOLE)
    assert r.ok, f'APP_EXTERNAL without WIFI_STA_DEF: no host error ({r.matched})'


# ── S36/S37: the app's own Wi-Fi layer (esp-qa wifi-cmd) ───────────────────
WIFI_CMD_LWIP = {'lwip': 'CONFIG_WIFI_CMD_NO_LWIP=n', 'no_lwip': 'CONFIG_WIFI_CMD_NO_LWIP=y'}


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', list(MODES))
@pytest.mark.parametrize('lwip', list(WIFI_CMD_LWIP))
def test_nwsplit_wifi_cmd_restart(bench, wifi_ap, mode, lwip):
    """`wifi restart` = deinit + init + start through wifi-cmd. With its lwIP
    on, wifi-cmd creates WIFI_STA_DEF itself (a third netif creator, at any
    time); with CONFIG_WIFI_CMD_NO_LWIP=y it leaves the netif alone, as the
    power-save examples ship. Either way the host must reconnect cleanly."""
    led = Ledger(f'[S36/37 {mode}/wifi-cmd-{lwip}]')
    host = _bring_up(bench, wifi_ap, mode=mode, host_extra=[WIFI_CMD_LWIP[lwip]], want_net=True)
    led.phase(host, 0, 'initial', got=None, conn=None)
    for c in range(1, 3):
        mark = _mark(host)
        host.write('wifi restart')
        eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
        _connect(host, wifi_ap)
        led.phase(host, mark, f'restart {c}/2')
    _check_inbound(led, host)
    led.expect(not HOST_ERR.search(_since(host)), 'host nw_split logged an error')
    led.verdict()


# ── S46: host Wi-Fi deinit → init, one call at a time ──────────────────────
WIFI_CMD_DONE = 'DONE.WIFI,'                              # wifi-cmd: one per command
WIFI_CMD_FAILED = re.compile(r'failed:(\w+),(-?\d+)')     # wifi-cmd: a call returned != ESP_OK
INVALID_STATE, NOT_INIT, NOT_STOPPED = 0x103, 0x3001, 0x3003


def _wifi_cmd(host, *cmds):
    """Run wifi-cmd commands one by one; return the (call, rc) of each failure.
    Waits for each command's DONE line: a prompt already in the log ends no wait."""
    mark = _mark(host)
    for cmd in cmds:
        m = _mark(host)
        host.write(cmd)
        end = time.time() + T_CONSOLE
        while WIFI_CMD_DONE not in _since(host, m):
            assert time.time() < end, f'{cmd}: no {WIFI_CMD_DONE}'
            time.sleep(0.1)
    return [(f, int(rc)) for f, rc in WIFI_CMD_FAILED.findall(_since(host, mark))]


@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', list(MODES))
def test_nwsplit_wifi_deinit_init(bench, wifi_ap, mode):
    """Each call returns what plain ESP-IDF returns. A refused call (init while
    inited, deinit while started) changes nothing: the link and data path stay
    up. stop -> deinit -> init -> start -> connect gets the lease back once per
    cycle; calls after deinit return NOT_INIT."""
    led = Ledger(f'[S46 {mode}/deinit-init]')
    host = _bring_up(bench, wifi_ap, mode=mode, want_net=True)
    led.phase(host, 0, 'initial', got=None, conn=None)

    mark = _mark(host)
    fails = _wifi_cmd(host, 'wifi init', 'wifi deinit')
    led.expect(fails == [('wifi_cmd_init_wifi_and_handlers', INVALID_STATE),
                         ('app_wifi_deinit', NOT_STOPPED)], f'refused calls: {fails}')
    led.phase(host, mark, 'refused', got=0, conn=0, disc=0)

    for c in range(1, 3):
        mark = _mark(host)
        host.write('wifi stop')
        eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
        fails = _wifi_cmd(host, 'wifi deinit', 'wifi deinit', 'wifi start',
                          'wifi init', 'wifi start')
        led.expect(fails == [('app_wifi_deinit', NOT_INIT), ('esp_wifi_start', NOT_INIT)],
                   f'cycle {c} calls: {fails}')
        _connect(host, wifi_ap)
        led.phase(host, mark, f'cycle {c}/2')
    _check_inbound(led, host)
    led.expect(not HOST_ERR.search(_since(host)), 'host nw_split logged an error')
    led.verdict()


# ── S42: config set while Wi-Fi is stopped ─────────────────────────────────
@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('split', ['split_off', 'split_on'])
def test_set_config_while_stopped(bench, wifi_ap, split):
    """esp_wifi_stop -> esp_wifi_set_config -> esp_wifi_connect -> esp_wifi_start, judged
    from the host app as if it were alone (idf-measured M3, R13): the config is taken
    while stopped (the CP refused it once: ESP_ERR_WIFI_NOT_STARTED); a connect while
    stopped returns ESP_ERR_WIFI_NOT_STARTED and starts nothing. Network split: CP code
    may take the radio meanwhile; the stopped host app still gets NOT_STARTED and sees
    none of that link until its own start, which adopts it (one connect, one got ip).
    Without split: the start associates nothing; the app's connect associates once."""
    on = split == 'split_on'
    led = Ledger(f'[S42 set-config-while-stopped/{split}]')
    host = _bring_up(bench, wifi_ap, mode='app_external', host_split=on, cp_split=on)
    led.phase(host, 0, 'initial', got=None, conn=None)
    mark, cp_mark = _mark(host), _mark(host.cp)
    host.write('wifi stop')
    eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
    time.sleep(2)
    _connect(host, wifi_ap)          # set_config + connect while stopped
    time.sleep(3)
    led.expect(not re.search(r'req_wifi_set_config.*failed', _since(host.cp, cp_mark)),
               'set_config refused while stopped')
    led.expect('WIFI_CONNECT_START,FAIL.12290' in _since(host, mark),
               'connect while stopped did not return ESP_ERR_WIFI_NOT_STARTED')
    if on:
        held = _mark(host)
        host.cp.write('lease cp_connect')      # CP code starts the radio and connects
        eh_test_expect(host.cp, r'\[lease\] cp_connect connect rc=0', fail=FAIL, timeout=T_IP)
        eh_test_expect(host.cp, CP_ASSOC, fail=FAIL, timeout=T_IP)
        _connect(host, wifi_ap)                # the host app is still stopped
        time.sleep(T_NOIP)
        seg = _since(host, held)
        led.expect('WIFI_CONNECT_START,FAIL.12290' in seg,
                   'stopped host app: connect while CP code runs the radio was not NOT_STARTED')
        led.expect(not any(e in seg for e in (CONN, DISC, GOT)),
                   'stopped host app saw the link CP code made')
    start = _mark(host)
    host.write('wifi start')
    eh_test_expect(host, TAG + 'sta start', fail=FAIL, timeout=T_IP)
    if on:
        led.phase(host, start, 'start adopts', got=1, conn=1, disc=0, lost=None)
        _cp_switch(led, host)
    else:
        time.sleep(T_NOIP)
        led.expect(CONN not in _since(host, start), 'associated at the start without a connect')
        _connect(host, wifi_ap)
        led.phase(host, start, 'connect', got=1, conn=1, disc=0, lost=None)
        _plain_cp_code_link(led, host)
    led.verdict()


def _plain_cp_code_link(led, host):
    """Plain mode, CP code uses the radio: A4 it drops the host app's link, A2 it
    connects while the host app is started (adopted), A3 it connects while the host
    app is stopped (nothing until the app's start, which adopts it)."""
    mark = _mark(host)
    host.cp.write('lease drop')
    eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
    time.sleep(T_NOIP)
    led.phase(host, mark, 'A4 cp drops', got=0, conn=0, disc=1, lost=None, ping=None)
    mark = _mark(host)
    host.cp.write('lease cp_connect')
    led.phase(host, mark, 'A2 cp connects', got=1, conn=1, disc=0, lost=None)
    host.write('wifi stop')
    eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
    held = _mark(host)
    host.cp.write('lease cp_connect')
    eh_test_expect(host.cp, r'\[lease\] cp_connect connect rc=0', fail=FAIL, timeout=T_IP)
    eh_test_expect(host.cp, CP_ASSOC, fail=FAIL, timeout=T_IP)
    time.sleep(T_NOIP)
    led.expect(not any(e in _since(host, held) for e in (CONN, DISC, GOT)),
               'A3: stopped host app saw the link CP code made')
    start = _mark(host)
    host.write('wifi start')
    eh_test_expect(host, TAG + 'sta start', fail=FAIL, timeout=T_IP)
    led.phase(host, start, 'A3 start adopts', got=1, conn=1, disc=0, lost=None)
    _cp_switch(led, host)


def _cp_switch(led, host):
    """A14: CP code moves the link to another config (the AP's BSSID pinned): the
    host app sees the drop and the new link, as for its own switch."""
    mark = _mark(host)
    host.cp.write('lease switch')
    eh_test_expect(host.cp, r'\[lease\] switch connect rc=', fail=FAIL, timeout=T_IP)
    led.phase(host, mark, 'A14 cp switch', got=None, conn=1, disc=1, lost=None)


# ── S45: the host disconnects; CP code that retries must not undo it ────────
@pytest.mark.nw_split
@pytest.mark.wifi
@pytest.mark.parametrize('mode', ['app_external', 'internal_static'])
def test_host_disconnect_stops_cp_retry(bench, wifi_ap, mode):
    """The CP example retries on STA_DISCONNECTED (esp_wifi_connect from CP code).
    A disconnect the host asked for wins: the link stays down until the host
    connects again, then comes back once."""
    led = Ledger(f'[S45 {mode}/host-disconnect-vs-cp-retry]')
    host = _bring_up(bench, wifi_ap, mode=mode, trigger='cli', cp_extra=[CP_APP_RETRY])
    led.phase(host, 0, 'initial', got=None, conn=None)
    mark = _mark(host)
    host.write('sta_disconnect')
    eh_test_expect(host, DISC, fail=FAIL, timeout=T_IP)
    time.sleep(LOST_IP_S + 10)
    led.phase(host, mark, 'down 20s', got=0, conn=0, disc=1, lost=1, ping=False)
    mark = _mark(host)
    _connect(host, wifi_ap)
    led.phase(host, mark, 'reconnect', disc=0)
    led.verdict()
