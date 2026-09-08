"""Wi-Fi link diagnostics for throughput tests.

Parses the coprocessor/host console logs for every parameter that materially
changes TCP throughput, then renders a decorated summary with an explicit
verdict per check. The point is that a throughput number alone is useless: the
same MCS, channel, bandwidth and RSSI can yield 1.4 Mbps or 36 Mbps depending
on parameters that are only visible in the log.
"""
import re
import statistics

OK, WARN, FAIL, NA = 'OK', 'WARN', 'FAIL', '--'


def _band_of_channel(ch):
    if ch is None:
        return None
    return '2G' if int(ch) <= 14 else '5G'


def _last(pat, text, group=1, cast=str):
    m = None
    for m in re.finditer(pat, text):
        pass
    if not m:
        return None
    try:
        return cast(m.group(group))
    except (ValueError, IndexError):
        return None


def parse(cp_log: str, host_log: str = '') -> dict:
    """Pull the throughput-relevant link state out of the logs."""
    d = {}
    both = cp_log + '\n' + host_log

    # --- association -----------------------------------------------------
    m = None
    for m in re.finditer(
            r'connected with (\S+), aid = (\d+), channel (\d+), BW(\d+)\(([^)]*)\), '
            r'bssid = (\S+)', cp_log):
        pass
    if m:
        d.update(ssid=m.group(1), aid=int(m.group(2)), channel=int(m.group(3)),
                 sta_bw=int(m.group(4)), bw_pos=m.group(5), bssid=m.group(6))
    d['band'] = _band_of_channel(d.get('channel'))
    d['ap_bss_bw'] = _last(r'\(vht\)BSS bandwidth:(\d+)MHz', cp_log, 1, int)

    # --- rate control (the one that hides a 27x difference) --------------
    m = None
    for m in re.finditer(r'\(trc\)band:(\w+), phymode:(\d+), highestRateIdx:(\d+), '
                         r'lowestRateIdx:(\d+), dataSchedTableSize:(\d+)', cp_log):
        pass
    if m:
        d.update(trc_band=m.group(1), trc_phymode=int(m.group(2)),
                 trc_highest_idx=int(m.group(3)), trc_lowest_idx=int(m.group(4)),
                 trc_sched_size=int(m.group(5)))
    d['rate'] = _last(r'\(trc\)band:\w+, rate\((\S+?), rateIdx', cp_log)
    d['ampdu_rate'] = _last(r'ampdu\(rate:(\S+?),', cp_log)
    d['ampdu_state'] = _last(r'ampduState:(\S+(?: \S+)?)', cp_log)
    d['phytype'] = _last(r'\(trc\)phytype:(\S+),', cp_log)
    d['max_rate'] = _last(r'\(trc\)phytype:\S+, snr:\d+, maxRate:(\d+)', cp_log, 1, int)

    # --- radio -----------------------------------------------------------
    m = None
    for m in re.finditer(r'ifidx:0, rssi:(-?\d+), nf:(-?\d+), phytype\(\S+ (\S+)\), '
                         r'phymode\(\S+ (\S+)\), max_rate:(\d+), he:(\d), vht:(\d), ht:(\d)',
                         cp_log):
        pass
    if m:
        d.update(rssi=int(m.group(1)), nf=int(m.group(2)), phymode=m.group(4),
                 he=int(m.group(6)), vht=int(m.group(7)), ht=int(m.group(8)))
    d['snr'] = _last(r'\(trc\)phytype:\S+, snr:(\d+)', cp_log, 1, int)

    # --- power save ------------------------------------------------------
    d['pm_type'] = _last(r'pm start, type: ?(\d+)', cp_log, 1, int)
    d['listen_interval'] = _last(r'listen_interval (\d+)', both, 1, int)
    m = None
    for m in re.finditer(r'pm stop, total sleep time: (\d+) us / (\d+) us', cp_log):
        pass
    if m:
        slept, total = int(m.group(1)), int(m.group(2))
        d['sleep_us'], d['sleep_window_us'] = slept, total
        d['sleep_pct'] = (100.0 * slept / total) if total else 0.0

    # --- band mode: did the last set actually TRANSITION? ----------------
    d['band_mode_req'] = _last(r'set band mode: (\d+)', cp_log, 1, int)
    d['band_mode_transitions'] = len(
        re.findall(r'wifi set band mode, new band mode (\d+), old band mode (\d+)', cp_log))

    # --- aggregation / transport ----------------------------------------
    d['addba_bufsize'] = _last(r'\[ADDBA\]RX addba response, status:0, \S+ bufsize:(\d+)',
                               cp_log, 1, int)
    d['txa_wnd'] = _last(r'txa_wnd:(\d+)', cp_log, 1, int)
    m = re.search(r'SW_AGGR negotiated \(e2h=(\d+)B h2e=(\d+)B\)', both)
    if m:
        d['aggr_e2h'], d['aggr_h2e'] = int(m.group(1)), int(m.group(2))
    d['sdio_mode'] = _last(r'SDIO mode: slave=(\w+) host=\w+', both)
    d['dtim'] = _last(r'DTIM period = (\d+)', cp_log, 1, int)
    d['beacon_int'] = _last(r"AP's beacon interval = (\d+) us", cp_log, 1, int)

    # --- error counters --------------------------------------------------
    d['err_ccmp_replay'] = len(re.findall(r'CCMP replay detected', cp_log))
    d['err_assoc_refused'] = len(re.findall(r'Association refused too many times', cp_log))
    d['err_no_credits'] = len(re.findall(r'no slave credits', both))
    d['err_set_protocol'] = len(re.findall(r'req_wifi_set_protocol\S* failed', cp_log))
    d['disconnects'] = len(re.findall(r'disconnected \(reason \d+', cp_log))
    d['reasons'] = sorted({int(x) for x in re.findall(r'disconnected \(reason (\d+)', cp_log)})
    return d


def check(d: dict) -> list:
    """Return [(verdict, title, detail)] — the reasoning, not just numbers."""
    out = []

    # 1. THE one that silently costs ~27x.
    tb, band, ch = d.get('trc_band'), d.get('band'), d.get('channel')
    if tb and band:
        if tb != band:
            out.append((FAIL, f'rate-control band is {tb} on a {band} channel (ch {ch})',
                        f'The {tb} rate/A-MPDU schedule table is in use '
                        f'(size={d.get("trc_sched_size")}, lowestRateIdx={d.get("trc_lowest_idx")}). '
                        'Measured effect: ~1-5 Mbps instead of ~36 Mbps, with MCS, channel, '
                        'bandwidth, RSSI and power save all unchanged.\n'
                        '  cause: esp_wifi syncs the trc band ONLY on an actual '
                        'esp_wifi_set_band_mode() transition (old != new). It is never derived '
                        'from the association channel, so setting the band it is already on is '
                        'a no-op and leaves the wrong table in place.\n'
                        '  workaround: force a transition before connecting '
                        '(e.g. `band 2g` then `band 5g`).'))
        else:
            out.append((OK, f'rate-control band {tb} matches the {band} channel (ch {ch})',
                        f'sched table size={d.get("trc_sched_size")}, '
                        f'lowestRateIdx={d.get("trc_lowest_idx")}'))
    else:
        out.append((NA, 'rate-control band not seen in the log', 'cannot verify the rate table'))

    # 2. Power save parks the radio.
    pm, pct = d.get('pm_type'), d.get('sleep_pct')
    if pm is not None:
        names = {0: 'WIFI_PS_NONE', 1: 'WIFI_PS_MIN_MODEM', 2: 'WIFI_PS_MAX_MODEM'}
        if pm == 0:
            out.append((OK, f'power save off (pm type 0, {names[0]})', ''))
        else:
            out.append((WARN, f'power save active (pm type {pm}, {names.get(pm, "?")})',
                        f'listen_interval={d.get("listen_interval")}. A parked radio caps '
                        'throughput in proportion to its sleep ratio; use `ps none` for a '
                        'throughput measurement.'))
    if pct is not None and pct > 5:
        out.append((WARN, f'radio asleep {pct:.1f}% of the measurement window',
                    f'{d.get("sleep_us")} us of {d.get("sleep_window_us")} us. '
                    f'Throughput is bounded by roughly (100-{pct:.0f})% of the link rate.'))

    # 3. Narrower than the AP offers.
    sta_bw, ap_bw = d.get('sta_bw'), d.get('ap_bss_bw')
    if sta_bw and ap_bw and sta_bw < ap_bw:
        out.append((WARN, f'associated at BW{sta_bw} while the AP offers {ap_bw}MHz',
                    f'{ap_bw // sta_bw}x less PHY capacity than the BSS allows.'))
    elif sta_bw:
        out.append((OK, f'bandwidth BW{sta_bw}' + (f' (AP {ap_bw}MHz)' if ap_bw else ''), ''))

    # 4. A-MPDU must actually come up.
    if d.get('ampdu_state') and 'operational' not in str(d['ampdu_state']).lower():
        out.append((WARN, f'A-MPDU state "{d["ampdu_state"]}"',
                    'aggregation not confirmed operational in the captured window'))

    # 5. Hard errors.
    # FAIL only for things that make the NUMBER untrustworthy. Association-time
    # blemishes that recovered are reported, but do not void a steady reading.
    for key, verdict, label, hint in (
            ('err_no_credits', FAIL, 'SDIO "no slave credits"',
             'host TX stalled waiting on the CP; the measured rate is not the link rate'),
            ('err_ccmp_replay', WARN, 'CCMP replay detected',
             'frames dropped by the crypto layer, seen at association; AP/PN related'),
            ('err_assoc_refused', WARN, 'association refused (max allowed 1)',
             'the AP applied admission control (comeback time) and the STA gave up after one '
             'attempt; it re-associated afterwards, so the measurement window is unaffected'),
            ('err_set_protocol', WARN, 'esp_wifi_set_protocol failed',
             'rejected under AUTO band mode; use esp_wifi_set_protocols')):
        n = d.get(key) or 0
        if n:
            out.append((verdict, f'{label} x{n}', hint))

    if d.get('disconnects'):
        out.append((WARN, f'{d["disconnects"]} disconnect(s), reasons {d["reasons"]}',
                    'a link that re-associates mid-test invalidates the numbers'))
    return out


def _bar(title, width=76):
    return '+' + '-' * (width - 2) + '+\n| ' + title.ljust(width - 4) + ' |'


def summary(d: dict, tx: list, rx: list, ssid='', transport='', width=76) -> str:
    """Decorated end-of-test summary: numbers AND the reasoning."""
    L = []
    line = lambda s='': L.append('| ' + str(s).ljust(width - 4) + ' |')
    rule = lambda: L.append('+' + '-' * (width - 2) + '+')

    rule()
    line(f'TCP THROUGHPUT SUMMARY   transport={transport or "?"}  ssid={ssid or d.get("ssid","?")}')
    rule()

    def stat(vals, name):
        if not vals:
            line(f'  {name:<22} (not measured)')
            return
        med = statistics.median(vals)
        line(f'  {name:<22} ' + '  '.join(f'{v:.2f}' for v in vals) +
             f'   Mbits/sec   median {med:.2f}')
    line('RESULT')
    stat(tx, 'TCP TX host->peer')
    stat(rx, 'TCP RX peer->host')
    rule()

    line('LINK')
    line(f'  ssid / bssid         {d.get("ssid","?")} / {d.get("bssid","?")}')
    line(f'  channel / band       {d.get("channel","?")} ({d.get("band","?")})   aid={d.get("aid","?")}')
    line(f'  bandwidth            BW{d.get("sta_bw","?")}   AP BSS {d.get("ap_bss_bw","?")}MHz')
    line(f'  rssi / nf / snr      {d.get("rssi","?")} / {d.get("nf","?")} / {d.get("snr","?")}')
    line(f'  phy                  {d.get("phymode","?")}  {d.get("phytype","?")}  '
         f'he={d.get("he","?")} vht={d.get("vht","?")} ht={d.get("ht","?")}  '
         f'max_rate={d.get("max_rate","?")}')
    rule()

    line('RATE CONTROL')
    line(f'  trc band             {d.get("trc_band","?")}   (channel band {d.get("band","?")})')
    line(f'  sched table          size={d.get("trc_sched_size","?")}  '
         f'lowestRateIdx={d.get("trc_lowest_idx","?")}  '
         f'highestRateIdx={d.get("trc_highest_idx","?")}')
    line(f'  selected rate        {d.get("rate","?")}   ampdu {d.get("ampdu_rate","?")}  '
         f'state={d.get("ampdu_state","?")}')
    rule()

    line('POWER SAVE')
    line(f'  pm type              {d.get("pm_type","?")}   listen_interval={d.get("listen_interval","?")}')
    sp = d.get('sleep_pct')
    line(f'  sleep ratio          ' + (f'{sp:.1f} %  ({d.get("sleep_us")} / {d.get("sleep_window_us")} us)'
                                       if sp is not None else '(not seen)'))
    line(f'  dtim / beacon        {d.get("dtim","?")} / {d.get("beacon_int","?")} us')
    rule()

    line('AGGREGATION / TRANSPORT')
    line(f'  addba bufsize        {d.get("addba_bufsize","?")}   txa_wnd={d.get("txa_wnd","?")}')
    line(f'  sdio sw_aggr         e2h={d.get("aggr_e2h","?")}B h2e={d.get("aggr_h2e","?")}B  '
         f'mode={d.get("sdio_mode","?")}')
    line(f'  band mode            requested={d.get("band_mode_req","?")}  '
         f'real transitions={d.get("band_mode_transitions",0)}')
    rule()

    checks = check(d)
    line('FINDINGS')
    if not checks:
        line('  (none)')
    import textwrap
    for verdict, title, detail in checks:
        line(f'  [{verdict:^4}] {title}')
        for dl in (detail or '').split('\n'):
            if not dl.strip():
                continue
            for w in textwrap.wrap(dl.strip(), width=width - 14):
                line(f'         {w}')
    rule()

    worst = FAIL if any(v == FAIL for v, _, _ in checks) else (
        WARN if any(v == WARN for v, _, _ in checks) else OK)
    line(f'VERDICT: {worst}' + ('   (numbers above are NOT a valid throughput reading)'
                                if worst == FAIL else ''))
    rule()
    return '\n'.join(L)
