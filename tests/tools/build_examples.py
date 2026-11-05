#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""
ESP-Hosted example build orchestrator.

Design goals:
  - Single source of truth for target/transport/example compatibility
  - Shell-agnostic: pluggable runners for bash, fish, or custom shells
  - Parallel builds with proper skip/pass/fail reporting
  - Functions < 30 lines (Clean Code standard)
  - Never writes into examples/: each (example, target) builds in its own scratch
    copy under tests/.work/build_examples/<run>/ (infra/scratch.py, as the test
    harness). Every build's scratch is deleted when it ends. A log is kept only
    for a failed build or one with warnings in ESP-Hosted sources (--no-cleanup
    keeps all logs); report.md, rewritten after each build, lists them. Runs older than
    EH_RETENTION_DAYS (default 7) are removed at startup; the EH_DISK_FLOOR_GB
    floor is checked before every build (infra/lab.py).

Usage:
  python3 build_examples.py --build-targets=esp32c6
  python3 build_examples.py --build-targets=esp32c2,esp32c6 --build-transports=sdio,spi-fd
  python3 build_examples.py --build-targets=esp32c6 --build-examples=wifi/connect,bluetooth/nimble_uart
  python3 build_examples.py --build-targets=all --build-transports=all --shell=fish
  python3 build_examples.py --build-targets=esp32c2 --dry-run
  python3 build_examples.py --list-compat
"""

import argparse
import contextlib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Optional

_TESTS = Path(__file__).resolve().parents[1]
if str(_TESTS) not in sys.path:
    sys.path.insert(0, str(_TESTS))
from infra import caps, lab, scratch  # noqa: E402


# ═══════════════════════════════════════════════════════════════════════════════
# Compatibility matrix — single source of truth
# ═══════════════════════════════════════════════════════════════════════════════

# Roles are explicit: each list is the universe of chips for one side.
# `--build-targets all` resolves against the role this script drives
# (CP today; a future host driver resolves it against ALL_HOST_TARGETS).
ALL_CP_TARGETS = [
    "esp32", "esp32c2", "esp32c3", "esp32c5", "esp32c6", "esp32c61",
    "esp32s2", "esp32s3", "esp32s31",
]
ALL_HOST_TARGETS = ["esp32p4", "esp32h2"]

# IDF lists these under PREVIEW_TARGETS; set-target refuses them without --preview.
PREVIEW_TARGETS: set[str] = {"esp32s31"}


class Transport(Enum):
    SDIO   = "sdio"
    SPI_FD = "spi-fd"
    SPI_HD = "spi-hd"
    UART   = "uart"


TRANSPORT_TARGETS: dict[Transport, set[str]] = {
    Transport.SDIO:   {"esp32", "esp32c5", "esp32c6", "esp32c61"},
    Transport.SPI_FD: set(ALL_CP_TARGETS),
    Transport.SPI_HD: {"esp32s2", "esp32s3", "esp32s31"},
    Transport.UART:   set(ALL_CP_TARGETS),
}

DEFAULT_TRANSPORT: dict[str, Transport] = {
    "esp32": Transport.SDIO, "esp32c2": Transport.SPI_FD,
    "esp32c3": Transport.SPI_FD, "esp32c5": Transport.SDIO,
    "esp32c6": Transport.SDIO, "esp32c61": Transport.SDIO,
    "esp32s2": Transport.SPI_FD, "esp32s3": Transport.SPI_FD,
    "esp32s31": Transport.SPI_FD,
}

TRANSPORT_KCONFIG: dict[Transport, str] = {
    Transport.SDIO:   "EH_TRANSPORT_CP_SDIO",
    Transport.SPI_FD: "EH_TRANSPORT_CP_SPI",
    Transport.SPI_HD: "EH_TRANSPORT_CP_SPI_HD",
    Transport.UART:   "EH_TRANSPORT_CP_UART",
}

# All CP targets currently have Wi-Fi.  Kept as a hook for future
# Wi-Fi-less CP chips; the host-side driver will need its own (h2 etc).
NO_WIFI_TARGETS: set[str] = set()

# Example path substring → excluded chips.
# Verified against IDF Kconfig + soc_caps.
# Patterns are substring-matched against the path relative to repo root,
# and matches union: a path may hit several keys (e.g. a classic-BT
# example matches both `bluetooth/` and its scenario key).
EXAMPLE_EXCLUDE: dict[str, set[str]] = {
    "network_split":   {"esp32c2", "esp32c3"},
    # BLE controller absent on esp32s2 → all Bluetooth examples excluded.
    "bluetooth/":      {"esp32s2"},
    # Classic BT (BR/EDR) is on ESP32 and ESP32-S31 only.
    "classic_bt_discovery": {"esp32c2", "esp32c3", "esp32c5", "esp32c6",
                             "esp32c61", "esp32h2", "esp32s2", "esp32s3"},
    "bt_hid_mouse":         {"esp32c2", "esp32c3", "esp32c5", "esp32c6",
                             "esp32c61", "esp32h2", "esp32s2", "esp32s3"},
    # esp32s31: no host-wakeup GPIO default for host deep sleep.
    "power_save":      {"esp32c2", "esp32s31"},
    "system/mcu_transport_config": {"esp32s31"},
    # esp32s31: IDF v6.1 has no librftest for it.
    "rf_cert":         {"esp32s31"},
    # esp32s31: no default pins for the dedicated spinel UART.
    "openthread/rcp_on_hosted_cp": {"esp32s31"},
    "zigbee/":         {"esp32s31"},
    "wifi/enterprise": {"esp32c2"},
    "wifi/dpp":        {"esp32c2"},
    "wifi/itwt":       {"esp32", "esp32c2", "esp32c3", "esp32s2", "esp32s3"},
    "ext_coex":        {"esp32", "esp32c2"},
}


# ═══════════════════════════════════════════════════════════════════════════════
# CompatMatrix — query helpers
# ═══════════════════════════════════════════════════════════════════════════════

class CompatMatrix:

    @staticmethod
    def get_default_transport(target: str) -> Transport:
        return DEFAULT_TRANSPORT.get(target, Transport.SPI_FD)

    @staticmethod
    def get_supported_transports(target: str) -> list[Transport]:
        return [t for t in Transport if target in TRANSPORT_TARGETS.get(t, set())]

    @staticmethod
    def should_skip_example(example_rel: str, target: str) -> Optional[str]:
        if target in NO_WIFI_TARGETS:
            return f"{target} has no WiFi"
        for pattern, excluded in EXAMPLE_EXCLUDE.items():
            if pattern in example_rel and target in excluded:
                return f"{pattern} not supported on {target}"
        return None

    @staticmethod
    def resolve_targets(spec: str) -> list[str]:
        # `all` resolves to this driver's role universe (CP today).
        if spec == "all":
            return list(ALL_CP_TARGETS)
        targets = [t.strip() for t in spec.split(",")]
        unknown = [t for t in targets if t not in ALL_CP_TARGETS]
        if unknown:
            print(f"WARNING: Unknown CP targets: {unknown}", file=sys.stderr)
        return targets

    @staticmethod
    def resolve_transports(spec: str, target: str) -> list[Transport]:
        if spec == "default":
            return [CompatMatrix.get_default_transport(target)]
        if spec == "all":
            return CompatMatrix.get_supported_transports(target)
        result = []
        for name in (n.strip() for n in spec.split(",")):
            try:
                t = Transport(name)
            except ValueError:
                print(f"WARNING: Unknown transport: {name}", file=sys.stderr)
                continue
            if target in TRANSPORT_TARGETS.get(t, set()):
                result.append(t)
        return result


# ═══════════════════════════════════════════════════════════════════════════════
# Shell runners — pluggable build invocation
# ═══════════════════════════════════════════════════════════════════════════════

class ShellRunner:
    """One build in a staged scratch dir: set-target configures from the copy's
    sdkconfig.defaults, then ninja with a bounded -j (the build_fw recipe)."""
    export_script = ""
    join = " && "

    def __init__(self, idf_path: Optional[str] = None):
        self.idf_path = idf_path or os.environ.get(
            "IDF_PATH", os.path.expanduser("~/esp-idf"))

    def argv(self, cmd: str) -> list[str]:
        raise NotImplementedError

    def env(self, target: str) -> str:
        raise NotImplementedError

    def run(self, cmd: str, cwd: str, timeout: int = 600) -> tuple[int, str]:
        try:
            r = subprocess.run(self.argv(cmd), cwd=cwd,
                               capture_output=True, text=True, timeout=timeout)
            return r.returncode, r.stdout + r.stderr
        except subprocess.TimeoutExpired:
            return 1, f"TIMEOUT after {timeout}s"
        except Exception as e:
            return 1, str(e)

    def idf_build(self, work_dir: str, target: str, ninja_jobs: int,
                  timeout: int = 900) -> tuple[int, str]:
        pv = "--preview " if target in PREVIEW_TARGETS else ""
        parts = [f'source "{self.idf_path}/{self.export_script}" > /dev/null 2>&1',
                 f"{self.env(target)}idf.py {pv}set-target {target} 2>&1",
                 f"ninja -C build -j {ninja_jobs} all 2>&1"]
        return self.run(self.join.join(parts), cwd=work_dir, timeout=timeout)


class BashRunner(ShellRunner):
    export_script = "export.sh"

    def argv(self, cmd: str) -> list[str]:
        return ["bash", "-c", cmd]

    def env(self, target: str) -> str:
        return f"IDF_TARGET={target} "


class FishRunner(ShellRunner):
    export_script = "export.fish"
    join = "; and "

    def argv(self, cmd: str) -> list[str]:
        return ["fish", "-c", cmd]

    def env(self, target: str) -> str:
        return f"set -x IDF_TARGET {target}; and "


SHELL_RUNNERS: dict[str, type[ShellRunner]] = {
    "bash": BashRunner, "fish": FishRunner,
}


# ═══════════════════════════════════════════════════════════════════════════════
# Build result
# ═══════════════════════════════════════════════════════════════════════════════

class Status(Enum):
    OK = "OK"; FAIL = "FAIL"; SKIP = "SKIP"
    INFRA = "INFRA"  # the build could not run (disk full); not a product failure


@dataclass
class BuildResult:
    example: str
    target: str
    transport: str = ""
    status: Status = Status.SKIP
    duration_s: float = 0.0
    warnings: int = 0
    hosted_warnings: int = 0
    reason: str = ""
    log_path: str = ""


# ═══════════════════════════════════════════════════════════════════════════════
# Discovery + detection
# ═══════════════════════════════════════════════════════════════════════════════

SKIP_DIRS = {"/build/", "/managed_components/", "/components/", "/main/"}


def discover_examples(examples_root: Path) -> list[Path]:
    projects = []
    for cmake in examples_root.rglob("CMakeLists.txt"):
        d = cmake.parent
        rel = str(d.relative_to(examples_root))
        if any(s in rel for s in SKIP_DIRS):
            continue
        if (d / "sdkconfig.defaults").exists() and (d / "main" / "CMakeLists.txt").exists():
            projects.append(d)
    return sorted(projects)


def detect_transport(project_dir: str, target: str) -> str:
    for name in [f"sdkconfig.defaults.{target}", "sdkconfig.defaults"]:
        path = os.path.join(project_dir, name)
        if not os.path.exists(path):
            continue
        with open(path) as f:
            content = f.read()
        for transport, kconfig in TRANSPORT_KCONFIG.items():
            if f"CONFIG_{kconfig}=y" in content:
                return transport.value
    return CompatMatrix.get_default_transport(target).value


# ═══════════════════════════════════════════════════════════════════════════════
# Project-dir helpers
# ═══════════════════════════════════════════════════════════════════════════════

def _repo_root() -> str:
    # build_examples.py lives at tests/tools/ — repo root is three up.
    return str(Path(__file__).resolve().parents[2])


def stage(project_dir: Path, work: Path, repo_root: Path) -> Path:
    """Copy the example into `work` and return the dir to build in. A sibling
    common/ (shared across roles) is copied beside it so ../common resolves as
    in-tree. The esp_hosted component is a symlink to the repo, made in the copy
    only; absolutize_overrides then drops the override_path that would make the
    component manager copy the whole repo into the build."""
    has_common = (project_dir.parent / "common").is_dir()
    wd = work / project_dir.name if has_common else work
    scratch.copy_scratch(project_dir, wd, scratch.ignore(), dirs_exist_ok=True)
    if has_common:
        scratch.copy_scratch(project_dir.parent / "common", work / "common",
                             scratch.ignore(), dirs_exist_ok=True)
    link = wd / "components" / "esp_hosted"
    if not os.path.lexists(link):
        link.parent.mkdir(parents=True, exist_ok=True)
        link.symlink_to(repo_root)
    scratch.absolutize_overrides(project_dir, wd)
    return wd


# IDF transient files that litter example source directories.
# ── ANSI colors ──────────────────────────────────────────────────────────────
class C:
    RED     = "\033[91m"
    GREEN   = "\033[92m"
    YELLOW  = "\033[93m"
    CYAN    = "\033[96m"
    DIM     = "\033[2m"
    BOLD    = "\033[1m"
    RESET   = "\033[0m"


_TRANSIENT_PATTERNS = ["build", "sdkconfig", "sdkconfig.old",
                       "managed_components", "dependencies.lock"]

# Directories that are always transient (safe to delete without confirmation)
_TRANSIENT_DIRS = {"build", "managed_components", "__pycache__"}

# Repo-wide transients: scanned from repo root, not just examples_root
# __pycache__ lives mostly in tests/ so needs wider scope.
_REPO_WIDE_TRANSIENT_DIRS = {"__pycache__"}
_REPO_WIDE_SKIP_DIRS = {".git", "workspace", "managed_components", "build",
                        "node_modules", ".venv", "venv"}

# Directories that MAY contain user code — require explicit confirmation
_CONFIRM_DIRS = {"components"}


def _find_symlinks(directory: Path) -> list[Path]:
    """Find symlinks inside a directory (non-recursive, top-level only)."""
    if not directory.is_dir():
        return []
    return [child for child in directory.iterdir() if child.is_symlink()]


def _find_transient_dirs(examples_root: Path) -> tuple[list[Path], list[Path]]:
    """Find transient dirs and component dirs separately.
    Returns (auto_clean_dirs, confirm_dirs)."""
    auto = []
    confirm = []
    for pattern in _TRANSIENT_DIRS:
        for hit in examples_root.rglob(pattern):
            if not hit.is_dir():
                continue
            # Skip dirs nested inside other transients (build/managed_components/...)
            if any(p.name in _TRANSIENT_DIRS for p in hit.parents
                   if p != examples_root):
                continue
            auto.append(hit)
    for pattern in _CONFIRM_DIRS:
        for hit in examples_root.rglob(pattern):
            if not hit.is_dir():
                continue
            if any(p.name in _TRANSIENT_DIRS for p in hit.parents):
                continue
            confirm.append(hit)
    return sorted(auto), sorted(confirm)


def _find_transient_files(examples_root: Path) -> list[Path]:
    """Find transient files (sdkconfig, dependencies.lock, etc.)."""
    files = []
    for pattern in ("sdkconfig", "sdkconfig.old", "dependencies.lock"):
        for hit in examples_root.rglob(pattern):
            if hit.is_file() and not any(
                    p.name in _TRANSIENT_DIRS for p in hit.parents):
                files.append(hit)
    return sorted(files)


def _print_dir_tree(dirs: list[Path], examples_root: Path, label: str,
                    warn: bool = False):
    """Print directory list with color coding."""
    color = C.YELLOW if warn else C.CYAN
    print(f"\n{color}{C.BOLD}{label} ({len(dirs)}){C.RESET}")
    for d in dirs:
        rel = d.relative_to(examples_root)
        symlinks = _find_symlinks(d)
        sym_info = ""
        if symlinks:
            targets = [f"{C.DIM}{s.name} -> {os.readlink(s)}{C.RESET}" for s in symlinks]
            sym_info = f"  {C.CYAN}[symlinks: {', '.join(targets)}{C.CYAN}]{C.RESET}"
        print(f"  {C.RED}{rel}/{C.RESET}{sym_info}")


def _find_repo_wide_pycache(repo_root: Path) -> list[Path]:
    """Find __pycache__ dirs across repo (skipping .git, workspace, etc.)."""
    hits = []
    for d in repo_root.rglob("__pycache__"):
        if not d.is_dir():
            continue
        # Skip if any parent is in the skip list
        if any(p.name in _REPO_WIDE_SKIP_DIRS for p in d.parents
               if p != repo_root):
            continue
        hits.append(d)
    return sorted(hits)


def clean_repo_wide_pycache(repo_root: Path) -> int:
    """Remove __pycache__ dirs from entire repo (tests/, tools/, etc.)."""
    hits = _find_repo_wide_pycache(repo_root)
    if not hits:
        return 0
    _print_dir_tree(hits, repo_root, "Python __pycache__ (repo-wide)")
    for d in hits:
        shutil.rmtree(d, ignore_errors=True)
    print(f"  {C.GREEN}✓ Removed {len(hits)} __pycache__ dirs.{C.RESET}")
    return len(hits)


def clean_example_transients(examples_root: Path, confirm: bool = True) -> int:
    """Remove IDF build transients + component dirs from example dirs.

    - build/, managed_components/: deleted automatically
    - sdkconfig, sdkconfig.old, dependencies.lock: deleted automatically
    - components/: shown with symlink details, requires user typing 'YES'
    - Symlinks inside components/ are unlinked (NOT followed/deleted)
    """
    auto_dirs, confirm_dirs = _find_transient_dirs(examples_root)
    transient_files = _find_transient_files(examples_root)

    if not auto_dirs and not confirm_dirs and not transient_files:
        print("Nothing to clean.")
        return 0

    removed = 0

    # Step 1: Auto-clean transient dirs + files
    if auto_dirs or transient_files:
        _print_dir_tree(auto_dirs, examples_root, "Transient directories (auto-clean)")
        print(f"\n  {C.DIM}+ {len(transient_files)} transient files "
              f"(sdkconfig, dependencies.lock, etc.){C.RESET}")
        for d in auto_dirs:
            shutil.rmtree(d)
            removed += 1
        for f in transient_files:
            f.unlink()
            removed += 1
        print(f"  {C.GREEN}✓ Removed {removed} items.{C.RESET}")

    # Step 2: Component dirs — require explicit 'YES'
    if confirm_dirs:
        _print_dir_tree(confirm_dirs, examples_root,
                        "⚠️  Component directories (MAY contain your code)", warn=True)
        print(f"\n  {C.RED}{C.BOLD}These directories will be DELETED.{C.RESET}")
        print(f"  {C.DIM}Symlinks inside will be unlinked "
              f"(target repos are NOT deleted).{C.RESET}")

        if confirm:
            ans = input(f"\n  Type '{C.RED}YES{C.RESET}' to delete, "
                        f"anything else to skip: ").strip()
            if ans != "YES":
                print(f"  {C.YELLOW}Skipped component directories.{C.RESET}")
                return removed

        for d in confirm_dirs:
            for sym in _find_symlinks(d):
                sym.unlink()
            if d.exists():
                shutil.rmtree(d)
            removed += 1
        print(f"  {C.GREEN}✓ Removed {len(confirm_dirs)} component directories.{C.RESET}")

    return removed


# ═══════════════════════════════════════════════════════════════════════════════
# Build single
# ═══════════════════════════════════════════════════════════════════════════════


_WARNING = re.compile(r"^(/[^:\n]+):\d+(?::\d+)?: warning:", re.M)


def count_hosted_warnings(output: str, roots: list[str]) -> int:
    """Compiler warnings in ESP-Hosted sources: the repo and the example's
    scratch copy, not IDF, managed components or generated build files."""
    n = 0
    for path in _WARNING.findall(output):
        if any(path.startswith(r) for r in roots) and \
                not any(s in path for s in ("/managed_components/", "/build/")):
            n += 1
    return n


def build_single(project_dir: str, target: str, shell: str, idf_path: str,
                 run_dir: str, export_root: Optional[str],
                 keep_logs: bool = False) -> BuildResult:
    repo_root = Path(_repo_root())
    rel = os.path.relpath(project_dir, repo_root)
    transport = detect_transport(project_dir, target)
    skip = CompatMatrix.should_skip_example(rel, target)
    if skip:
        return BuildResult(rel, target, transport, Status.SKIP, reason=skip)

    short = rel.replace("examples/", "", 1).replace("/", "__")
    log_path = Path(run_dir) / "logs" / f"{short}__{target}.log"
    work = Path(run_dir) / "builds" / short / target
    try:
        lab.ensure_free(f"build {rel} [{target}]")
    except lab.DiskExhausted as e:
        return BuildResult(rel, target, transport, Status.INFRA, reason=str(e))

    t0 = time.time()
    wd = stage(Path(project_dir), work, repo_root)
    runner = SHELL_RUNNERS.get(shell, BashRunner)(idf_path=idf_path)
    with lab.build_slot():
        jobs = caps.build_jobs(lab.build_capacity(), lab.live_emu_pairs())
        code, output = runner.idf_build(str(wd), target, jobs)
    log_path.write_text(output)
    r = BuildResult(rel, target, transport, Status.OK if code == 0 else Status.FAIL,
                    time.time() - t0, output.count("warning:"),
                    count_hosted_warnings(output, [str(repo_root) + "/", str(work) + "/"]),
                    log_path=str(log_path))
    if r.status == Status.FAIL and lab.is_enospc(output):
        r.status, r.reason = Status.INFRA, "no space left on device"
    if r.status == Status.OK and export_root:
        export_artifacts(r, wd, Path(export_root), repo_root, idf_path)
    shutil.rmtree(work, ignore_errors=True)
    with contextlib.suppress(OSError):
        work.parent.rmdir()  # the example's dir, once its last target is gone
    if r.status == Status.OK and not r.hosted_warnings and not keep_logs:
        log_path.unlink(missing_ok=True)
        r.log_path = ""
    return r


# ═══════════════════════════════════════════════════════════════════════════════
# Reporting
# ═══════════════════════════════════════════════════════════════════════════════

def _print_result_line(r: BuildResult):
    dur = f"{r.duration_s:.0f}s" if r.duration_s else "-"
    warn = str(r.warnings) if r.status != Status.SKIP else "-"
    tr = r.transport or "-"
    extra = f"  ({r.reason})" if r.status in (Status.SKIP, Status.INFRA) \
        else f"  -> {r.log_path}" if r.status == Status.FAIL else ""
    print(f"  {r.status.value:<8} {r.target:<12} {tr:<8} {dur:>5}  {warn:>5}  {r.example}{extra}")


def print_report(results: list[BuildResult], output_dir: str):
    print(f"\n{'=' * 80}")
    print(f"  Build summary (logs in {output_dir})")
    print(f"{'=' * 80}")
    print(f"  {'STATUS':<8} {'TARGET':<12} {'TRANSPORT':<8} {'DUR':>5}  {'WARN':>5}  PATH")
    print("-" * 80)
    for r in sorted(results, key=lambda x: (x.status.value, x.target, x.example)):
        _print_result_line(r)
    _print_totals(results)


def _print_totals(results: list[BuildResult]):
    ok = sum(1 for r in results if r.status == Status.OK)
    fail = sum(1 for r in results if r.status == Status.FAIL)
    skip = sum(1 for r in results if r.status == Status.SKIP)
    infra = sum(1 for r in results if r.status == Status.INFRA)
    print(f"-" * 80)
    print(f"  Total: OK={ok}  FAIL={fail}  SKIP={skip}"
          + (f"  INFRA={infra}" if infra else "") + "\n")
    if fail:
        print("  Failed builds:")
        for r in results:
            if r.status == Status.FAIL:
                print(f"    {r.example} [{r.target}] -> {r.log_path}")
        print()


def write_report_md(results: list[BuildResult], output_dir: Path, total: int):
    """Short run summary, rewritten after each build so it is current mid-run."""
    count = lambda st: sum(1 for r in results if r.status == st)
    lines = [f"# Example builds: {len(results)}/{total} done",
             "",
             f"OK {count(Status.OK)} · FAIL {count(Status.FAIL)} · "
             f"SKIP {count(Status.SKIP)} · INFRA {count(Status.INFRA)}",
             ""]
    rows = [r for r in results if r.status in (Status.FAIL, Status.INFRA) or r.hosted_warnings]
    if rows:
        lines += ["| Status | Target | Example | Hosted warnings | Log / reason |",
                  "|---|---|---|---|---|"]
        for r in sorted(rows, key=lambda x: (x.status.value, x.example, x.target)):
            where = os.path.basename(r.log_path) if r.log_path else r.reason
            lines.append(f"| {r.status.value} | {r.target} | {r.example} | "
                         f"{r.hosted_warnings} | {where} |")
    tmp = output_dir / "report.md.tmp"
    tmp.write_text("\n".join(lines) + "\n")
    tmp.replace(output_dir / "report.md")


def save_json_report(results: list[BuildResult], output_dir: Path):
    with open(output_dir / "report.json", "w") as f:
        json.dump([{
            "example": r.example, "target": r.target, "transport": r.transport,
            "status": r.status.value, "duration_s": round(r.duration_s, 1),
            "warnings": r.warnings, "hosted_warnings": r.hosted_warnings, "reason": r.reason, "log_path": r.log_path,
        } for r in results], f, indent=2)


# ═══════════════════════════════════════════════════════════════════════════════
# Artifact export
# ═══════════════════════════════════════════════════════════════════════════════

def _md5_file(path: Path) -> str:
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b''):
            h.update(chunk)
    return h.hexdigest()


def _get_idf_version(idf_path: str) -> Optional[str]:
    ver_cmake = Path(idf_path) / "tools" / "cmake" / "version.cmake"
    if not ver_cmake.exists():
        return None
    content = ver_cmake.read_text()
    parts = {}
    for line in content.splitlines():
        for key in ("IDF_VERSION_MAJOR", "IDF_VERSION_MINOR", "IDF_VERSION_PATCH"):
            if f"set({key}" in line:
                val = line.split(key)[-1].strip().rstrip(")").strip()
                parts[key] = val
    if parts:
        return ".".join(parts.get(k, "?") for k in
                        ("IDF_VERSION_MAJOR", "IDF_VERSION_MINOR", "IDF_VERSION_PATCH"))
    return None


def _get_git_commit(repo_root: Path) -> Optional[str]:
    try:
        r = subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(repo_root),
                           capture_output=True, text=True, timeout=5)
        return r.stdout.strip() if r.returncode == 0 else None
    except Exception:
        return None


def _get_hosted_version(repo_root: Path) -> Optional[str]:
    yml = repo_root / "idf_component.yml"
    if not yml.exists():
        return None
    for line in yml.read_text().splitlines():
        if line.strip().startswith("version:"):
            return line.split(":", 1)[1].strip().strip('"')
    return None


def _collect_extra_info(repo_root: Path, idf_path: str) -> dict:
    info = {}
    v = _get_idf_version(idf_path)
    if v:
        info["idf_version"] = v
    c = _get_git_commit(repo_root)
    if c:
        info["eh_commit"] = c
    v = _get_hosted_version(repo_root)
    if v:
        info["eh_version"] = v
    return info


def _parse_flash_args(build_dir: Path) -> list[tuple[str, str]]:
    fa = build_dir / "flash_args"
    if not fa.exists():
        return []
    tokens = fa.read_text().strip().split()
    pairs = []
    i = 0
    while i < len(tokens):
        if tokens[i].startswith("0x") and i + 1 < len(tokens) \
                and not tokens[i + 1].startswith("-"):
            pairs.append((tokens[i], tokens[i + 1]))
            i += 2
        else:
            i += 1
    return pairs


def _copy_bins(build_dir: Path, dest: Path, flash_pairs: list) -> list:
    copied = []
    for offset, bin_name in flash_pairs:
        src = build_dir / bin_name
        if src.exists():
            shutil.copy2(src, dest / src.name)
            copied.append((offset, src.name))
    return copied


def _write_md5sums(dest: Path):
    with open(dest / "md5sums.txt", "w") as mf:
        for f in sorted(dest.glob("*.bin")):
            mf.write(f"{_md5_file(f)}  {f.name}\n")


def _write_flash_command(dest: Path, target: str, short: str, parts: list):
    with open(dest / "flash_command.txt", "w") as fc:
        fc.write(f"# Flash command for {short} on {target}\n")
        fc.write(f"esptool.py --chip {target} -b 460800 "
                 f"--before default_reset --after hard_reset "
                 f"write_flash --flash_mode dio --flash_freq 80m \\\n")
        for offset, name in parts:
            fc.write(f"  {offset} {name} \\\n")
        fc.write("\n")


def _write_extra_info(dest: Path, repo_root: Path, idf_path: str, work_dir: Path):
    extra_dir = dest / "extra_info"
    extra_dir.mkdir(exist_ok=True)
    for key, val in _collect_extra_info(repo_root, idf_path).items():
        (extra_dir / f"{key}.txt").write_text(val + "\n")
    if (work_dir / "sdkconfig").is_file():
        shutil.copy2(work_dir / "sdkconfig", extra_dir / "sdkconfig")


def export_artifacts(result: BuildResult, work_dir: Path, export_root: Path,
                     repo_root: Path, idf_path: str) -> int:
    short = result.example.replace("examples/", "", 1)
    dest = export_root / short / result.target
    dest.mkdir(parents=True, exist_ok=True)
    copied = _copy_bins(work_dir / "build", dest, _parse_flash_args(work_dir / "build"))
    _write_md5sums(dest)
    _write_flash_command(dest, result.target, short, copied)
    _write_extra_info(dest, repo_root, idf_path, work_dir)
    return len(copied)


# ═══════════════════════════════════════════════════════════════════════════════
# CLI — argument parsing
# ═══════════════════════════════════════════════════════════════════════════════

def parse_args():
    p = argparse.ArgumentParser(
        description="ESP-Hosted example build orchestrator",
        formatter_class=argparse.RawDescriptionHelpFormatter)

    _skip_targets = "--list-compat" in sys.argv or "--clean" in sys.argv
    p.add_argument("--build-targets", required=not _skip_targets,
                   metavar="TARGETS",
                   help="Comma-separated chip targets, or 'all' (e.g. --build-targets=esp32c6)")
    p.add_argument("--build-transports", default="default", metavar="TRANSPORTS",
                   help="sdio,spi-fd,spi-hd,uart,all,default (e.g. --build-transports=sdio)")
    p.add_argument("--build-examples", default="all", metavar="EXAMPLES",
                   help="Comma-separated example substrings, or 'all'")
    p.add_argument("--jobs", type=int, default=4, metavar="N",
                   help="Parallel build jobs (default: 4)")
    p.add_argument("--filter", default="", metavar="REGEX",
                   help="Regex filter on example path")
    p.add_argument("--build-dir", default=None, metavar="DIR",
                   help="Run directory for logs, failed-build scratch and report "
                        "(default: tests/.work/build_examples/<timestamp>, removed after "
                        "EH_RETENTION_DAYS)")
    p.add_argument("--export-fw-bins-dir", default=None, metavar="DIR",
                   help="Export firmware binaries + metadata per example/target")
    p.add_argument("--shell", default="bash", metavar="SHELL",
                   choices=sorted(SHELL_RUNNERS.keys()),
                   help="Shell for idf.py: bash, fish (default: bash)")
    p.add_argument("--idf-path", default=None, metavar="PATH",
                   help="ESP-IDF path (default: ~/esp-idf or $IDF_PATH)")
    p.add_argument("--list-compat", action="store_true",
                   help="Print compatibility matrix and exit")
    p.add_argument("--no-cleanup", action="store_true",
                   help="Keep the logs of passing builds too (build dirs are still deleted)")
    p.add_argument("--dry-run", action="store_true",
                   help="Show what would be built without building")
    p.add_argument("--clean", action="store_true",
                   help="Remove IDF build transients (build/, sdkconfig, managed_components, "
                        "dependencies.lock) from all example dirs and exit")
    return p.parse_args()


# ═══════════════════════════════════════════════════════════════════════════════
# CLI — list-compat
# ═══════════════════════════════════════════════════════════════════════════════

def cmd_list_compat():
    print("\nTransport compatibility:")
    for t in Transport:
        print(f"  {t.value:<8} -> {', '.join(sorted(TRANSPORT_TARGETS[t]))}")
    print("\nDefault transport per CP chip:")
    for chip in ALL_CP_TARGETS:
        print(f"  {chip:<12} -> {DEFAULT_TRANSPORT.get(chip, Transport.SPI_FD).value}")
    print(f"\nHost-side targets (driven by separate host script): "
          f"{', '.join(ALL_HOST_TARGETS)}")
    print("\nExample exclusions:")
    for pat, chips in EXAMPLE_EXCLUDE.items():
        print(f"  {pat:<30} NOT on {', '.join(sorted(chips))}")
    print(f"\nNo-WiFi chips (skip all): {', '.join(sorted(NO_WIFI_TARGETS))}")


# ═══════════════════════════════════════════════════════════════════════════════
# CLI — setup
# ═══════════════════════════════════════════════════════════════════════════════

def setup_output(args, repo_root: Path) -> Path:
    """Sweep expired runs, check the disk floor, then create this run's dir."""
    lab.sweep_expired()
    lab.ensure_free("build_examples start")
    if args.build_dir:
        run_dir = Path(args.build_dir)
        if not run_dir.is_absolute():
            run_dir = repo_root / run_dir
    else:
        run_dir = lab.BUILD_EXAMPLES / time.strftime("%Y%m%d-%H%M%S")
    (run_dir / "logs").mkdir(parents=True, exist_ok=True)
    lab.touch_last_used(run_dir)
    return run_dir


def collect_projects(args, examples_root: Path, repo_root: Path) -> list[Path]:
    projects = discover_examples(examples_root)
    if args.build_examples != "all":
        subs = [s.strip() for s in args.build_examples.split(",")]
        projects = [p for p in projects if any(s in str(p) for s in subs)]
    if args.filter:
        pat = re.compile(args.filter)
        projects = [p for p in projects if pat.search(str(p.relative_to(repo_root)))]
    return projects


def make_jobs(projects: list[Path], targets: list[str]) -> list[tuple[str, str]]:
    return [(str(p), t) for p in projects for t in targets]


# ═══════════════════════════════════════════════════════════════════════════════
# CLI — execute
# ═══════════════════════════════════════════════════════════════════════════════

def print_live(result: BuildResult):
    if result.status == Status.SKIP:
        print(f"[SKIP] {result.example} [{result.target}] ({result.reason})")
    elif result.status == Status.INFRA:
        print(f"[INFRA] {result.example} [{result.target}] ({result.reason})")
    elif result.status == Status.OK:
        print(f"[OK]   {result.example} [{result.target}] "
              f"({result.duration_s:.0f}s, {result.warnings} warnings)")
    else:
        print(f"[FAIL] {result.example} [{result.target}] "
              f"({result.duration_s:.0f}s, see {result.log_path})")


def execute_builds(jobs, args, output_dir, idf_path, repo_root,
                   export_root) -> list[BuildResult]:
    """Run builds in parallel. An INFRA result (disk full) cancels the builds not
    yet started: they would fail the same way and bury the cause."""
    results = []
    workers = max(1, min(args.jobs, caps.build_capacity()))
    with ProcessPoolExecutor(max_workers=workers) as pool:
        futures = {
            pool.submit(build_single, p, t, args.shell, idf_path, str(output_dir),
                        export_root, args.no_cleanup): (p, t)
            for p, t in jobs
        }
        for future in as_completed(futures):
            p, t = futures[future]
            try:
                result = future.result()
            except Exception as e:
                rel = os.path.relpath(p, repo_root)
                result = BuildResult(rel, t, "", Status.FAIL, reason=str(e))
            results.append(result)
            print_live(result)
            write_report_md(results, Path(output_dir), len(jobs))
            if result.status == Status.INFRA:
                pool.shutdown(wait=True, cancel_futures=True)
                break
    return results


# ═══════════════════════════════════════════════════════════════════════════════
# main
# ═══════════════════════════════════════════════════════════════════════════════

def main():
    args = parse_args()

    if args.list_compat:
        cmd_list_compat()
        return

    repo_root = Path(__file__).resolve().parents[2]
    examples_root = repo_root / "examples"

    if args.clean:
        n = clean_example_transients(examples_root, confirm=True)
        n += clean_repo_wide_pycache(repo_root)
        print(f"\nTotal cleaned: {n} items.")
        return

    try:
        output_dir = setup_output(args, repo_root)
    except lab.DiskExhausted as e:
        print(f"INFRA: {e}", file=sys.stderr)
        sys.exit(2)
    idf_path = args.idf_path or os.environ.get("IDF_PATH", os.path.expanduser("~/esp-idf"))
    targets = CompatMatrix.resolve_targets(args.build_targets)

    projects = collect_projects(args, examples_root, repo_root)
    if not projects:
        print("No examples matched.", file=sys.stderr)
        return

    jobs = make_jobs(projects, targets)
    print(f"==> {len(projects)} example(s) x {len(targets)} target(s) = {len(jobs)} build(s)")
    print(f"==> Targets: {', '.join(targets)}")
    print(f"==> Transports: {args.build_transports}")
    print(f"==> Shell: {args.shell}, Jobs: {args.jobs} (cap {caps.build_capacity()})")
    print(f"==> Output: {output_dir}\n")

    if args.dry_run:
        for p, t in jobs:
            rel = os.path.relpath(p, repo_root)
            skip = CompatMatrix.should_skip_example(rel, t)
            print(f"  {'[SKIP: ' + skip + ']' if skip else '[BUILD]'} {rel} [{t}]")
        return

    export_root = None
    if args.export_fw_bins_dir:
        export_root = Path(args.export_fw_bins_dir)
        if not export_root.is_absolute():
            export_root = repo_root / export_root
        export_root = str(export_root)
    results = execute_builds(jobs, args, output_dir, idf_path, repo_root, export_root)

    print_report(results, str(output_dir))
    save_json_report(results, output_dir)

    sys.exit(1 if any(r.status in (Status.FAIL, Status.INFRA) for r in results) else 0)


if __name__ == "__main__":
    main()
