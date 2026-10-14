# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Single source of truth for the test-lab filesystem layout.

Everything a run touches lives under ONE sandbox-bindable root, `tests/.work`
(bwrap binds this RW; the rest of the FS is read-only), split by lifecycle:

    tests/.work/
    ├─ cache/<config-hash>/   reused across runs (built firmware); disposable
    ├─ sock/                  emu Unix sockets (kept short + shallow — sun_path
    │                         is capped ~108 bytes, so NOT under runs/<ts>/…)
    └─ runs/<ts>/             per-run, fresh:
        ├─ scratch/<test>/    live writable working files (COW flash, raw logs)
        ├─ logs/<test>/       the frozen record (archived cp/host/build logs + seq.md)
        └─ report.md · live.log · events.jsonl

`run_dir()` is pinned by eh.py via $EH_RUN_DIR so the xdist controller and every
worker agree; consumers here derive the rest from it.
"""
import contextlib
import errno
import fcntl
import os
import shutil
import signal
import socket
import subprocess
import time
import zlib
from pathlib import Path

_REPO = Path(__file__).resolve().parents[2]
WORK = _REPO / "tests" / ".work"
CACHE = WORK / "cache"
SOCK_DIR = WORK / "sock"
RUNS = WORK / "runs"

# 7d: scratch is purged on PASS at teardown (eh_logcap, R8), so the age-sweep is only a
# backstop for FAIL scratch + strays. EH_RETENTION_DAYS overrides.
RETENTION_DEFAULT_DAYS = 7.0


# ── per-test spawn registry ─────────────────────────────────────────────────
# Every subprocess a bench spawns (emu DUTs, the linux shim, an external peer
# such as Bumble) registers here AT SPAWN, so the infra guarantees a reap even
# when the fixture that launched it raises before wiring up its own teardown —
# e.g. make() launches the CP emu, then the host EmuDut hits EMFILE and raises,
# so the half-built bench never reaches the caller's `made[]` teardown and the CP
# emu orphans (holding its socket/port → wedging the next test on that worker).
# Per-process (one xdist worker), cleared at each test end by the conftest reaper.
_spawned = []


def _spawn_registry() -> Path:
    """Per-worker DURABLE spawn record under EH_RUN_DIR/spawned/<pid>. The in-memory
    _spawned list dies with the worker on an os._exit (pytest-timeout) or watchdog
    killpg — no fixture teardown runs — so orphaned emus/Bumble outlive the worker
    invisibly. This on-disk trace lets the controller reap them after the run."""
    d = run_dir() / "spawned"
    d.mkdir(parents=True, exist_ok=True)
    return d / str(os.getpid())


def track(proc):
    """Register a just-spawned subprocess for guaranteed end-of-test reaping.
    Returns proc, so a caller can inline: self.proc = lab.track(Popen(...))."""
    _spawned.append(proc)
    try:
        with open(_spawn_registry(), "a") as fh:
            fh.write(f"{proc.pid}\n")
    except OSError:
        pass
    return proc


def reap_spawned():
    """Terminate any tracked process still alive at test end. A normally-torn-down
    DUT is already dead (poll() is not None) → skipped. Idempotent and independent
    of other teardown ordering (it only kills leaks nobody else reaped)."""
    for p in _spawned:
        try:
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()
        except (OSError, ValueError):
            pass
    _spawned.clear()
    # Our children are dead → clear the durable trace so the controller sweep never
    # (racing pid-reuse) targets a pid we already cleanly reaped.
    try:
        f = _spawn_registry()
        if f.exists():
            f.write_text("")
    except OSError:
        pass


def reap_run_orphans(run_dir_path) -> int:
    """Controller-side durable reap: SIGTERM→SIGKILL any pid a worker recorded at
    track() but never cleared — its reap_spawned never ran because os._exit / the
    watchdog killed the worker. Best-effort: runs right after the run group has been
    signalled (tiny pid-reuse window) and skips already-dead pids. Returns #killed."""
    sd = Path(run_dir_path) / "spawned"
    if not sd.is_dir():
        return 0
    pids = []
    for f in sd.iterdir():
        try:
            pids += [int(x) for x in f.read_text().split()]
        except (OSError, ValueError):
            continue
    alive = []
    for pid in pids:
        try:
            os.kill(pid, signal.SIGTERM)
            alive.append(pid)
        except (ProcessLookupError, PermissionError, OSError):
            pass
    if alive:
        time.sleep(2)
        for pid in alive:
            try:
                os.kill(pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError, OSError):
                pass
    return len(alive)


def sanitize(nodeid: str) -> str:
    return (nodeid.replace("::", ".").replace("/", ".")
            .replace("[", "_").replace("]", ""))


def run_dir() -> Path:
    d = os.environ.get("EH_RUN_DIR")
    return Path(d) if d else RUNS / "adhoc"


def test_logs_dir(nodeid: str, failed: bool = False) -> Path:
    """The frozen per-test record dir (cp/host/build logs + seq.md)."""
    return run_dir() / "logs" / (sanitize(nodeid) + (".FAIL" if failed else ""))


def test_scratch(nodeid: str) -> Path:
    """Live per-test writable scratch (COW flash, raw logs) — under the bound root
    so a sandboxed emu can write it; discardable (the record lives in logs/)."""
    d = run_dir() / "scratch" / sanitize(nodeid)
    d.mkdir(parents=True, exist_ok=True)
    return d


def sock_path(worker: str, tag: str = "emu") -> str:
    """A short, shallow socket path (Unix sun_path ~108-byte cap forbids the deep
    runs/<ts>/… tree) that still sits under the sandbox-bound root."""
    SOCK_DIR.mkdir(parents=True, exist_ok=True)
    return str(SOCK_DIR / f"eh_{tag}_{worker}_{os.getpid()}.sock")


# ── Per-bench resource allocation ────────────────────────────────────────────
# Every bench pair must own a UNIQUE hostfwd TCP port and Unix socket, or two
# pairs collide with "Address already in use". Uniqueness is layered:
#
#   run   → each eh.py RUN gets a disjoint high-band window, seeded from
#           EH_RUN_DIR (set once by eh.py, shared by every xdist worker of the
#           run, unique per run). A FIXED low base was the bug behind the 2313
#           collision: every concurrent run/rerun (and every user on a shared
#           box) started at the same port, so their blocks overlapped.
#   worker → striped by _PORT_STRIDE within the run window (workers are separate
#           processes; the shared seed keeps their bases disjoint).
#   bench  → monotonic slot within THIS process, never reused (also side-steps
#           TCP TIME_WAIT).
#
# The band sits above privileged/well-known ports and BELOW the Linux ephemeral
# range (ip_local_port_range default 32768+), so the kernel's outbound-connection
# allocator never steals a test port. The base is a stable property fixed at
# import (process start), not re-derived per bind. _port_free() remains a final
# guard against a cross-run straggler landing in an aliased window.
_PORT_LO = 10000
_PORT_HI = 32000                                   # < 32768 ephemeral floor
_PORT_STRIDE = 100                                 # ports per xdist worker
_WORKERS_PER_RUN = 16                              # worker blocks per run window
_WORKER_SPAN = _WORKERS_PER_RUN * _PORT_STRIDE     # 1600
_RUN_SLOTS = (_PORT_HI - _PORT_LO) // _WORKER_SPAN  # disjoint run windows

# crc32 (not hash()) — hash() of str is per-process randomized, which would give
# each xdist worker a different base and break worker striping.
_run_seed = os.environ.get("EH_RUN_DIR") or str(os.getppid())
_PORT_BASE = _PORT_LO + (zlib.crc32(_run_seed.encode()) % _RUN_SLOTS) * _WORKER_SPAN
_bench_slot = 0


def _worker_num(worker: str) -> int:
    n = int(worker[2:]) if worker.startswith("gw") and worker[2:].isdigit() else 0
    return n % _WORKERS_PER_RUN   # keep every worker inside this run's window


def _port_free(p: int) -> bool:
    """True if 127.0.0.1:p can be bound right now (no live bind, no TIME_WAIT).
    No SO_REUSEADDR — mirror the emu's plain bind so we skip a port it couldn't take."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.bind(("127.0.0.1", p))
            return True
        except OSError:
            return False


def alloc_bench(worker: str) -> dict:
    """Reserve a fresh {slot, port} for one bench pair. `slot` uniquifies the
    socket path; `port` is the hostfwd bind. Call once per bench in make().

    The slot-derived port is unique within THIS run, but a lingering emu from a
    killed/overlapping run (or another eh.py process) can still hold it — so probe
    and skip any port not actually free, avoiding a spurious 'Address already in
    use' at emu launch. We check the base port AND the next few (the bench binds a
    small contiguous block: wake→22 at `port`, hostfwd at port+1+i)."""
    global _bench_slot
    n = _worker_num(worker)
    for _ in range(_PORT_STRIDE):  # bounded: at most one lap of this worker's block
        slot = _bench_slot
        _bench_slot += 1
        port = _PORT_BASE + n * _PORT_STRIDE + slot % _PORT_STRIDE
        if all(_port_free(port + k) for k in range(4)):
            return {"slot": slot, "port": port}
    # Exhausted the block (every candidate held) — fall back to the raw slot port;
    # the emu will surface the real bind error rather than us looping forever.
    return {"slot": _bench_slot, "port": _PORT_BASE + n * _PORT_STRIDE + _bench_slot % _PORT_STRIDE}


# ── live emu-pair admission control (cross-process counting semaphore) ────────
# Each bench launches TWO single-threaded emus that each want a core during active
# phases; oversubscribing them starves the emulated CP so it misses the host
# firmware's FIXED 5s RPC deadline (the spi_fd / power-save under-load flake). xdist's
# only concurrency primitive is `--dist loadgroup`, which pins a whole cohort to ONE
# worker (all-or-1, and only among the tagged tests) — a blunt QoS hack. This is the
# real primitive: a counting semaphore of N slots over LIVE emu pairs, enforced
# ACROSS every worker (and every concurrent run on this checkout) via advisory flocks,
# so heavy cells get N-way concurrency (not 1) AND total live pairs never exceed the
# safe count even when --jobs is set larger than that. flock is auto-released by the
# OS when the holder dies, so a killed worker never leaks a slot.
_EMU_SLOTS_DIR = WORK / ".emu_slots"


def emu_pair_capacity() -> int:
    """Max concurrent live emu PAIRS. Delegates to the single sizing authority
    (caps.emu_pair_capacity) so CPU+memory bounds and EH_EMU_PAIR_SLOTS live in one
    place; derivation there."""
    from infra import caps
    return caps.emu_pair_capacity()


def _refresh_heartbeat():
    """Bump this worker's liveness heartbeat ts (state stays 'running') so the
    controller's stall watchdog does not mistake a worker legitimately BLOCKED on an
    emu slot for a wedged one. The heartbeat is stamped at test start (conftest,
    pytest_runtest_logstart) BEFORE the fixture reaches acquire_emu_slot, so a long
    slot-wait would otherwise let `age` grow past the stall budget and killpg a
    correctly-waiting run group. Best-effort; preserves the existing nodeid field."""
    d = run_dir() / "workers"
    wid = os.environ.get("PYTEST_XDIST_WORKER", "master")
    f = d / wid
    try:
        parts = f.read_text().split("\t", 2)
        nodeid = parts[1] if len(parts) >= 2 else ""
    except (OSError, ValueError):
        return  # no heartbeat file yet (e.g. -n0 before first test) → nothing to keep alive
    try:
        f.write_text(f"running\t{nodeid}\t{time.time()}\t{os.getpid()}\n")
    except OSError:
        pass


def flock_with_heartbeat(fh) -> None:
    """Blocking exclusive flock that refreshes this worker's heartbeat while it waits —
    the holder may be another worker's whole (slow-disk) build of the same config."""
    while True:
        try:
            fcntl.flock(fh, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return
        except OSError:
            _refresh_heartbeat()
            time.sleep(1.0)


def run_with_heartbeat(argv, timeout, **kw) -> int:
    """subprocess.run(argv, timeout=...) that refreshes this worker's heartbeat while
    the child runs, so a long but progressing build never reads as a wedged worker.
    Raises subprocess.TimeoutExpired past `timeout`, killing the child like run() does."""
    import subprocess
    p = subprocess.Popen(argv, **kw)
    deadline = time.time() + timeout
    while True:
        try:
            return p.wait(timeout=min(10.0, max(0.1, deadline - time.time())))
        except subprocess.TimeoutExpired:
            if time.time() >= deadline:
                p.kill()
                p.wait()
                raise subprocess.TimeoutExpired(argv, timeout)
            _refresh_heartbeat()


def acquire_emu_slot(timeout=1800.0):
    """Reserve one live-emu-pair slot; blocks (polling) until one is free or `timeout`.
    Returns an open file object whose held flock IS the reservation — keep it alive for
    the bench's lifetime and pass it to release_emu_slot() at teardown. Returns None on
    timeout (caller proceeds fail-open rather than deadlocking a run). Fail-open on any
    fs error too — admission control must never be the thing that breaks a run.
    Refreshes the worker heartbeat each poll so a long, LEGITIMATE wait never trips the
    stall watchdog (which can't otherwise tell 'waiting for a slot' from 'wedged')."""
    n = emu_pair_capacity()
    try:
        _EMU_SLOTS_DIR.mkdir(parents=True, exist_ok=True)
    except OSError:
        return None
    deadline = time.time() + timeout
    while True:
        for i in range(n):
            try:
                fh = open(_EMU_SLOTS_DIR / f"slot{i}", "w")
            except OSError:
                continue
            try:
                fcntl.flock(fh, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return fh
            except OSError:
                fh.close()
        if time.time() >= deadline:
            return None
        _refresh_heartbeat()  # keep the watchdog from killing a correctly-waiting worker
        time.sleep(0.2)


# Builds are the expensive resource (~2 GB each); bound them in BOTH phases.
_BUILD_SLOTS_DIR = WORK / ".build_slots"


def build_capacity() -> int:
    """Max concurrent firmware BUILDS (caps.build_capacity: CPU *and* memory bound)."""
    from infra import caps
    return caps.build_capacity()


def acquire_build_slot(timeout=1800.0):
    """Reserve one concurrent-build slot; blocks (polling) until free or `timeout`.
    Returns the open file whose flock IS the reservation, or None (fail-open) so a
    slot shortage degrades to oversubscription rather than a failed run.

    CALLER CONTRACT: take this BEFORE the per-config build flock. The reverse order
    deadlocks — a config-lock holder waiting for a slot while a slot holder waits for
    that same config lock. One global order removes the cycle."""
    from infra import caps
    try:
        _BUILD_SLOTS_DIR.mkdir(parents=True, exist_ok=True)
    except OSError:
        return None
    deadline = time.time() + timeout
    while True:
        # Re-size every poll; PSI throttles to 1 under pressure. Never below 1.
        n = build_capacity()
        # Throttle on EITHER pressure. Each build is a write stream, so I/O
        # saturation — including another tenant's — must shrink us too.
        if (caps.mem_pressure_pct() > caps.mem_pressure_limit()
                or caps.io_pressure_pct() > caps.io_pressure_limit()):
            n = 1
        for i in range(n):
            try:
                fh = open(_BUILD_SLOTS_DIR / f"slot{i}", "w")
            except OSError:
                continue
            try:
                fcntl.flock(fh, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return fh
            except OSError:
                fh.close()
        if time.time() >= deadline:
            return None
        _refresh_heartbeat()  # a legitimate build-slot wait must not look like a wedge
        time.sleep(0.2)


@contextlib.contextmanager
def build_slot(timeout=1800.0):
    """Context-manager form of acquire/release_build_slot, so a caller can fold it
    into an existing `with` without restructuring. Fail-open: yields even when no
    slot is free (degrades to oversubscription, never to a failed run)."""
    fh = acquire_build_slot(timeout)
    try:
        yield fh
    finally:
        release_build_slot(fh)


def release_build_slot(fh) -> None:
    """Release a slot from acquire_build_slot(). Idempotent / None-safe."""
    if fh is None:
        return
    try:
        fcntl.flock(fh, fcntl.LOCK_UN)
    except OSError:
        pass
    try:
        fh.close()
    except OSError:
        pass


def release_emu_slot(fh) -> None:
    """Release a slot taken by acquire_emu_slot(). Idempotent / None-safe. (The OS also
    releases the flock if the holder process dies, so this is the clean-path release.)"""
    if fh is None:
        return
    try:
        fcntl.flock(fh, fcntl.LOCK_UN)
    except OSError:
        pass
    try:
        fh.close()
    except OSError:
        pass


def retention_days() -> float:
    """Days to keep run reports + build-cache entries. Default 7 (scratch is purged
    on PASS at teardown, so the sweep is only a backstop); any run overrides via
    $EH_RETENTION_DAYS (0 or negative disables the sweep)."""
    v = os.environ.get("EH_RETENTION_DAYS")
    if v:
        try:
            return float(v)
        except ValueError:
            pass
    return RETENTION_DEFAULT_DAYS


def sweep_expired(days=None) -> int:
    """Prune runs/ and cache/ entries whose mtime is older than `days` (default
    retention_days()). One stat per top-level entry, so it's cheap to run at every
    startup. Never removes the current run ($EH_RUN_DIR). A cache dir's mtime is
    bumped on every reuse (build_fw), so 'old' means genuinely unused. Returns the
    count removed."""
    days = retention_days() if days is None else days
    if days <= 0:
        return 0
    cutoff = time.time() - days * 86400.0
    keep = os.environ.get("EH_RUN_DIR")
    keep = str(Path(keep)) if keep else None
    removed = 0
    # Per-entry sweep of the reused/per-run trees (a cache dir's mtime is bumped on
    # every build reuse, so 'old' == genuinely unused).
    for base in (RUNS, CACHE):
        if not base.is_dir():
            continue
        for d in base.iterdir():
            if _is_live(d):
                continue
            try:
                if d.stat().st_mtime < cutoff:
                    shutil.rmtree(d, ignore_errors=True)
                    removed += 1
            except OSError:
                pass
    # Also sweep the sibling hygiene dirs the old sweep missed (they accumulated
    # forever): the per-config lock files, emu socket files, live-pair slot files,
    # spawn registries, and any ad-hoc top-level cache a non-build_fw caller dropped
    # straight under WORK (e.g. runner.py scratch, test_openthread's ot_cli_peer).
    for base in (WORK / ".locks", SOCK_DIR, WORK / ".emu_slots", WORK / "spawned"):
        if not base.is_dir():
            continue
        for f in base.iterdir():
            try:
                if f.stat().st_mtime < cutoff:
                    if f.is_dir():
                        shutil.rmtree(f, ignore_errors=True)
                    else:
                        f.unlink()
                    removed += 1
            except OSError:
                pass
    # Top-level strays under WORK that belong to no blessed subdir — the same age rule.
    _blessed = {"cache", "runs", "sock", ".locks", ".emu_slots", "spawned", "adhoc"}
    if WORK.is_dir():
        for d in WORK.iterdir():
            if d.name in _blessed or _is_live(d):
                continue
            try:
                if d.stat().st_mtime < cutoff:
                    if d.is_dir():
                        shutil.rmtree(d, ignore_errors=True)
                    else:
                        d.unlink()
                    removed += 1
            except OSError:
                pass
    return removed


# ── resource sampling (F4) ─────────────────────────────────────────────────
# A duration is meaningless without the conditions that produced it.
# Control needs a fast tick; attribution does not. /proc reads cost 0.03ms so 2s is
# free, while a `ps` scan costs 19ms (~1% of a core at 2s) — so only the cheap signals
# tick fast, and their PEAKS are folded into the slow row. A 30s row alone reports the
# average and hides the spike that actually trips oomd.
_FAST = {"n": 0, "psi_max": 0.0, "io_psi_max": 0.0, "cpu_psi_max": 0.0,
         "own_cpu_psi_max": 0.0, "avail_min": None,
         "load_max": 0.0, "violations": 0, "io_violations": 0}


def sample_fast(psi_limit=None):
    """Cheap control sample (~0.03ms). Updates the peak accumulator and returns it."""
    from infra import caps
    psi = caps.mem_pressure_pct()
    iopsi = caps.io_pressure_pct()
    cpupsi = caps.cpu_pressure_pct()
    own_cpupsi = caps.own_pressure_pct("cpu")
    limit = caps.mem_pressure_limit() if psi_limit is None else psi_limit
    try:
        avail = (caps._mem_available() or 0) // 2**20
    except Exception:  # noqa: BLE001
        avail = 0
    try:
        load = os.getloadavg()[0]
    except OSError:
        load = 0.0
    f = _FAST
    f["n"] += 1
    f["psi_max"] = max(f["psi_max"], psi)
    f["io_psi_max"] = max(f.get("io_psi_max", 0.0), iopsi)
    f["cpu_psi_max"] = max(f["cpu_psi_max"], cpupsi)
    f["own_cpu_psi_max"] = max(f["own_cpu_psi_max"], own_cpupsi)
    if iopsi > caps.io_pressure_limit():
        f["io_violations"] = f.get("io_violations", 0) + 1
    f["load_max"] = max(f["load_max"], load)
    f["avail_min"] = avail if f["avail_min"] is None else min(f["avail_min"], avail)
    if psi > limit:
        f["violations"] += 1
    return dict(f)


def _fast_drain():
    """Return the peaks since the last drain, then reset."""
    f = dict(_FAST)
    _FAST.update({"n": 0, "psi_max": 0.0, "io_psi_max": 0.0, "cpu_psi_max": 0.0,
                  "own_cpu_psi_max": 0.0, "avail_min": None,
                  "load_max": 0.0, "violations": 0, "io_violations": 0})
    return f


def sample_resources(run_dir_path=None):
    """One sample row. Cheap: /proc reads + one ps. Never raises."""
    import subprocess
    row = {"ts": time.strftime("%H:%M:%S")}
    try:
        from infra import caps
        mem = caps.mem_bytes() or 0
        avail = caps._mem_available() or 0
        row["mem_total_mb"] = int(mem / 2**20)
        row["mem_avail_mb"] = int(avail / 2**20)
        row["mem_psi"] = round(caps.mem_pressure_pct(), 2)
        row["io_psi"] = round(caps.io_pressure_pct(), 2)
        row["build_cap"] = caps.build_capacity()
    except Exception:  # noqa: BLE001
        pass
    try:
        row["load1"] = round(os.getloadavg()[0], 1)
    except OSError:
        row["load1"] = ""
    try:  # our footprint vs everyone else's — the collaborative-citizen metric
        out = subprocess.run(["ps", "-eo", "rss,user,comm", "--no-headers"],
                             capture_output=True, text=True, timeout=10).stdout
        me = os.environ.get("USER") or ""
        ours = others = emus = builds = 0
        for ln in out.splitlines():
            f = ln.split(None, 2)
            if len(f) < 3:
                continue
            rss, user, comm = int(f[0]), f[1], f[2].strip()
            # ps truncates to 8 cols and marks it with '+': "yogeshm+"
            u = user.rstrip("+")
            if me and (me == user or me.startswith(u)):
                ours += rss
            else:
                others += rss
            if comm == "esp-emu":
                emus += 1
            elif comm.startswith(("cc1", "riscv32-esp", "xtensa", "ninja")):
                builds += 1
        row["ours_mb"] = int(ours / 1024)
        row["others_mb"] = int(others / 1024)
        row["emus"] = emus
        row["compilers"] = builds
    except Exception:  # noqa: BLE001
        pass
    f = _fast_drain()
    row["fast_n"] = f["n"]
    row["psi_peak"] = round(f["psi_max"], 2)
    row["avail_min_mb"] = f["avail_min"] if f["avail_min"] is not None else ""
    row["load_peak"] = round(f["load_max"], 1)
    row["psi_violations"] = f["violations"]
    row["io_psi_peak"] = round(f.get("io_psi_max", 0.0), 2)
    row["io_violations"] = f.get("io_violations", 0)
    row["cpu_psi_peak"] = round(f["cpu_psi_max"], 2)
    row["own_cpu_psi_peak"] = round(f["own_cpu_psi_max"], 2)
    if run_dir_path:
        try:
            import csv
            f = Path(run_dir_path) / "resource_samples.csv"
            new = not f.exists()
            with open(f, "a", newline="") as fh:
                w = csv.DictWriter(fh, fieldnames=list(row.keys()), extrasaction="ignore")
                if new:
                    w.writeheader()
                w.writerow(row)
        except OSError:
            pass
    return row


def summarize_resources(run_dir_path):
    """Aggregate resource_samples.csv for the run-history row: the conditions the
    run actually met, so a duration can be judged against them."""
    import csv
    f = Path(run_dir_path) / "resource_samples.csv"
    try:
        rows = list(csv.DictReader(open(f)))
    except OSError:
        return {}
    if not rows:
        return {}

    def col(k):
        out = []
        for r in rows:
            try:
                out.append(float(r.get(k) or 0))
            except ValueError:
                pass
        return out
    av, psi, ld, ot, cc = (col("mem_avail_mb"), col("mem_psi"), col("load1"),
                           col("others_mb"), col("compilers"))
    pk, vi, am = col("psi_peak"), col("psi_violations"), col("avail_min_mb")
    iopk, cpk, ocpk = col("io_psi_peak"), col("cpu_psi_peak"), col("own_cpu_psi_peak")
    return {
        "samples": len(rows),
        "mem_avail_min_mb": int(min(av)) if av else "",
        "mem_avail_mean_mb": int(sum(av) / len(av)) if av else "",
        "mem_psi_max": round(max(psi), 2) if psi else "",
        "load_max": round(max(ld), 1) if ld else "",
        "others_mb_max": int(max(ot)) if ot else "",
        "compilers_max": int(max(cc)) if cc else "",
        "psi_peak_max": round(max(pk), 2) if pk else "",
        "psi_violations": int(sum(vi)) if vi else 0,
        "avail_min_mb": int(min(am)) if am else "",
        "io_psi_peak_max": round(max(iopk), 2) if iopk else "",
        "cpu_psi_peak_max": round(max(cpk), 2) if cpk else "",
        "own_cpu_psi_peak_max": round(max(ocpk), 2) if ocpk else "",
    }


# ── disk budget (F3) ────────────────────────────────────────────────────────
# Age retention alone cannot stop one long run filling the fs; check before each
# byte-consuming step and escalate: expired → cold cache → old runs → stop.
DISK_FLOOR_DEFAULT_GB = 20.0
CACHE_BUDGET_DEFAULT_GB = 0.0   # 0 = unbounded; cache is the speed asset


class DiskExhausted(RuntimeError):
    """The run cannot continue because the filesystem is full. An INFRA verdict,
    never a product failure — but the run is still RED (see eh_reporter)."""


def _env_gb(name, default) -> float:
    v = os.environ.get(name)
    if v:
        try:
            return float(v)
        except ValueError:
            pass
    return default


def disk_floor_gb() -> float:
    """Free-space floor in GB ($EH_DISK_FLOOR_GB; 0 or negative disables)."""
    return _env_gb("EH_DISK_FLOOR_GB", DISK_FLOOR_DEFAULT_GB)


def cache_budget_gb() -> float:
    """Size budget for cache/ in GB ($EH_CACHE_BUDGET_GB; 0 = unbounded)."""
    return _env_gb("EH_CACHE_BUDGET_GB", CACHE_BUDGET_DEFAULT_GB)


def free_gb(path=None) -> float:
    try:
        return shutil.disk_usage(str(path or (WORK if WORK.is_dir() else _REPO))).free / 1e9
    except OSError:
        return float("inf")   # unknowable → never block the run on a stat failure


def is_enospc(err) -> bool:
    """True for the two shapes ENOSPC takes here: a raised OSError(errno 28), and a
    build log where the compiler/linker reported the write failure as its own error."""
    if isinstance(err, OSError) and err.errno == errno.ENOSPC:
        return True
    t = err if isinstance(err, str) else f"{err}"
    return ("No space left on device" in t or "error writing" in t
            or "No space left" in t)


def _dir_size(p) -> int:
    total = 0
    for root, _dirs, files in os.walk(p, onerror=lambda e: None):
        for f in files:
            try:
                total += os.lstat(os.path.join(root, f)).st_size
            except OSError:
                pass
    return total


def _is_live(d) -> bool:
    """True if `d` is the current run dir OR an ancestor of it. During recovery
    EH_RUN_DIR is <run>/second_chance, so an equality test would leave the parent
    run unprotected and evictable by mtime alone."""
    keep = os.environ.get("EH_RUN_DIR")
    if not keep:
        return False
    keep = str(Path(keep))
    d = str(Path(d))
    return keep == d or keep.startswith(d + os.sep)


def _evict_lru(base, keep_newest=0, stop_when=None) -> int:
    """Evict entries under `base` coldest-first (mtime; build_fw bumps it on every
    cache reuse, so 'cold' means genuinely unused). Stops as soon as stop_when()
    is satisfied. Never touches the current run."""
    if not base.is_dir():
        return 0
    try:
        entries = sorted(base.iterdir(), key=lambda d: d.stat().st_mtime)
    except OSError:
        return 0
    if keep_newest:
        entries = entries[:-keep_newest] or []
    removed = 0
    for d in entries:
        if _is_live(d):
            continue
        if stop_when and stop_when():
            break
        if d.is_dir():
            shutil.rmtree(d, ignore_errors=True)
        else:
            _unlink(d)
        removed += 1
    return removed


def _unlink(p):
    try:
        p.unlink()
    except OSError:
        pass


def enforce_cache_budget() -> int:
    """LRU-evict cache/ down to $EH_CACHE_BUDGET_GB. Unbounded by default — the
    cache is what makes a warm run fast, so it is never evicted on age alone."""
    budget = cache_budget_gb()
    if budget <= 0 or not CACHE.is_dir():
        return 0
    over = [0]

    def _under():
        over[0] = _dir_size(CACHE)
        return over[0] <= budget * 1e9

    if _under():
        return 0
    return _evict_lru(CACHE, stop_when=_under)


def ensure_free(stage="run", floor_gb=None) -> float:
    """Free-space floor, checked before a byte-consuming stage. Escalates until the
    floor is met, then raises DiskExhausted rather than letting the fs fill and the
    failure land on whichever test happened to be running.

    Returns the free GB actually available. Raises DiskExhausted if the floor
    cannot be met."""
    floor = disk_floor_gb() if floor_gb is None else floor_gb
    if floor <= 0:
        return free_gb()
    free = free_gb()
    if free >= floor:
        return free
    # 1) expired entries (the ordinary age sweep, run on demand rather than only at
    #    startup — this is the gap that let a long run fill its own disk).
    sweep_expired()
    free = free_gb()
    if free >= floor:
        return free
    # 2) oldest runs, keeping the newest few for debugging (never the current one).
    _evict_lru(RUNS, keep_newest=2, stop_when=lambda: free_gb() >= floor)
    free = free_gb()
    if free >= floor:
        return free
    raise DiskExhausted(
        f"free space {free:.1f}GB is below the {floor:.0f}GB floor before {stage} "
        f"(after pruning expired entries and old runs). The build cache is "
        f"deliberately NOT evicted here — it is the speed asset, and mid-run rmtree "
        f"races in-flight builds; free space outside tests/.work instead, or set "
        f"EH_CACHE_BUDGET_GB (applied at session start) / EH_DISK_FLOOR_GB.")


def note_disk_failure(msg) -> None:
    """Record an ENOSPC for the end-of-run verdict, so it renders as a disk/infra
    failure instead of being attributed to whichever cell was unlucky."""
    try:
        d = run_dir()
        d.mkdir(parents=True, exist_ok=True)
        with open(d / "disk_exhausted.txt", "a") as fh:
            fh.write(f"{time.strftime('%H:%M:%S')} {msg}\n")
    except OSError:
        pass


def disk_failure_notes(run_dir_path=None):
    """The recorded ENOSPC lines for this run (empty list = no disk failure).
    Reads the recovery phase too: during second_chance EH_RUN_DIR is
    <run>/second_chance, so a disk failure there lands in a different file than the
    one the final render looks at — which is exactly where 2026-10-04 died."""
    base = Path(run_dir_path or run_dir())
    out = []
    for p in (base / "disk_exhausted.txt", base / "second_chance" / "disk_exhausted.txt"):
        try:
            out += [ln for ln in p.read_text().splitlines() if ln.strip()]
        except OSError:
            pass
    return out


# ── killed-worker evidence (F3) ─────────────────────────────────────────────
# A killed worker runs no teardown, so its scratch holds the only wedge record.
CAP_LINES = 4000      # keep first/last this-many when a log is oversized
CAP_BYTES = 4 << 20   # ...triggered above this size (4 MiB)

_HEAVY = ("build", "managed_components")   # regenerable; the bytes worth reclaiming


def copy_capped(src, dst) -> None:
    """Copy a log, head+tail capped so a wedged CP spewing 100s of MB stays readable."""
    src, dst = Path(src), Path(dst)
    try:
        if src.stat().st_size <= CAP_BYTES:
            shutil.copyfile(src, dst)
            return
    except OSError:
        return
    head, tail, i = [], [], -1
    try:
        with open(src, "r", errors="replace") as fh:
            for i, line in enumerate(fh):
                if i < CAP_LINES:
                    head.append(line)
                else:
                    tail.append(line)
                    if len(tail) > CAP_LINES:
                        tail.pop(0)
        elided = max(0, (i + 1) - len(head) - len(tail))
        with open(dst, "w") as out:
            out.writelines(head)
            out.write(f"\n... [eh: {elided} lines elided — see full log in tmp] ...\n\n")
            out.writelines(tail)
    except OSError:
        pass


def harvest_killed_scratch(run_dir_path, nodeids, reclaim=True):
    """For each nodeid whose scratch survived without a teardown: freeze its *.log into
    logs/<slug>.FAIL/ (capped), then drop the regenerable bulk (build dirs, COW flash).
    Returns (tests_harvested, logs_frozen, bytes_reclaimed). Order is load-bearing:
    reclaiming first would destroy the only evidence a wedge leaves behind."""
    base = Path(run_dir_path) / "scratch"
    harvested = frozen = freed = 0
    for n in nodeids:
        d = base / sanitize(n)
        if not d.is_dir():
            continue           # never ran, or teardown already purged it on PASS
        dest = Path(run_dir_path) / "logs" / (sanitize(n) + ".FAIL")
        got = False
        for lg in sorted(d.glob("*.log")):
            target = dest / lg.name
            if target.exists():
                continue       # teardown got there first — don't clobber the record
            try:
                dest.mkdir(parents=True, exist_ok=True)
                copy_capped(lg, target)
                frozen += 1
                got = True
            except OSError:
                pass
        if got:
            harvested += 1
        if not reclaim:
            continue
        for sub in _HEAVY:                      # regenerable build output
            h = d / sub
            if h.is_dir():
                freed += _dir_size(h)
                shutil.rmtree(h, ignore_errors=True)
        for img in list(d.glob("*.bin")) + list(d.glob("**/flash_*.bin")):
            try:                                # COW flash: a real copy on ext4
                freed += img.stat().st_size
                img.unlink()
            except OSError:
                pass
    return harvested, frozen, freed
