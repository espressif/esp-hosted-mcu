# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Tool-agnostic isolation provider (R6).

One seam both the pytest path (build_fw build + targets/emu EmuDut launch) and the
esplab runner call, so sandboxing is FULL, not partial. `wrap(argv, rw_paths, net)`
returns argv wrapped by the selected provider; `none` is identity so native always
works and no tool is ever mandated. Selected by EH_SANDBOX=auto|none|bwrap (default
none): `auto` = bwrap if it actually works here else native; an explicit `bwrap` that
can't run also falls back to native rather than failing the run.
"""

import os
import shutil
import subprocess

_bwrap_works_cache = None


def _bwrap_works() -> bool:
    """True if bubblewrap is installed AND user namespaces actually work here (memoized
    — the probe spawns a process, and wrap() is called per build/emu launch)."""
    global _bwrap_works_cache
    if _bwrap_works_cache is None:
        ok = False
        if shutil.which("bwrap"):
            try:
                r = subprocess.run(
                    ["bwrap", "--ro-bind", "/", "/", "--dev-bind", "/dev", "/dev",
                     "--proc", "/proc", "true"], capture_output=True, timeout=10)
                ok = r.returncode == 0
            except Exception:  # noqa: BLE001  any probe failure = unavailable
                ok = False
        _bwrap_works_cache = ok
    return _bwrap_works_cache


def bwrap_available() -> bool:
    """True if bwrap can actually run here (installed AND user namespaces work) — the
    real probe, not just PATH presence. For caps.json's tool inventory."""
    return _bwrap_works()


def provider(mode=None) -> str:
    """Resolve EH_SANDBOX (or an explicit mode) to a concrete provider: 'none'|'bwrap'.
    Never mandates a tool — 'auto'/'bwrap' fall back to 'none' when bwrap can't run."""
    m = (mode or os.environ.get("EH_SANDBOX") or "none").lower()
    if m in ("auto", "bwrap"):
        return "bwrap" if _bwrap_works() else "none"
    return "none"


def _bwrap_wrap(argv, rw_paths, net):
    """Whole root read-only, private /tmp + /dev + /proc, own PID namespace (aids
    reaping); `rw_paths` re-bound read-write (later binds override the RO root).
    net=False drops the network namespace (emu --net user is in-process; Unix sockets
    under a bound-RW dir still work)."""
    pre = ["bwrap", "--die-with-parent", "--unshare-pid",
           "--ro-bind", "/", "/", "--dev-bind", "/dev", "/dev",
           "--proc", "/proc", "--tmpfs", "/tmp"]
    for p in rw_paths:
        sp = str(p)
        if os.path.exists(sp):
            pre += ["--bind", sp, sp]
    if not net:
        pre += ["--unshare-net"]
    return pre + list(argv)


def wrap(argv, rw_paths=(), net=True, mode=None):
    """Return `argv` wrapped by the selected isolation provider ('none' = identity)."""
    if provider(mode) == "bwrap":
        return _bwrap_wrap(argv, rw_paths, net)
    return list(argv)
