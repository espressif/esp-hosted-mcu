# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Event-bus reporter — the transparency/responsiveness seam.

The engine (pytest) emits a typed event stream; reporters consume it. Per run
(`runs/<ts>/`) this plugin writes:
  * `events.jsonl` — machine stream any sink can tail (WS/UI).
  * `live.log`     — HUMAN, tailable line-per-event (▶ start / ✓ pass / ✗ fail),
                     so even under `--jobs` (where xdist hides the live step
                     trace) you can `tail -f` it to see exactly what's running
                     and what's stuck.
  * `report.md`    — on finish: summary + mermaid timeline.

Events: session_start · test_start · test_end{outcome,duration} · session_end.
"""
import json
import os
import sys
import time
from pathlib import Path

from infra import lab

_RUNS = lab.RUNS  # tests/.work/runs
_MARK = {"passed": "✓ PASS", "failed": "✗ FAIL", "skipped": "⚪ SKIP"}


class _Bus:
    def __init__(self):
        self.dir = None
        self.fh = None       # events.jsonl
        self.live = None     # human live.log
        self.events = []
        self.t0 = None
        self.console = False  # mirror per-test lines to the console (batch mode)
        self.regression = False  # full-matrix run (--regression) vs default sanity

    def emit(self, **ev):
        ev["t"] = time.time()
        self.events.append(ev)
        if self.fh:
            self.fh.write(json.dumps(ev) + "\n")
            self.fh.flush()

    def line(self, msg):
        stamped = f"{time.strftime('%H:%M:%S')}  {msg}\n"
        if self.live:
            self.live.write(stamped)
            self.live.flush()
        if self.console:
            # One atomic line per event, emitted only by the single xdist
            # controller — so under --jobs the per-test names/results show live
            # WITHOUT the interleaved multi-line spew of N parallel workers.
            sys.stderr.write(stamped)
            sys.stderr.flush()


_bus = _Bus()


def pytest_configure(config):
    # Only the xdist controller (no workerinput) owns the stream/report; workers
    # forward their reports to it, so we still see every test exactly once.
    if hasattr(config, "workerinput"):
        return
    # Retention: prune runs/ + cache/ past their expiry (default 30d,
    # $EH_RETENTION_DAYS) before this run's dir is created — the "scan on startup".
    try:
        n = lab.sweep_expired()
        if n:
            print(f"[eh] retention: pruned {n} expired run/cache dir(s) "
                  f"(older than {lab.retention_days():.0f}d; set EH_RETENTION_DAYS to change)")
    except Exception:  # noqa: BLE001 — housekeeping must never break a run
        pass
    # F3: state the disk budget up front and escalate once now, so a run that cannot
    # possibly finish says so at second 0. This only WARNS — the hard stop belongs at
    # the byte-consuming step (build / cow_flash), where it can name what it was doing.
    try:
        free = lab.free_gb()
        floor = lab.disk_floor_gb()
        if floor > 0 and free < floor * 2:
            try:
                free = lab.ensure_free("session start")
                print(f"[eh] disk: {free:.0f}GB free after pruning "
                      f"(floor {floor:.0f}GB) — tight; builds may be evicting cache")
            except lab.DiskExhausted as e:
                print(f"[eh] disk: {e}")
        lab.enforce_cache_budget()
    except Exception:  # noqa: BLE001
        pass
    # eh.py pins one run dir for the controller + all xdist workers (so the live
    # log and per-test log archive co-locate); fall back to a fresh ts dir when
    # pytest is invoked directly.
    import os
    env_dir = os.environ.get("EH_RUN_DIR")
    ts = Path(env_dir).name if env_dir else time.strftime("%Y%m%d-%H%M%S")
    _bus.dir = Path(env_dir) if env_dir else _RUNS / ts
    _bus.dir.mkdir(parents=True, exist_ok=True)
    _bus.fh = open(_bus.dir / "events.jsonl", "w")
    # Append (not truncate): xdist workers append their step trace to this same
    # file (see infra.eh_logcap); O_APPEND keeps single-line writes atomic across
    # processes. The run dir is unique per run, so there's nothing to truncate.
    _bus.live = open(_bus.dir / "live.log", "a")
    _bus.t0 = time.time()
    # Batch/parallel run (xdist -n N) hides the per-step log_cli trace, so mirror
    # the per-test start/result lines to the console (see _Bus.line). Sequential
    # runs already stream the full trace, so leave console off to avoid dup output.
    _bus.console = bool(getattr(config.option, "numprocesses", 0) or 0)
    _bus.regression = bool(config.getoption("regression", default=False))
    # F4: sample the BOX through the whole run (free RAM, PSI, load, our share vs
    # others) so a duration can be read against the conditions that produced it —
    # wall-clock alone cannot distinguish "fast" from "had the box to itself".
    # Only sample here when NOT supervised: eh.py's supervisor owns the loop (it can
    # act on what it sees). A direct `pytest` run has no supervisor, so fall back.
    try:
        import threading
        if os.environ.get("EH_SUPERVISED") == "1":
            raise RuntimeError("supervised")

        def _sampler():
            # 2s control tick (free: 0.03ms of /proc reads); a full attribution row
            # every 15th tick, carrying the peaks seen in between.
            i = 0
            while not _bus.stop_sampling.wait(2):
                try:
                    lab.sample_fast()
                except Exception:  # noqa: BLE001
                    pass
                i += 1
                if i % 15 == 0:
                    lab.sample_resources(_bus.dir)
        _bus.stop_sampling = threading.Event()
        lab.sample_resources(_bus.dir)          # t=0 sample
        t = threading.Thread(target=_sampler, daemon=True)
        t.start()
    except Exception:  # noqa: BLE001 — instrumentation must never break a run
        pass
    _bus.emit(event="session_start", ts=ts)
    _bus.line("session start")


def pytest_report_header(config):
    # Shows at the top of every run so the user knows where to watch — even
    # under --jobs, where the per-step [build]/[expect]/[cmd] trace is hidden.
    if _bus.dir:
        return (f"eh: live progress → tail -f {_bus.dir}/live.log   "
                f"(per-step trace shows inline when run WITHOUT --jobs)")
    return None


def pytest_runtest_logstart(nodeid, location):
    if _bus.fh:
        _bus.emit(event="test_start", nodeid=nodeid)
        _bus.line(f"▶ START {nodeid}")


def pytest_runtest_logreport(report):
    if not _bus.fh:
        return
    if report.when == "call" or (report.when == "setup" and report.outcome != "passed"):
        dur = round(getattr(report, "duration", 0.0), 3)
        # Marker names ride in report.keywords; stamp the two lifecycle markers on the
        # event so any consumer (matrix, eh.py second_chance merge) can classify the
        # outcome without re-collecting.
        kw = getattr(report, "keywords", {})
        _bus.emit(event="test_end", nodeid=report.nodeid, outcome=report.outcome,
                  duration=dur, when=report.when,
                  allow_fail=("allow_fail" in kw), second_chance=("second_chance" in kw))
        msg = f"{_MARK.get(report.outcome, report.outcome)} {report.nodeid} ({dur:.1f}s)"
        if report.outcome == "failed":
            # Point at the archived per-test logs so a failure can be assessed
            # mid-run without hunting — cp/host build + runtime logs live here.
            msg += f"  → {_bus.dir}/logs/{lab.sanitize(report.nodeid)}.FAIL/"
        _bus.line(msg)


def pytest_sessionfinish(session, exitstatus):
    if not _bus.fh:
        return
    _bus.emit(event="session_end", exitstatus=int(exitstatus))
    _bus.line(f"session end (exit {int(exitstatus)})")
    # Under eh.py's two-phase orchestration the merged (post-second_chance) matrix is
    # rendered by eh.py after both phases; skip the per-invocation one so a soon-to-be-
    # recovered flake isn't shown as a hard ✗. Direct pytest runs still print here.
    import os
    if not os.environ.get("EH_DEFER_MATRIX"):
        _print_matrix()
    _write_report()
    _bus.fh.close()
    if _bus.live:
        _bus.live.close()
    print(f"\n[eh] run trace: {_bus.dir}/report.md   live: {_bus.dir}/live.log   "
          f"events: {_bus.dir}/events.jsonl")


_TRANSPORTS = ["sdio", "spi_fd", "spi_hd", "uart"]
_ANSI = {"grey": "\033[90m", "green": "\033[32m", "red": "\033[31m",
         "yellow": "\033[33m", "bold": "\033[1m", "off": "\033[0m"}


def _scen_col(nodeid):
    """(scenario, transport-column) for a nodeid. The transport is the whole param
    (sdio/uart/spi_fd/spi_hd[_N]) or the suffix of a compound id like
    "<example>-<wire>" (the boot cases, e.g. network_split_iperf-uart). spi_hd_{1,2,4}
    fold to spi_hd; a non-transport param (rep number / none / linux) sits under
    sdio, the default emu wire."""
    tail = nodeid.split("::")[-1].split("@")[0]
    fn, _, rest = tail.partition("[")
    param = rest.rstrip("]").lower()
    tok = param.rsplit("-", 1)[-1] if "-" in param else param
    if tok in ("sdio", "uart", "spi_fd"):
        col = tok
    elif tok.startswith("spi_hd"):
        col = "spi_hd"
    else:
        col = "sdio"
    if param and "boot" in fn:              # keep each example boot as its own row
        fn = f"{fn}[{param}]"
    return fn, col


# Effective per-cell STATES (outcome after applying the two lifecycle markers):
#   passed / skipped / failed — as before
#   tolerated — an allow_fail test that ran & FAILED: red ⊗ (distinct from a hard ✗),
#               non-blocking — "ran, failed, fix later"
#   recovered — a second_chance test that failed in parallel then PASSED isolated:
#               green ↻ (flaky-but-green) — distinct from a clean ✓
#   hung_recovered — a test that DID NOT RUN in the parallel phase (its worker was
#               killed/wedged) then PASSED isolated: yellow ⤴. A hang is a MORE
#               serious signal than a load flake, so it never renders as a clean ✓
#               or a flake ↻ — the wedge must stay visible even though it recovered.
_STATE_GLYPH = {"passed": ("green", "✓"), "failed": ("red", "✗"),
                "skipped": ("yellow", "–"), "tolerated": ("red", "⊗"),
                "recovered": ("green", "↻"), "hung_recovered": ("yellow", "⤴")}
# Cell-fold severity when several params collapse onto one (scenario,transport) cell:
# a hard fail dominates a tolerated fail dominates a hung-recovery dominates skip
# dominates a flake recovery / pass.
_STATE_RANK = {"passed": 0, "recovered": 1, "skipped": 2, "hung_recovered": 3,
               "tolerated": 4, "failed": 5}


def _state_of(outcome, allow_fail=False, recovered=False, hung_recovered=False):
    """Map a raw outcome + markers to an effective matrix state."""
    if hung_recovered:
        return "hung_recovered"
    if recovered:
        return "recovered"
    if outcome == "failed":
        return "tolerated" if allow_fail else "failed"
    return outcome  # passed / skipped


def _grid_from_states(items):
    """items: iterable of (nodeid, state) → (grid{scen:{col:state}}, ordered scen list)."""
    grid, order = {}, []
    for nid, st in items:
        scen, col = _scen_col(nid)
        row = grid.setdefault(scen, {})
        if scen not in order:
            order.append(scen)
        if col not in row or _STATE_RANK[st] > _STATE_RANK[row[col]]:
            row[col] = st
    return grid, order


def _render_matrix(grid, order, regression):
    color = sys.stderr.isatty()

    def c(txt, key):
        return f"{_ANSI[key]}{txt}{_ANSI['off']}" if color else txt

    def cell(state):
        # fixed 8-wide column; visible content is 2 glyphs, centred by hand so the
        # ANSI codes (zero visible width) don't skew Python's str formatting.
        if state is None:
            return " " * 8
        col_key, g = _STATE_GLYPH[state]
        return "   " + c("✓", "grey") + c(g, col_key) + "   "  # grey ran-tick + result

    wscen = max([len("scenario")] + [len(s) for s in order]) + 2
    title = "Regression matrix" if regression else "Sanity test performed"
    hdr = f"{'scenario':<{wscen}}" + "".join(f"{t:^8}" for t in _TRANSPORTS)
    lines = ["", c(f"{title}  (grey ✓ = ran · green ✓ pass · red ✗ fail · yellow – skip · "
                   f"red ⊗ allow_fail · green ↻ recovered 2nd-chance · yellow ⤴ hung-recovered)", "bold"),
             hdr, "─" * (wscen + 8 * len(_TRANSPORTS))]
    for scen in order:
        cells = "".join(cell(grid[scen].get(t)) for t in _TRANSPORTS)
        lines.append(f"{scen:<{wscen}}{cells}")
    out = "\n".join(lines) + "\n"
    sys.stderr.write(out)
    sys.stderr.flush()
    if _bus.live and not _bus.live.closed:
        import re
        _bus.live.write(re.sub(r"\033\[[0-9]+m", "", out))
        _bus.live.flush()


def _print_matrix():
    """End-of-run coverage matrix: scenarios × transports. Each cell that ran gets
    a grey ✓ (ran) followed by the outcome glyph. Shows at a glance that the set
    touches every wire without duplicating."""
    ends = [e for e in _bus.events if e["event"] == "test_end"]
    if not ends:
        return
    items = [(e["nodeid"], _state_of(e["outcome"], e.get("allow_fail", False)))
             for e in ends]
    grid, order = _grid_from_states(items)
    _render_matrix(grid, order, _bus.regression)


def _load_ends(run_dir):
    """Read the test_end events out of a run's events.jsonl (controller-owned, one
    per test). Tolerant of a missing/partial file so a merge never crashes a run."""
    p = Path(run_dir) / "events.jsonl"
    out = []
    if not p.exists():
        return out
    for line in p.read_text().splitlines():
        try:
            ev = json.loads(line)
        except ValueError:
            continue
        if ev.get("event") == "test_end":
            out.append(ev)
    return out


def _load_collected(run_dir, key):
    """Expected-to-run nodeids (conftest-written collected.txt), keyed the same way as
    results so the two sets compare. Empty set if absent (pre-feature or non-eh runs)."""
    p = Path(run_dir) / "collected.txt"
    if not p.exists():
        return set()
    return {key(ln.strip()) for ln in p.read_text().splitlines() if ln.strip()}


def summarize(run_dir, retry_dir=None, regression=False, elapsed=None, prewarm=None, hung=None):
    """eh.py's authoritative post-run render: merge the parallel phase (run_dir) with
    the optional isolated second_chance re-run (retry_dir, whose result is FINAL),
    print the merged matrix + a grouped truthful summary, and return the failure
    classification so eh.py can compute the pipeline exit code:

        {"recovered": [...], "allow_fail": [...], "blocking": [...]}

    `hung` = nodeids that DID NOT RUN in the parallel phase (killed with a wedged
    worker). One that then PASSES isolated is a hang-recovery (yellow ⤴), reported
    distinctly from an ordinary parallel-load flake (↻) so a wedge is never invisible.

    Exit is non-zero iff 'blocking' is non-empty (a FINAL failure that is not allow_fail)."""
    def _key(n):
        # xdist_group runs carry a "nodeid@group" display suffix; the -n0 retry
        # emits the bare nodeid. Key both phases on the bare id or the retry's
        # pass can never flip the parallel-phase failure it recovered.
        i = n.rfind("@")
        return n[:i] if i > n.rfind("]") else n

    hung = {_key(n) for n in (hung or [])}
    final = {}
    for e in _load_ends(run_dir):
        final[_key(e["nodeid"])] = {"nodeid": e["nodeid"], "outcome": e["outcome"],
                              "allow_fail": e.get("allow_fail", False),
                              "second_chance": e.get("second_chance", False),
                              "recovered": False, "hung_recovered": False}
    if retry_dir:
        for e in _load_ends(retry_dir):
            k = _key(e["nodeid"])
            f = final.setdefault(k, {"nodeid": e["nodeid"],
                                               "allow_fail": e.get("allow_fail", False),
                                               "second_chance": True, "recovered": False,
                                               "hung_recovered": False})
            # the isolated re-run is the FINAL word. A failed→passed flip is a flake
            # "recovered"; a did-not-run(hung)→passed is a "hung_recovered" — a more
            # serious signal kept distinct so a wedge never hides as a clean pass.
            if e["outcome"] == "passed":
                if k in hung and final.get(k, {}).get("outcome") != "failed":
                    f["hung_recovered"] = True
                elif final.get(k, {}).get("outcome") == "failed":
                    f["recovered"] = True
            f["outcome"] = e["outcome"]

    # completeness: expected-to-run set (conftest collected.txt) minus what produced a
    # result → tests that silently vanished (aborted worker / xdist INTERNALERROR). Blocks GREEN.
    expected = _load_collected(run_dir, _key)
    completed = {_key(f["nodeid"]) for f in final.values()}
    not_run = sorted(n for n in expected if n not in completed)

    if not final:  # nothing produced a result
        if not_run:
            _print_groups([], [], [], run_dir,
                          {"passed": 0, "recovered": 0, "allow_fail": 0,
                           "blocking": 0, "skipped": 0, "not_run": len(not_run)},
                          elapsed, prewarm, not_run=not_run)
        record_run_history(run_dir, {"not_run": len(not_run)}, "RED",
                           elapsed, prewarm, collected=len(expected))
        return {"recovered": [], "allow_fail": [], "blocking": [], "counts": {},
                "not_run": not_run}
    items = [(f["nodeid"], _state_of(f["outcome"], f["allow_fail"], f["recovered"],
                                     f.get("hung_recovered", False)))
             for f in final.values()]
    grid, order = _grid_from_states(items)
    _render_matrix(grid, order, regression)

    recovered = sorted(f["nodeid"] for f in final.values() if f["recovered"])
    hung_recovered = sorted(f["nodeid"] for f in final.values() if f.get("hung_recovered"))
    allow_fail = sorted(f["nodeid"] for f in final.values()
                        if f["outcome"] == "failed" and f["allow_fail"])
    blocking = sorted(f["nodeid"] for f in final.values()
                      if f["outcome"] == "failed" and not f["allow_fail"])
    counts = {"passed": sum(1 for f in final.values()
                            if f["outcome"] == "passed" and not f["recovered"]
                            and not f.get("hung_recovered")),
              "recovered": len(recovered), "hung_recovered": len(hung_recovered),
              "allow_fail": len(allow_fail), "blocking": len(blocking),
              "skipped": sum(1 for f in final.values() if f["outcome"] == "skipped"),
              "not_run": len(not_run)}
    _print_groups(recovered, allow_fail, blocking, run_dir, counts, elapsed, prewarm,
                  not_run=not_run, hung_recovered=hung_recovered)
    green = not blocking and not counts.get("not_run") and not lab.disk_failure_notes(run_dir)
    record_run_history(run_dir, counts, "GREEN" if green else "RED",
                       elapsed, prewarm, collected=len(expected))
    return {"recovered": recovered, "hung_recovered": hung_recovered,
            "allow_fail": allow_fail, "blocking": blocking,
            "counts": counts, "not_run": not_run}



# ── run history (checked-in CSV) ────────────────────────────────────────────
# Flake rate and speed are distribution questions; one run cannot answer either.
HISTORY_CSV = Path(__file__).resolve().parents[1] / "run_history.csv"

_HISTORY_COLS = [
    "ts", "git_sha", "branch", "dirty", "verdict",
    "collected", "passed", "recovered", "hung_recovered", "allow_fail",
    "blocking", "skipped", "not_run",
    "prewarm_s", "run_s", "total_s",
    "build_pool", "emu_pairs", "cpus_eff", "cpus_phys", "mem_gb",
    "cache_entries", "free_gb", "load1",
    "idf", "emu_rev", "expect_scale", "sandbox", "disk_floor_gb", "resctl", "run_dir", "note",
    "samples", "mem_avail_min_mb", "mem_avail_mean_mb", "mem_psi_max",
    "load_max", "others_mb_max", "compilers_max",
    "psi_peak_max", "psi_violations", "avail_min_mb",
    "io_psi_peak_max", "cpu_psi_peak_max", "own_cpu_psi_peak_max",
]


def _sh(*argv):
    try:
        import subprocess
        r = subprocess.run(argv, capture_output=True, text=True, timeout=10)
        return r.stdout.strip() if r.returncode == 0 else ""
    except Exception:  # noqa: BLE001
        return ""


def record_run_history(run_dir, counts, verdict, elapsed=None, prewarm=None,
                       collected=None):
    """Append one row to tests/run_history.csv. Cheap fields only — never a du walk of
    the cache, because this runs at the end of EVERY run. Best-effort: a failure here
    must never affect the verdict."""
    try:
        import csv
        from infra import caps
        env = os.environ.get
        repo = Path(__file__).resolve().parents[2]
        mem = getattr(caps, "mem_bytes", lambda: 0)() or 0
        try:
            cache_entries = len(list(lab.CACHE.iterdir())) if lab.CACHE.is_dir() else 0
        except OSError:
            cache_entries = 0
        try:
            load1 = round(os.getloadavg()[0], 1)
        except OSError:
            load1 = ""
        from infra import build_fw
        idf_dir = env("IDF_PATH", "") or build_fw._idf_path_opt() or ""
        idf = _sh("git", "-C", idf_dir, "describe", "--tags") if idf_dir else ""
        emu_dir = repo / ".deps" / "esp-emu"
        emu_rev = _sh("git", "-C", str(emu_dir), "rev-parse", "--short", "HEAD") \
            if emu_dir.is_dir() else ""
        row = {
            "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "git_sha": _sh("git", "-C", str(repo), "rev-parse", "--short", "HEAD"),
            "branch": _sh("git", "-C", str(repo), "rev-parse", "--abbrev-ref", "HEAD"),
            "dirty": "1" if _sh("git", "-C", str(repo), "status", "--porcelain",
                                "--untracked-files=no") else "0",
            "verdict": verdict,
            "collected": collected if collected is not None else "",
            "passed": counts.get("passed", 0),
            "recovered": counts.get("recovered", 0),
            "hung_recovered": counts.get("hung_recovered", 0),
            "allow_fail": counts.get("allow_fail", 0),
            "blocking": counts.get("blocking", 0),
            "skipped": counts.get("skipped", 0),
            "not_run": counts.get("not_run", 0),
            "prewarm_s": round(prewarm) if prewarm else 0,
            "run_s": round(elapsed) if elapsed else 0,
            "total_s": round((prewarm or 0) + (elapsed or 0)),
            "build_pool": caps.build_pool(),
            "emu_pairs": caps.emu_pair_capacity(),
            "cpus_eff": caps.effective_cpus(),
            "cpus_phys": getattr(caps, "physical_cpus", lambda: "")(),
            "mem_gb": round(mem / 1e9) if mem else "",
            "cache_entries": cache_entries,
            "free_gb": round(lab.free_gb()),
            "load1": load1,
            "idf": idf,
            "emu_rev": emu_rev,
            "expect_scale": env("EH_EXPECT_SCALE", ""),
            "sandbox": env("EH_SANDBOX", ""),
            "disk_floor_gb": lab.disk_floor_gb(),
            "resctl": __import__("infra.rescontrol", fromlist=["x"]).provider(),
            "run_dir": Path(run_dir).name,
            "note": env("EH_RUN_NOTE", ""),
        }
        try:
            ev = getattr(_bus, "stop_sampling", None)
            if ev:
                ev.set()
            row.update(lab.summarize_resources(run_dir))
        except Exception:  # noqa: BLE001
            pass
        # Migrate in place if the columns grew: appending wider rows under an older
        # header silently misaligns every future row.
        new = not HISTORY_CSV.exists()
        if not new:
            try:
                old_rows = list(csv.DictReader(open(HISTORY_CSV)))
                hdr = list(old_rows[0].keys()) if old_rows else _HISTORY_COLS
                if hdr != _HISTORY_COLS:
                    with open(HISTORY_CSV, "w", newline="") as fh:
                        w = csv.DictWriter(fh, fieldnames=_HISTORY_COLS,
                                           extrasaction="ignore")
                        w.writeheader()
                        for r in old_rows:
                            w.writerow({k: r.get(k, "") for k in _HISTORY_COLS})
            except (OSError, csv.Error, IndexError):
                pass
        with open(HISTORY_CSV, "a", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=_HISTORY_COLS, extrasaction="ignore")
            if new:
                w.writeheader()
            w.writerow(row)
    except Exception:  # noqa: BLE001 — bookkeeping must never break a run
        pass


def _print_groups(recovered, allow_fail, blocking, run_dir, counts, elapsed=None, prewarm=None, not_run=None, hung_recovered=None):
    """Truthful end-of-run grouping + a single closing verdict line (always printed).
    Every not-clean-green outcome is shown in its own labelled bucket so no failure is
    ever hidden by the allow_fail/second_chance path; the verdict states the overall
    GREEN/RED (pipeline exit) so the run never ends ambiguously."""
    color = sys.stderr.isatty()

    def c(txt, key):
        return f"{_ANSI[key]}{txt}{_ANSI['off']}" if color else txt

    def paths(n):
        """Log dir(s) for a failed nodeid: the phase-1 archive, plus the isolated
        second_chance retry archive if it exists (the cleaner one to debug from)."""
        slug = lab.sanitize(n)
        # Only link the archive dir if eh_logcap actually created it — it makes the
        # dir only when *.log exist, so a pre-launch failure has none (dangling link).
        fail_dir = Path(run_dir) / "logs" / f"{slug}.FAIL"
        out = ([f"      → {fail_dir}/"] if fail_dir.is_dir()
               else ["      (no logs captured — failed before launch)"])
        retry = Path(run_dir) / "second_chance" / "logs" / f"{slug}.FAIL"
        if retry.is_dir():
            out.append(f"      → {retry}/   (isolated retry — clean repro)")
        return out

    lines = []
    if recovered:
        lines.append(c("recovered on 2nd chance (flaky — passed isolated):", "bold"))
        for n in recovered:
            lines.append(f"  {c('↻', 'green')} {n}")
            lines.append(f"      → {run_dir}/logs/{lab.sanitize(n)}.FAIL/   (phase-1 flake log)")
    if hung_recovered:
        lines.append(c("HUNG then recovered (did NOT run under load — worker wedged/killed — "
                       "then PASSED isolated; investigate the wedge):", "bold"))
        for n in hung_recovered:
            lines.append(f"  {c('⤴', 'yellow')} {n}")
    if allow_fail:
        lines.append(c("allow_fail (non-blocking, fix later):", "bold"))
        for n in allow_fail:
            lines.append(f"  {c('⊗', 'red')} {n} FAILED")
            lines += paths(n)
    if blocking:
        lines.append(c("BLOCKING failures (pipeline red):", "bold"))
        for n in blocking:
            lines.append(f"  {c('✗', 'red')} {n} FAILED")
            lines += paths(n)
    if not_run:
        lines.append(c("DID NOT RUN (incomplete — aborted worker / xdist crash):", "bold"))
        for n in not_run:
            lines.append(f"  {c('∅', 'red')} {n}")
    # Always: one closing verdict line, so the run has an unambiguous end state.
    # Only surface the categories that actually have entries — a clean run reads
    # "N passed → GREEN", with no 0-count recovered/allow_fail/blocking/skipped noise;
    # the detail groups above already print only when non-empty.
    # F3: an ENOSPC anywhere in the run is an INFRA failure, not the cell's. It is
    # reported as its own cause and it still forces RED — a full disk never yields a
    # GREEN run, and it never gets bookkept as a product failure either.
    disk = lab.disk_failure_notes(run_dir)
    if disk:
        lines.append(c("DISK EXHAUSTED (infra — results below are not trustworthy):", "bold"))
        for ln in disk[:5]:
            lines.append(f"  {c('⛁', 'red')} {ln}")
        if len(disk) > 5:
            lines.append(f"      … and {len(disk) - 5} more")
        lines.append(f"      free now: {lab.free_gb():.1f}GB "
                     f"(floor {lab.disk_floor_gb():.0f}GB; set EH_DISK_FLOOR_GB)")
    green = not blocking and not counts.get('not_run') and not disk
    parts = [f"{counts['passed']} passed"]
    if counts['recovered']:  parts.append(f"{counts['recovered']} recovered ↻")
    if counts.get('hung_recovered'): parts.append(f"{counts['hung_recovered']} hung-recovered ⤴")
    if counts['allow_fail']: parts.append(f"{counts['allow_fail']} allow_fail ⊗")
    if counts['blocking']:   parts.append(f"{counts['blocking']} blocking ✗")
    if counts.get('not_run'): parts.append(f"{counts['not_run']} did-not-run ∅")
    if counts['skipped']:    parts.append(f"{counts['skipped']} skipped –")
    tally = " · ".join(parts)
    if elapsed:
        def _hms(sec):
            s = int(round(sec))
            return f"{s // 60}:{s % 60:02d}" if s >= 60 else f"{s}s"
        if prewarm:
            # account BOTH phases: headline is total wall time, with the split shown
            tally += f" in {_hms(prewarm + elapsed)} (prewarm {_hms(prewarm)} + run {_hms(elapsed)})"
        else:
            tally += f" in {_hms(elapsed)}"
    if disk:
        tally += " · DISK EXHAUSTED ⛁"
    verdict = c("GREEN", "green") if green else c("RED", "red")
    lines.append(f"{c('eh verdict:', 'bold')} {tally}  →  {verdict}")
    out = "\n" + "\n".join(lines) + "\n"
    sys.stderr.write(out)
    sys.stderr.flush()
    try:
        import re
        live = Path(run_dir) / "live.log"
        with open(live, "a") as fh:
            fh.write(re.sub(r"\033\[[0-9]+m", "", out))
    except OSError:
        pass


def _write_report():
    ends = [e for e in _bus.events if e["event"] == "test_end"]
    starts = {e["nodeid"]: e["t"] for e in _bus.events if e["event"] == "test_start"}
    passed = sum(1 for e in ends if e["outcome"] == "passed")
    failed = sum(1 for e in ends if e["outcome"] == "failed")
    skipped = sum(1 for e in ends if e["outcome"] == "skipped")
    total = time.time() - _bus.t0

    md = ["# Test run trace", "",
          f"**{passed} passed · {failed} failed · {skipped} skipped** "
          f"in {total:.1f}s", "",
          f"Detailed logs per test: `{_bus.dir}/logs/<test>/` — each holds "
          f"`{{cp,host}}_build.log` (firmware build), `{{cp,host}}.log` (runtime), "
          f"and `seq.md` (sequence diagram). Reused build cache: `{lab.CACHE}`.", "",
          "| test | outcome | duration | logs |", "|---|---|---|---|"]
    for e in ends:
        nid = e["nodeid"].split("::", 1)[-1]
        mark = {"passed": "✅", "failed": "❌", "skipped": "⚪"}.get(e["outcome"], e["outcome"])
        d = lab.sanitize(e["nodeid"]) + (".FAIL" if e["outcome"] == "failed" else "")
        # Link the archive only if it exists — a pre-launch failure produces no *.log,
        # so eh_logcap never made the .FAIL dir; a bare link would dangle.
        cell = f"[`logs/{d}/`](logs/{d}/)" if (_bus.dir / "logs" / d).is_dir() else "—"
        md.append(f"| `{nid}` | {mark} | {e['duration']:.2f}s | {cell} |")
    md += ["", "```mermaid", "gantt", "  dateFormat X", "  axisFormat %Ss",
           "  title Test timeline"]
    # one section = the whole run; each test a bar from its start (rel s) for its duration
    md.append("  section run")
    for i, e in enumerate(ends):
        nid = e["nodeid"].split("::")[-1].replace(":", "_")[:36]
        s = int(max(starts.get(e["nodeid"], _bus.t0) - _bus.t0, 0))
        dur = max(int(round(e["duration"])), 1)
        tag = "done" if e["outcome"] == "passed" else ("active" if e["outcome"] == "skipped" else "crit")
        md.append(f"  {nid} :{tag}, t{i}, {s}, {dur}s")
    md += ["```", ""]
    (_bus.dir / "report.md").write_text("\n".join(md))
