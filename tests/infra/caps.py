# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Single sizing authority (R7 chameleon).

Every CPU/memory-derived limit routes through here so one quota-aware source feeds
--jobs auto, the prewarm build pool, emu_pair_capacity, and cmake -j. A CPU-limited CI
container downsizes instead of oversubscribing; a runner too small to host even one emu
pair is REFUSED with a number (never silently serialized). All env knobs are read inside
their functions so an in-process override takes effect.

Model: one emu pair = 2 emus + 1 worker ~= CPUS_PER_PAIR busy CPUs + MEM_PER_PAIR mem.
Physical cores gate the emu pool (hyperthreads add no emu-timing throughput); the
CPU-bound build pool uses the effective (logical, quota-clamped) count.
"""

import json
import math
import os
import shutil
from pathlib import Path

CPUS_PER_PAIR = 3


class CapsError(RuntimeError):
    """Runner cannot meet a required capacity — raised to fail fast with a number."""


def _mem_per_pair_bytes() -> int:
    return int(os.environ.get("EH_MEM_PER_PAIR_MB", "1500")) * 1024 * 1024


def _cgroup_chain():
    """Our own cgroup v2 dir, then each ancestor up to the root. A limit can sit on any
    level (unit, slice, CI container), so the root file alone misses ours."""
    try:
        rel = Path("/proc/self/cgroup").read_text().strip().splitlines()[-1].split(":", 2)[2]
    except (OSError, IndexError):
        return []
    d = Path("/sys/fs/cgroup") / rel.lstrip("/")
    out = []
    while True:
        out.append(d)
        if d == Path("/sys/fs/cgroup"):
            return out
        d = d.parent


def _cgroup_cpu_quota():
    """CPUs the cgroup chain allows (float, tightest level), or None if unlimited.
    v2 cpu.max = '<quota> <period>' ('max' = unlimited); v1 cfs_quota_us/cfs_period_us."""
    best = None
    for d in _cgroup_chain():
        try:
            parts = (d / "cpu.max").read_text().split()
            if parts and parts[0] != "max":
                q, p = int(parts[0]), int(parts[1])
                if q > 0 and p > 0:
                    best = q / p if best is None else min(best, q / p)
        except (OSError, ValueError, IndexError):
            pass
    if best is not None:
        return best
    try:
        q = int(Path("/sys/fs/cgroup/cpu/cpu.cfs_quota_us").read_text())
        p = int(Path("/sys/fs/cgroup/cpu/cpu.cfs_period_us").read_text())
        if q > 0 and p > 0:
            return q / p
    except (OSError, ValueError):
        pass
    return None


def _affinity_cpus() -> int:
    try:
        return len(os.sched_getaffinity(0))
    except (AttributeError, OSError):
        return os.cpu_count() or 1


def cpu_reserve() -> int:
    """Cores kept out of sizing for interactive use on a shared box; a CI runner is
    dedicated, so 0 there. EH_CPU_RESERVE."""
    v = os.environ.get("EH_CPU_RESERVE")
    if v and v.isdigit():
        return int(v)
    return 0 if is_ci() else 2


def effective_cpus() -> int:
    """Usable CPUs: min(scheduler affinity, cgroup quota), floored at 1; without a quota,
    minus cpu_reserve() (a quota already IS the operator's headroom decision). floor()
    the quota so a fractional limit never rounds UP into oversubscription."""
    aff = _affinity_cpus()
    q = _cgroup_cpu_quota()
    if q is not None:
        return max(1, min(aff, math.floor(q)))
    return max(1, aff - cpu_reserve())


def _physical_from_cpuinfo():
    """Distinct (physical id, core id) pairs, or None when the topology is hidden
    (many VMs / ARM omit these lines) so the caller can record which branch it got."""
    try:
        pairs, pid = set(), None
        with open("/proc/cpuinfo") as f:
            for ln in f:
                if ln.startswith("physical id"):
                    pid = ln.split(":", 1)[1].strip()
                elif ln.startswith("core id"):
                    pairs.add((pid, ln.split(":", 1)[1].strip()))
        return len(pairs) or None
    except OSError:
        return None


def physical_cpus() -> int:
    """Physical cores; falls back to effective_cpus() when topology is hidden (see
    _physical_from_cpuinfo). Fallback trusts the vCPU count — recorded in caps.json as
    physical_source so a CI snapshot reveals which branch ran."""
    return _physical_from_cpuinfo() or effective_cpus()


def _cgroup_mem_limit():
    """Tightest memory.max on our cgroup chain (v2), else the v1 limit, else None."""
    best = None
    for d in _cgroup_chain():
        try:
            v = (d / "memory.max").read_text().strip()
            if v != "max":
                best = int(v) if best is None else min(best, int(v))
        except (OSError, ValueError):
            pass
    if best is not None:
        return best
    try:
        n = int(Path("/sys/fs/cgroup/memory/memory.limit_in_bytes").read_text().strip())
        if 0 < n < (1 << 62):   # v1 unlimited is a huge sentinel, not a real cap
            return n
    except (OSError, ValueError):
        pass
    return None


def _mem_total():
    try:
        with open("/proc/meminfo") as f:
            for ln in f:
                if ln.startswith("MemTotal"):
                    return int(ln.split()[1]) * 1024
    except (OSError, ValueError):
        pass
    return None


def mem_bytes():
    """Usable memory: min(cgroup limit, MemTotal), or None if both unknown."""
    vals = [v for v in (_cgroup_mem_limit(), _mem_total()) if v]
    return min(vals) if vals else None


def is_ci() -> bool:
    return bool(os.environ.get("CI") or os.environ.get("GITLAB_CI"))


def _raw_emu_pair_capacity() -> int:
    """TRUE capacity (may be 0): min of the CPU bound (physical cores // CPUS_PER_PAIR,
    since hyperthreads add no emu-timing headroom) and the memory bound. The refusal
    check reads THIS — the consumer-facing emu_pair_capacity() floors it at 1."""
    cpu_cap = min(physical_cpus(), effective_cpus()) // CPUS_PER_PAIR
    mem = mem_bytes()
    mem_cap = (mem // _mem_per_pair_bytes()) if mem else cpu_cap
    return min(cpu_cap, mem_cap)


def emu_pair_capacity() -> int:
    """Max concurrent live emu PAIRS. EH_EMU_PAIR_SLOTS overrides; else the raw
    CPU+memory bound, floored at 1 (a sequential run still hosts one pair at a time)."""
    v = os.environ.get("EH_EMU_PAIR_SLOTS")
    if v:
        try:
            return max(1, int(v))
        except ValueError:
            pass
    return max(1, _raw_emu_pair_capacity())


def require_emu_capacity(needed: int = 1) -> int:
    """Fail fast WITH A NUMBER when the runner can't host `needed` emu pairs — never
    silently serialize/oversubscribe. Honors the EH_EMU_PAIR_SLOTS escape hatch."""
    v = os.environ.get("EH_EMU_PAIR_SLOTS")
    have = max(1, int(v)) if (v and v.isdigit()) else _raw_emu_pair_capacity()
    if needed > have:
        mem = mem_bytes() or 0
        raise CapsError(
            f"runner too small for {needed} emu pair(s): need "
            f"{needed * CPUS_PER_PAIR} CPUs and {needed * (_mem_per_pair_bytes() >> 20)} MB, "
            f"have effective_cpus={effective_cpus()} physical={physical_cpus()} "
            f"mem={mem >> 20} MB (capacity={have}). "
            f"Provision a larger runner or set EH_EMU_PAIR_SLOTS to override.")
    return have


# Budget the BUILD (measured ~2 GB), not the emu pair (measured ~76 MB).
BUILD_MEM_MB = 2500          # measured 2067 peak + headroom; EH_MEM_PER_BUILD_MB
MEM_BUDGET_FRAC = 0.6        # SHARED BOX ceiling vs total. EH_MEM_BUDGET_FRAC
MEM_RESERVE_MB  = 4096       # never drive the box below this. EH_MEM_RESERVE_MB


def build_capacity() -> int:
    """Concurrent FIRMWARE BUILDS, bounded by CPU *and* MEMORY. One authority for both
    the prewarm phase and the run phase — the run phase had no build cap at all, which
    is what let 8 concurrent cold builds (~16 GB) trip systemd-oomd."""
    v = os.environ.get("EH_BUILD_SLOTS")
    if v:
        try:
            return max(1, int(v))
        except ValueError:
            pass
    io_cap = int(_env_float("EH_BUILD_IO_STREAMS", BUILD_IO_STREAMS))
    cpu_cap = min(max(1, effective_cpus() // _BUILD_SELF_PARALLELISM), max(1, io_cap))
    mem = mem_bytes()
    if not mem:
        return cpu_cap
    per = _env_float("EH_MEM_PER_BUILD_MB", BUILD_MEM_MB) * 1024 * 1024
    if per <= 0:
        return cpu_cap
    # Budget from what is ACTUALLY FREE, not from total: this box carries ~4 GB of
    # other users' long-lived sessions, and sizing off MemTotal would over-commit by
    # exactly their footprint. Re-read on every acquire, so the cap tracks the box.
    frac = _env_float("EH_MEM_BUDGET_FRAC", MEM_BUDGET_FRAC)
    reserve = _env_float("EH_MEM_RESERVE_MB", MEM_RESERVE_MB) * 1024 * 1024
    avail = _mem_available() or mem
    budget = min(mem * frac, max(0, avail - reserve))
    mem_cap = int(budget // per)
    return max(1, min(cpu_cap, mem_cap))


def _psi_some_avg10(path) -> float:
    """'some avg10' from a PSI file; 0.0 when unavailable."""
    try:
        for ln in Path(path).read_text().splitlines():
            if ln.startswith("some "):
                for f in ln.split():
                    if f.startswith("avg10="):
                        return float(f.split("=", 1)[1])
    except (OSError, ValueError, IndexError):
        pass
    return 0.0


def mem_pressure_pct() -> float:
    """/proc/pressure/memory 'some avg10' — the SAME signal systemd-oomd acts on.
    A level check (MemAvailable) can look fine while the box is already thrashing;
    PSI is what actually precedes an oomd kill. 0.0 when unavailable."""
    return _psi_some_avg10("/proc/pressure/memory")


def io_pressure_pct() -> float:
    """/proc/pressure/io 'some avg10' — % of time at least one task stalled on I/O.
    The disk, not the CPU, was the wall on 2026-10-06 (88-100% iowait at 1-3% user),
    and this is the only signal that sees saturation caused by OTHER tenants too."""
    return _psi_some_avg10("/proc/pressure/io")


def cpu_pressure_pct() -> float:
    """/proc/pressure/cpu 'some avg10' — box-wide runnable-but-waiting time."""
    return _psi_some_avg10("/proc/pressure/cpu")


def own_pressure_pct(res: str) -> float:
    """Our OWN cgroup's <res>.pressure: contention we cause among ourselves, which is
    what emu timing jitter tracks. Falls back to the box-wide value outside a cgroup."""
    chain = _cgroup_chain()
    if chain and (chain[0] / f"{res}.pressure").exists() and chain[0] != Path("/sys/fs/cgroup"):
        return _psi_some_avg10(chain[0] / f"{res}.pressure")
    return _psi_some_avg10(f"/proc/pressure/{res}")


def io_pressure_limit() -> float:
    """Throttle new builds above this 'some avg10' %. EH_IO_PRESSURE_LIMIT."""
    return _env_float("EH_IO_PRESSURE_LIMIT", 40.0)


def mem_pressure_limit() -> float:
    """Refuse NEW build slots above this 'some avg10' %. Well under oomd's default
    60%/30s so we back off before it starts killing. EH_MEM_PRESSURE_LIMIT."""
    return _env_float("EH_MEM_PRESSURE_LIMIT", 25.0)


def _mem_available():
    """/proc/meminfo MemAvailable — what we can take WITHOUT evicting others."""
    try:
        for ln in Path("/proc/meminfo").read_text().splitlines():
            if ln.startswith("MemAvailable:"):
                return int(ln.split()[1]) * 1024
    except (OSError, ValueError, IndexError):
        pass
    return None


def _env_float(name, default):
    v = os.environ.get(name)
    if v:
        try:
            return float(v)
        except ValueError:
            pass
    return float(default)


# ninja self-sizes to cpus+2, so size against concurrent BUILDS, not cores.
_BUILD_SELF_PARALLELISM = 8   # cores one ninja build keeps busy on average
# Each concurrent build is an independent write stream; 2026-10-06 measured 88-100%
# iowait with only 1-3% user, i.e. the disk — not the CPU — was the wall. Cap the
# streams too, not just CPU/memory. EH_BUILD_IO_STREAMS.
BUILD_IO_STREAMS = 2
BUILD_JOBS_MIN = 2            # EH_BUILD_JOBS_MIN


def cpu_budget() -> int:
    """CPUs the whole run may keep busy (builds + emulators). EH_CPU_BUDGET overrides."""
    v = os.environ.get("EH_CPU_BUDGET")
    if v and v.isdigit() and int(v) > 0:
        return int(v)
    return effective_cpus()


def build_jobs(slots: int, live_pairs: int = 0) -> int:
    """ninja -j for one firmware build, fixed when the build starts:
    (cpu_budget - live emu pairs x EH_CPU_PER_EMU_PAIR) / build slots, clamped to
    [EH_BUILD_JOBS_MIN, EH_BUILD_JOBS_MAX]. EH_BUILD_JOBS pins it. Keeps the sum of all
    compile jobs within the budget instead of every ninja taking every core."""
    v = os.environ.get("EH_BUILD_JOBS")
    if v and v.isdigit() and int(v) > 0:
        return int(v)
    budget = cpu_budget()
    per_pair = _env_float("EH_CPU_PER_EMU_PAIR", CPUS_PER_PAIR)
    lo = int(_env_float("EH_BUILD_JOBS_MIN", BUILD_JOBS_MIN))
    hi = int(_env_float("EH_BUILD_JOBS_MAX", budget))
    free = budget - int(live_pairs * per_pair)
    return max(lo, min(hi, free // max(1, slots)))


def build_pool() -> int:
    """Concurrent FIRMWARE BUILDS, not cores: effective_cpus // 8, floored at 2 (each
    build's ninja self-parallelises to ~cpus+2). EH_BUILD_POOL overrides."""
    v = os.environ.get("EH_BUILD_POOL")
    if v:
        try:
            return max(1, int(v))
        except ValueError:
            pass
    return build_capacity()   # single authority: same bound as the run phase


def cmake_jobs() -> int:
    """-j for a single cmake --build. EH_CMAKE_JOBS overrides."""
    v = os.environ.get("EH_CMAKE_JOBS")
    if v:
        try:
            return max(1, int(v))
        except ValueError:
            pass
    return effective_cpus()


CCACHE_MIN_GB = 20.0     # one regression's working set overflows ccache's 5G default
CCACHE_FULL_PCT = 90.0   # at the limit every store triggers an eviction pass
CCACHE_GB = 20.0         # test-owned cache cap; one regression stores ~6 G
CCACHE_TRIM_PCT = 50.0   # trimmed below this at session start, so a run never stores at the cap


def ccache_dir() -> Path:
    """Test builds' own ccache (never the user's): EH_CCACHE_DIR, else tests/.work/ccache."""
    v = os.environ.get("EH_CCACHE_DIR")
    return Path(v) if v else Path(__file__).resolve().parents[1] / ".work" / "ccache"


def ccache_env() -> dict:
    # content: IDF writes picolibc.specs into each build dir, so an mtime check
    # would give every build dir its own cache entries.
    return {"CCACHE_DIR": str(ccache_dir()),
            "CCACHE_MAXSIZE": f"{_env_float('EH_CCACHE_GB', CCACHE_GB):g}G",
            "CCACHE_COMPILERCHECK": "content"}


def _ccache(*args, timeout=10):
    import subprocess
    return subprocess.run(["ccache", *args], capture_output=True, text=True,
                          timeout=timeout, env={**os.environ, **ccache_env()})


def ccache_prepare():
    """Evict old entries when the test cache is past CCACHE_TRIM_PCT; returns ccache_warning()."""
    import subprocess
    st = ccache_stats()
    if st and st["pct"] > CCACHE_TRIM_PCT:
        for age in ("7d", "2d", "1d"):
            try:
                _ccache("--evict-older-than", age, timeout=600)
            except (OSError, subprocess.SubprocessError):
                break
            st = ccache_stats()
            if not st or st["pct"] <= CCACHE_TRIM_PCT:
                break
    return ccache_warning(st)


def ccache_stats():
    """{size_gb, max_gb, pct, cleanups} of the test ccache, or None."""
    import subprocess
    if not shutil.which("ccache"):
        return None
    try:
        out = _ccache("--print-stats").stdout
        kv = dict(ln.split("\t", 1) for ln in out.splitlines() if "\t" in ln)
        # ccache's "G" is decimal; report in the same unit `--max-size` takes.
        size = int(kv["cache_size_kibibyte"]) * 1024 / 1e9
        mx = int(kv["max_cache_size_kibibyte"]) * 1024 / 1e9
        return {"size_gb": round(size, 1), "max_gb": round(mx, 1),
                "pct": round(100 * size / mx, 1) if mx else 0.0,
                "cleanups": int(kv.get("cleanups_performed", 0))}
    except (OSError, ValueError, KeyError, subprocess.SubprocessError):
        return None


def ccache_warning(st=None):
    """Why the compiler cache will churn (evictions = small random I/O + misses), or None."""
    st = st if st is not None else ccache_stats()
    if not st:
        return None
    floor = _env_float("EH_CCACHE_MIN_GB", CCACHE_MIN_GB)
    why = []
    if st["max_gb"] < floor:
        why.append(f"limit {st['max_gb']}G < {floor:g}G")
    if st["pct"] >= CCACHE_FULL_PCT:
        why.append(f"{st['pct']}% full")
    if not why:
        return None
    return (f"ccache {ccache_dir()} will evict on every store ({', '.join(why)}; "
            f"{st['cleanups']} cleanups so far): set EH_CCACHE_GB={max(floor, 2 * st['max_gb']):g}")


def snapshot() -> dict:
    from infra import sandbox  # bwrap = the real userns probe, not just PATH (no cycle)
    return {
        "effective_cpus": effective_cpus(),
        "affinity_cpus": _affinity_cpus(),
        "physical_cpus": physical_cpus(),
        "physical_source": "cpuinfo" if _physical_from_cpuinfo() else "fallback",
        "cgroup_cpu_quota": _cgroup_cpu_quota(),
        "mem_bytes": mem_bytes(),
        "is_ci": is_ci(),
        "tools": {"ccache": bool(shutil.which("ccache")),
                  "bumble-hci": bool(shutil.which("bumble-hci")),
                  "bwrap": sandbox.bwrap_available()},
        "raw_emu_pair_capacity": _raw_emu_pair_capacity(),
        "emu_pair_capacity": emu_pair_capacity(),
        "build_pool": build_pool(),
        "cmake_jobs": cmake_jobs(),
        "cpu_budget": cpu_budget(),
        "build_jobs_idle": build_jobs(build_capacity()),
        "ccache": ccache_stats(),
    }


def emit_caps_json(path) -> None:
    """Write the sizing snapshot; best-effort (a caps dump must never break a run)."""
    try:
        Path(path).write_text(json.dumps(snapshot(), indent=2, sort_keys=True))
    except OSError:
        pass


if __name__ == "__main__":
    print(json.dumps(snapshot(), indent=2, sort_keys=True))
