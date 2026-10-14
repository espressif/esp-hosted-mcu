# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Tool-agnostic resource-control provider (F4).

Kernel cgroup limits are the best way to bound a run's CPU/memory/IO against other
users, but they are a PRIVILEGE and a PLATFORM assumption: no systemd in most CI
containers, no cgroup v2 on macOS, often no root on a shared box, and a read-only
cgroup fs inside Docker. So this is a provider with an honest probe, mirroring
`sandbox.py`: nothing is ever mandated and every tier degrades to one that always
works.

Tiers, best first — EH_RESCTL=auto|none|systemd|cgroup|nice (default auto):
  systemd  transient USER unit (no superuser) with CPUWeight/MemoryMax/MemorySwapMax;
           needs a running `systemd --user` with Delegate=yes. CPU is a share (idle
           capacity is never wasted); memory is a hard cap with no swap (see DEFAULTS).
  cgroup   write the same knobs into our own cgroup v2 dir when it is delegated and
           writable (common in rootless containers with cgroup delegation).
  nice     renice + ionice: no isolation, but still yields to other users. Always
           available on POSIX.
  none     identity. The in-process build semaphore remains the only bound.

The userspace semaphore (lab.build_slot) is NOT a tier: it bounds our own concurrency
and is always on. These tiers bound our footprint against OTHER tenants.
"""

import os
import shutil
import subprocess
from pathlib import Path

_probe_cache = None

# CPU is reclaimable, so it gets a SHARE: yields under contention, uses idle cores
# otherwise (a CPUQuota wasted idle capacity and its 100ms throttle bursts jittered emu
# timing). Memory is not reclaimable, so it gets the one HARD cap — with swap off, or
# the cap just spills into swap (measured: 300M cap, 1GB alloc, 835M swapped, no kill).
# No MemoryHigh: with swap off it stalls the run at the soft cap instead of killing it.
# No IOWeight: the io controller is not delegated to user units; I/O is bounded by
# build-slot admission on io PSI instead. Interactive CPU headroom = caps.cpu_reserve().
DEFAULTS = {
    "CPUWeight": "20",
    "MemoryMax": "22G",
    "MemorySwapMax": "0",
    "OOMPolicy": "continue",   # default 'stop' tears down the whole run on ONE oom-kill
}
# Honoured only when set explicitly (EH_RESCTL_<KNOB>); never on by default.
OPTIONAL = ("CPUQuota", "MemoryHigh", "IOWeight")


def _systemd_env():
    """Env a non-login context needs to reach the per-user systemd bus."""
    env = dict(os.environ)
    uid = os.getuid()
    env.setdefault("XDG_RUNTIME_DIR", f"/run/user/{uid}")
    env.setdefault("DBUS_SESSION_BUS_ADDRESS",
                   f"unix:path={env['XDG_RUNTIME_DIR']}/bus")
    return env


def _systemd_works() -> bool:
    """True if we can create a transient USER unit — no superuser anywhere. Probed by
    actually creating a no-op unit, since a present binary proves nothing."""
    if not shutil.which("systemd-run") or not Path("/run/systemd/system").is_dir():
        return False
    try:
        r = subprocess.run(
            ["systemd-run", "--user", "--collect", "--quiet",
             "--unit=eh-resctl-probe", "--", "/bin/true"],
            capture_output=True, timeout=15, env=_systemd_env())
        return r.returncode == 0
    except Exception:  # noqa: BLE001  any probe failure = unavailable
        return False


def _cgroup_writable() -> bool:
    """True if our own cgroup v2 dir is delegated and writable (rootless containers)."""
    try:
        line = Path("/proc/self/cgroup").read_text().strip().splitlines()[-1]
        rel = line.split(":", 2)[2].lstrip("/")
        d = Path("/sys/fs/cgroup") / rel
        return d.is_dir() and os.access(d, os.W_OK)
    except (OSError, IndexError):
        return False


def provider(mode=None) -> str:
    """Resolve EH_RESCTL (or an explicit mode) to a tier that actually works here."""
    global _probe_cache
    m = (mode or os.environ.get("EH_RESCTL") or "auto").lower()
    if m == "none":
        return "none"
    if _probe_cache is None:
        _probe_cache = {"systemd": _systemd_works(), "cgroup": _cgroup_writable()}
    if m in ("systemd", "cgroup"):
        # An explicit choice that cannot run falls back rather than failing the run.
        return m if _probe_cache.get(m) else ("nice" if m != "none" else "none")
    if m == "nice":
        return "nice"
    for tier in ("systemd", "cgroup"):
        if _probe_cache.get(tier):
            return tier
    return "nice"


def limits() -> dict:
    """Effective limits, env-overridable per knob (EH_RESCTL_<KNOB>)."""
    out = dict(DEFAULTS)
    for k in list(out) + list(OPTIONAL):
        v = os.environ.get(f"EH_RESCTL_{k.upper()}")
        if v:
            out[k] = v
    return out


def _bytes(v: str) -> str:
    """systemd size ('22G', '300M', '0', 'infinity') -> cgroup v2 file value."""
    v = v.strip()
    if v.lower() in ("infinity", "max"):
        return "max"
    mult = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30, "T": 1 << 40}.get(v[-1:].upper())
    return str(int(float(v[:-1]) * mult)) if mult else v


def wrap(argv, cwd=None, unit="eh-run", setenv=None, scope=False):
    """argv wrapped by the selected tier. `none`/`cgroup` return argv unchanged —
    cgroup limits are applied in-process by apply_self(). scope=True keeps the child
    attached to the caller's terminal (live output, Ctrl-C, exit code, inherited env)."""
    tier = provider()
    if tier != "systemd":
        return list(argv)
    if scope:
        cmd = ["systemd-run", "--user", "--scope", "--quiet", f"--unit={unit}"]
        for k, v in limits().items():
            cmd.append(f"--property={k}={v}")
        return cmd + ["--"] + list(argv)
    cmd = ["systemd-run", "--user", "--collect", "--quiet", f"--unit={unit}"]
    if cwd:
        cmd.append(f"--working-directory={cwd}")
    for k, v in (setenv or {}).items():
        cmd.append(f"--setenv={k}={v}")
    for k, v in limits().items():
        cmd.append(f"--property={k}={v}")
    return cmd + ["--"] + list(argv)


def apply_self() -> str:
    """Apply limits to the CURRENT process's cgroup (tier 'cgroup'), or renice
    (tier 'nice'). Best-effort: resource control must never break a run."""
    tier = provider()
    if tier == "cgroup":
        try:
            line = Path("/proc/self/cgroup").read_text().strip().splitlines()[-1]
            d = Path("/sys/fs/cgroup") / line.split(":", 2)[2].lstrip("/")
            for knob, fname in (("MemoryHigh", "memory.high"),
                                ("MemoryMax", "memory.max"),
                                ("MemorySwapMax", "memory.swap.max"),
                                ("CPUWeight", "cpu.weight"),
                                ("IOWeight", "io.weight")):
                v = limits().get(knob)
                f = d / fname
                if v and f.exists():
                    try:
                        f.write_text(_bytes(v) if fname.startswith("memory") else v)
                    except OSError:
                        pass
        except (OSError, IndexError):
            pass
    elif tier == "nice":
        try:
            os.nice(5)
        except OSError:
            pass
        if shutil.which("ionice"):
            try:
                subprocess.run(["ionice", "-c", "2", "-n", "6", "-p", str(os.getpid())],
                               capture_output=True, timeout=5)
            except Exception:  # noqa: BLE001
                pass
    return tier


def describe() -> dict:
    """Inventory for caps.json / the run-history row."""
    tier = provider()
    return {"resctl": tier,
            "systemd_ok": bool((_probe_cache or {}).get("systemd")),
            "cgroup_ok": bool((_probe_cache or {}).get("cgroup")),
            **({} if tier == "none" else limits())}
