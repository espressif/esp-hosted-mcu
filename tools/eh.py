#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
# SPDX-License-Identifier: Apache-2.0
"""eh — orchestrator for esp_hosted Linux-side builds.

CLI surface is a strict subset of `idf.py`, Linux-biased.  eh.py
is a **dispatcher only**: classification + command routing.  No
Python-side Kconfig parsing, no projbuild discovery, no sdkconfig
generation — all of that is owned by CMake via
`tools/cmake/hosted_kconfig.cmake`, which calls out to the
vendored `esp-idf-kconfig` backend (kconfgen + prepare_kconfig_files
+ menuconfig).

Behaviour rules:

  * IDF project (CMakeLists has `include($ENV{IDF_PATH}/tools/cmake/project.cmake)`):
    eh.py `execvp`s into `idf.py` with this process's argv.  Same
    PID handoff, same exit code, same stdio.  eh.py never tries
    to second-guess idf.py's command surface.

  * Hosted Linux project (CMakeLists has `add_executable(...)` +
    `target_link_libraries(... esp_hosted)`, OR `main.py` imports
    `eh_host`): eh.py runs the matching command locally.

  * Anything else: hard reject — point the user at the two
    recognised project shapes.

Commands (each one maps 1:1 to a CMake invocation; no Python kconfig
work happens here):

    eh set-target <port>     persist .eh.target; `cmake -S . -B build`
                                 wires up hosted_kconfig.cmake's defconfig.
    eh defconfig             `cmake -S . -B build` (re-runs Kconfig
                                 with --defaults applied).
    eh reconfigure           `cmake -S . -B build` (idempotent).
    eh menuconfig            `cmake --build build --target menuconfig`.
    eh save-defconfig        `cmake --build build --target save-defconfig`.
    eh build                 `cmake -S . -B build` + `cmake --build build`.
    eh run / monitor         build + exec the linked binary.
    eh clean / fullclean     wipe build artefacts.
    eh show                  diagnostic.

Output paths (identical to idf.py):

    <project>/sdkconfig                    user-persisted Kconfig choices
    <project>/build/config/sdkconfig.h     `-include` artefact
    <project>/build/config/sdkconfig.cmake CMake `set(CONFIG_*)` mirror
    <project>/.eh.target               one-line state file
"""

from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

# Box-drawing / ✓✗ output crashes on a non-UTF-8 terminal (e.g. the RPi's latin-1
# locale: UnicodeEncodeError). Emit UTF-8 where the stream allows, and never crash
# on an unencodable char.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="backslashreplace")
    except (AttributeError, ValueError, OSError):
        pass


REPO_ROOT      = Path(__file__).resolve().parent.parent
TOOLS_DIR      = Path(__file__).resolve().parent
DEFAULTS_DIR   = TOOLS_DIR

VALID_TARGETS  = ("linux",)        # extends as stm32 / zephyr land
TARGET_ALIASES = {"posix": "linux"}       # back-compat alias — `linux` is the canonical name
DEFAULT_TARGET = "linux"


class _PathCfg:
    def __init__(self, project_dir: Path, build_dir: Path, kconfig_dir: Path,
                 bin_dir: Path | None = None):
        self.project_dir = project_dir
        self.build_dir = build_dir
        self.kconfig_dir = kconfig_dir
        self.sdkconfig = project_dir / "sdkconfig"
        self.target_file = project_dir / ".eh.target"
        self.build_cfg_dir = build_dir / "config"
        # bin_dir holds the final linked executable(s) — idf.py keeps
        # those in build/ next to all the intermediate artefacts; we
        # split them off into a sibling `bin/` so it's trivial to
        # `./bin/<demo>` after a build.
        self.bin_dir = bin_dir if bin_dir is not None else project_dir / "bin"


_PATH_CFG: _PathCfg | None = None


def _paths() -> _PathCfg:
    assert _PATH_CFG is not None, "path config must be initialized in main()"
    return _PATH_CFG


def _read_target() -> str | None:
    target_file = _paths().target_file
    if target_file.exists():
        return target_file.read_text().strip() or None
    return None


def _require_target() -> str:
    """Read the persisted target, defaulting to DEFAULT_TARGET (`linux`)
    if none is set.  Auto-persist so subsequent invocations are stable."""
    target = _read_target()
    if not target:
        target = DEFAULT_TARGET
        _paths().target_file.write_text(target + "\n")
        print(f"eh: defaulting target to '{target}' (no .eh.target found)")
    target = TARGET_ALIASES.get(target, target)
    return target


def _defaults_file(target: str) -> Path:
    return DEFAULTS_DIR / f"sdkconfig.defaults.{target}"


# All Kconfig pipeline work — projbuild discovery, defaults layering,
# sdkconfig.h / sdkconfig.cmake generation, menuconfig orchestration —
# is owned by `tools/cmake/hosted_kconfig.cmake`.
    print(f"eh: wrote {sdkconfig_cmake}")


# ── subcommands ─────────────────────────────────────────────────────

def _resolve_target_arg(arg_target: str) -> str | None:
    """Normalise the requested target string (alias expansion + valid
    check).  Returns None on failure (after writing an error message)."""
    target = TARGET_ALIASES.get(arg_target, arg_target)
    if target != arg_target:
        sys.stderr.write(f"eh: target alias {arg_target!r} → {target!r}\n")
    if target not in VALID_TARGETS:
        sys.stderr.write(
            f"eh: unknown target {arg_target!r}; valid: "
            f"{', '.join(VALID_TARGETS)}\n"
        )
        return None
    return target


def _cmake_target(build_dir: Path, target: str) -> int:
    """Build a single named CMake target — used for `menuconfig`,
    `save-defconfig`, `confserver` etc. (the Kconfig-pipeline targets
    that `hosted_kconfig.cmake` registers).  No `-j`: these are
    interactive or single-step, never parallel work.

    Pass EH_PATH through to cmake.  `cmake --build` triggers
    nested re-configure when CMAKE_CONFIGURE_DEPENDS sees changes;
    that nested cmake re-evaluates the example's
    `include($ENV{EH_PATH}/...)` line and needs the env var
    set.  Without this, menuconfig/save-defconfig fail with an empty
    include path on shells that haven't sourced export.sh.
    """
    cmd = ["cmake", "--build", str(build_dir), "--target", target]
    env = os.environ.copy()
    env.setdefault("EH_PATH", str(REPO_ROOT))
    print(f"eh: {' '.join(cmd)}")
    return subprocess.run(cmd, env=env).returncode


def _ensure_configured(build_dir: Path) -> int:
    """Cheap idempotent check: if CMakeCache.txt is missing, run
    `cmake configure` once.  No-op when already configured."""
    if (build_dir / "CMakeCache.txt").exists():
        return 0
    return _cmake_configure(build_dir)


def cmd_set_target(args) -> int:
    """Persist the target file, then drive a `cmake configure` so
    `hosted_kconfig.cmake` runs the Kconfig pipeline (defconfig +
    sdkconfig.h/cmake emission).  Mirrors `idf.py set-target`."""
    target = _resolve_target_arg(args.target)
    if target is None:
        return 1
    args.target = target
    p = _paths()
    prior = _read_target()
    # IDF parity: when changing target, preserve the user's prior
    # sdkconfig as sdkconfig.old before defconfig overwrites it.
    if prior and prior != target and p.sdkconfig.exists():
        backup = p.project_dir / "sdkconfig.old"
        print(f"eh: {p.sdkconfig} -> {backup}")
        p.sdkconfig.replace(backup)
    p.target_file.write_text(target + "\n")
    print(f"eh: target set to {target}")
    return _cmake_configure(p.build_dir)


def cmd_defconfig(args) -> int:
    """Re-run `cmake configure`; `hosted_kconfig.cmake` re-reads
    sdkconfig.defaults overlays and re-runs kconfgen."""
    _require_target()
    return _cmake_configure(_paths().build_dir)


def cmd_menuconfig(args) -> int:
    """Invoke the `menuconfig` CMake target registered by
    `hosted_kconfig.cmake`.  Auto-configures first if needed."""
    _require_target()
    rc = _ensure_configured(_paths().build_dir)
    if rc != 0:
        return rc
    return _cmake_target(_paths().build_dir, "menuconfig")


def cmd_save_defconfig(args) -> int:
    """Invoke the `save-defconfig` CMake target registered by
    `hosted_kconfig.cmake`.  Auto-configures first if needed.
    The target writes to `<project>/sdkconfig.defaults` by default;
    `args.output` (if any) is ignored — same behaviour as idf.py."""
    _require_target()
    rc = _ensure_configured(_paths().build_dir)
    if rc != 0:
        return rc
    return _cmake_target(_paths().build_dir, "save-defconfig")


def cmd_reconfigure(args) -> int:
    """Idempotent `cmake configure` — pulls in any sdkconfig.defaults
    changes since last configure."""
    _require_target()
    return _cmake_configure(_paths().build_dir)


_PHASE_WIDTH = 70


def _caps():
    """The single sizing authority (tests/infra/caps.py) — quota-aware CPU/mem limits."""
    sys.path.insert(0, str(REPO_ROOT / "tests"))
    from infra import caps
    return caps


def _phase(name: str) -> None:
    """Print a single-line centered phase banner.  Colored when stdout
    is a TTY, plain ASCII otherwise (pipes / log files stay clean)."""
    inner = f" {name} "
    pad_l = (_PHASE_WIDTH - len(inner)) // 2
    pad_r = _PHASE_WIDTH - pad_l - len(inner)
    bar = "─" * pad_l + inner + "─" * pad_r
    if sys.stdout.isatty():
        print(f"\n\033[1;36m{bar}\033[0m")  # bold cyan
    else:
        print(f"\n{bar}")


def _cmake_configure(build_dir: Path) -> int:
    """Configure cmake against the project's own top-level CMakeLists.txt.

    Strict per-project invocation — mirrors `idf.py build` which always
    runs cmake from the project root (cwd).  Source dir is
    `_paths().project_dir`.
    """
    src = _paths().project_dir
    env = os.environ.copy()
    env.setdefault("EH_PATH", str(REPO_ROOT))
    cmd = ["cmake", "-S", str(src), "-B", str(build_dir)]
    _phase("CONFIGURE")
    print(f"eh: {' '.join(cmd)}")
    return subprocess.run(cmd, env=env).returncode


def _cmake_build(build_dir: Path, target: str | None, parallel: int | None) -> int:
    cmd = ["cmake", "--build", str(build_dir)]
    if target:
        cmd += ["--target", target]
    cmd += [f"-j{parallel or _caps().cmake_jobs()}"]  # one quota-aware sizing authority
    _phase("BUILD")
    # Pass EH_PATH through to cmake's nested re-configure
    # invocations (CMAKE_CONFIGURE_DEPENDS triggers them on first
    # build after a fresh configure).  Belt-and-braces: hosted_project.cmake
    # also caches it in CMakeCache.txt + self-derives from its own
    # location, so this is just defence in depth.
    env = os.environ.copy()
    env.setdefault("EH_PATH", str(REPO_ROOT))
    print(f"eh: {' '.join(cmd)}")
    return subprocess.run(cmd, env=env).returncode


def _find_binary(build_dir: Path, target: str) -> Path | None:
    hits = list(build_dir.rglob(target))
    hits = [h for h in hits if h.is_file() and os.access(h, os.X_OK)]
    return hits[0] if hits else None


def _read_exe_target(cml_dir: Path) -> str | None:
    cml = cml_dir / "CMakeLists.txt"
    if not cml.exists():
        return None
    import re
    m = re.search(r"add_executable\(\s*(\w+)", cml.read_text())
    return m.group(1) if m else None


def _is_idf_project_dir(cwd: Path) -> bool:
    """Strict cwd-only check for "is this an IDF project root?"

    The IDF-specific idiom every IDF project has at top of its root
    CMakeLists.txt:

        include($ENV{IDF_PATH}/tools/cmake/project.cmake)

    That line — not the generic `project(...)` call — is what makes
    a cmake project an IDF project.  Matching just `project(...)`
    would false-positive on generic cmake projects (e.g.
    `examples/CMakeLists.txt`, our own Linux-examples umbrella).
    """
    cml = cwd / "CMakeLists.txt"
    if not cml.is_file():
        return False
    try:
        text = cml.read_text(errors="replace")
    except OSError:
        return False
    import re
    # Match the IDF include line with mild flexibility — $ENV{IDF_PATH}
    # or ${IDF_PATH}, any whitespace, dirsep agnostic to /tools/cmake/.
    return bool(re.search(
        r"^\s*include\s*\(\s*\$(?:ENV\{IDF_PATH\}|\{IDF_PATH\})"
        r"[^)]*project\.cmake",
        text, re.MULTILINE))


def _is_host_project_dir(cwd: Path) -> str | None:
    """Strict cwd-only check for "is this a Linux-host project root?".
    Modelled on `idf.py`'s "does this dir have a `project(...)` call?"
    gate.

    Two shapes are accepted (both content-based, no path heuristics):

      IDF-shape (canonical):
          include(.../hosted_kconfig.cmake)
          hosted_kconfig_apply(<exe_name>)
        Sources live under main/ in this layout.  Returns <exe_name>.

      Legacy flat shape (still accepted during rollout):
          add_executable(<name> ...)
          target_link_libraries(<name> PRIVATE ... esp_hosted ...)
        Both lines must appear in the same top-level CMakeLists.txt.
        Returns <name>.

    Returns the executable target name on a positive match, else None.
    Does NOT walk downward — the user must be IN the leaf dir, same
    as `idf.py` requires for IDF projects.
    """
    cml = cwd / "CMakeLists.txt"
    if not cml.is_file():
        return None
    try:
        text = cml.read_text(errors="replace")
    except OSError:
        return None
    import re

    # IDF-shape match (canonical layout — hosted_project.cmake
    # include).  Executable target name comes from the project() call
    # rather than an explicit apply() — the project() name is the
    # executable convention idf.py uses too.
    if re.search(r"\bhosted_project\.cmake\b", text):
        m_proj = re.search(
            r"^\s*project\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)",
            text, re.MULTILINE)
        if m_proj:
            return m_proj.group(1)

    # Transitional shape (hosted_kconfig_apply call).
    m_apply = re.search(
        r"hosted_kconfig_apply\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)",
        text)
    if m_apply:
        return m_apply.group(1)

    # Legacy flat-shape match.
    m_exe = re.search(r"add_executable\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)",
                       text)
    if not m_exe:
        return None
    name = m_exe.group(1)
    # Match `target_link_libraries(<name> ... PRIVATE ... esp_hosted ...)`
    # — `PRIVATE` may sit anywhere inside the call; `esp_hosted` must
    # appear as a whole-word link target.
    tll = re.search(
        r"target_link_libraries\s*\(\s*" + re.escape(name) + r"\b[^)]*?\besp_hosted\b",
        text, re.DOTALL)
    if not tll:
        return None
    return name


def _detect_example_dir(cwd: Path) -> str | None:
    """Back-compat shim — returns just the target name."""
    return _is_host_project_dir(cwd)


def _classify_cwd(cwd: Path, repo: Path,
                  is_host_leaf: bool) -> tuple[str, str]:
    """Tell the caller what kind of directory we're in.

    Returns a (kind, hint) tuple. `kind` is "host" (hosted_project.cmake
    signature — handle locally), "idf" (IDF signature — dispatch to
    idf.py), or "reject" (hint = error to print)."""
    if is_host_leaf:
        return ("host", "")

    # IDF signature → dispatch.  Use the path relative to the repo
    # for the verbose notice when cwd sits under the repo; otherwise
    # fall back to the absolute path.
    if _is_idf_project_dir(cwd):
        try:
            rel = cwd.resolve().relative_to(repo.resolve())
            display = str(rel) if rel.parts else "."
        except ValueError:
            display = str(cwd.resolve())
        return ("idf", display)

    # Neither — refuse with the precise signatures we're looking for.
    return ("reject",
            f"cwd is not a recognized project directory.  eh.py "
            f"expects one of:\n"
            f"  - HOST C app : CMakeLists.txt with "
            f"`include($ENV{{EH_PATH}}/tools/cmake/hosted_project.cmake)`\n"
            f"                 (IDF-shape; source `. ./export.sh` first "
            f"to set EH_PATH)\n"
            f"  - HOST py app: main.py importing `eh_host`\n"
            f"  - IDF project: CMakeLists.txt with "
            f"`include($ENV{{IDF_PATH}}/tools/cmake/project.cmake)` "
            f"(dispatched to idf.py)\n"
            f"`cd` into such a directory and re-run.")


def _dispatch_to_idf_py(rel_for_msg: str) -> int:
    """Exec `idf.py` with this process's argv[1:] (replaces process).

    Returns non-zero only when idf.py couldn't be located or execvp
    itself failed; on a successful exec control never comes back. """
    idf_py = shutil.which("idf.py")
    if not idf_py:
        sys.stderr.write(
            f"eh: `{rel_for_msg}` is an ESP-IDF project, but "
            f"`idf.py` is not on PATH.  Source ESP-IDF's export.sh "
            f"(or `. ./export.sh` from the esp_hosted repo root) "
            f"and re-run.\n")
        return 2
    # Single-line verbose notice — makes the dispatch explicit so the
    sys.stderr.write(
        f"eh: `{rel_for_msg}` is an ESP-IDF project — dispatching "
        f"to idf.py ({idf_py})\n")
    os.execvp(idf_py, [idf_py, *sys.argv[1:]])
    # execvp doesn't return on success.
    return 2


def _stage_binaries(build_dir: Path, bin_dir: Path,
                    target: str | None) -> int:
    """Copy freshly-built executables out of build/ into bin/.

    When `target` is given, copy that one binary.  When it isn't, walk
    every executable that was linked under build/ and copy each into
    bin/.  Existing bin/<name> is overwritten (so the staged copy
    always tracks the latest build).
    """
    if target:
        src = _find_binary(build_dir, target)
        candidates = [src] if src else []
    else:
        candidates = [p for p in build_dir.rglob("*")
                      if p.is_file()
                      and os.access(p, os.X_OK)
                      # filter cmake's own helpers / shared libs
                      and not p.name.endswith((".so", ".cmake", ".sh", ".py",
                                                ".cmake.in"))
                      and "CMakeFiles" not in p.parts]
    if not candidates:
        return 0
    _phase("STAGE")
    bin_dir.mkdir(parents=True, exist_ok=True)
    for src in candidates:
        dst = bin_dir / src.name
        shutil.copy2(src, dst)
        print(f"eh: staged {dst}")
    return 0


def cmd_build(args) -> int:
    _require_target()                       # persist default if unset
    build_dir = _paths().build_dir
    rc = _cmake_configure(build_dir)
    if rc != 0:
        return rc
    rc = _cmake_build(build_dir, args.target, args.jobs)
    if rc != 0:
        return rc
    return _stage_binaries(build_dir, _paths().bin_dir, args.target)


_SUDO_EXPLAIN = (
    "\n"
    "┌──────────────────────────────────────────────────────────────────┐\n"
    "│  ROOT ACCESS REQUIRED                                            │\n"
    "│                                                                  │\n"
    "│  This example talks to the esp_hosted kmod via /dev/esps0.       │\n"
    "│  Only root can open that device, so eh runs the binary       │\n"
    "│  under `sudo`. Enter your password when prompted.                │\n"
    "└──────────────────────────────────────────────────────────────────┘"
)


def _sudo_creds_cached() -> bool:
    """True if `sudo` can run without prompting (creds cached or
    NOPASSWD)."""
    try:
        return subprocess.run(["sudo", "-n", "true"],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode == 0
    except OSError:
        return False


def _announce_sudo() -> None:
    """Print the ROOT box only when sudo will actually prompt."""
    if not _sudo_creds_cached():
        print(_SUDO_EXPLAIN)


def cmd_run(args) -> int:
    """Build then exec the project's run artefact under `sudo`.
    Same shape for every hosted_project: cmake build → sudo exec.
    What gets exec'd differs by leaf source — a binary for C apps,
    `python3 main/main.py` for py apps."""
    project_dir = _paths().project_dir
    py_main = project_dir / "main" / "main.py"
    is_py = py_main.is_file()

    if not is_py and not args.target:
        sys.stderr.write("eh: run/monitor requires a target name "
                         "(or run from inside a host_c_app or host_py_app dir)\n")
        return 2

    rc = cmd_build(args)
    if rc != 0:
        return rc

    if is_py:
        return _exec_py_main(py_main, args)

    staged = _paths().bin_dir / args.target
    bin_path = staged if staged.is_file() and os.access(staged, os.X_OK) \
                      else _find_binary(_paths().build_dir, args.target)
    if not bin_path:
        sys.stderr.write(f"eh: binary not found for target {args.target}\n")
        return 2
    _phase("RUN")
    _announce_sudo()
    cmd = ["sudo", str(bin_path), *args.args]
    print(f"eh: exec {' '.join(cmd)}")
    return subprocess.run(cmd).returncode


def _exec_py_main(main_py: Path, args) -> int:
    """Exec `sudo env … python3 main_py` so the script can open
    /dev/esps0.  EH_HOST_LIB + PYTHONPATH are passed explicitly via
    `env` because sudo otherwise strips the caller's environment."""
    so_path = _paths().build_dir / "eh_libeh_host" / "libeh_host.so"
    if not so_path.is_file():
        sys.stderr.write(
            f"eh: libeh_host.so not produced at {so_path}\n")
        return 2

    eh_path = os.environ.get("EH_PATH")
    if not eh_path:
        sys.stderr.write(
            "eh: EH_PATH not set; source ./export.sh first\n")
        return 2
    pkg_dir = Path(eh_path) / "host/linux/eh_host_linux_python_ctypes"

    existing_pp = os.environ.get("PYTHONPATH", "")
    pythonpath = (f"{pkg_dir}:{existing_pp}" if existing_pp else str(pkg_dir))

    _phase("RUN")
    _announce_sudo()
    cmd = ["sudo", "env",
           f"EH_HOST_LIB={so_path}",
           f"PYTHONPATH={pythonpath}",
           sys.executable, str(main_py),
           *(args.args or [])]
    print(f"eh: exec {' '.join(cmd)}")
    return subprocess.run(cmd).returncode


def cmd_clean(args) -> int:
    """Remove every generated dir (build/, bin/) — matches the
    "rm -rf build" intent of `idf.py fullclean` but leaves sdkconfig
    + .eh.target alone so a `build` follow-up is incremental."""
    _phase("CLEAN")
    p = _paths()
    removed_any = False
    for d in (p.build_dir, p.bin_dir):
        if d.exists():
            print(f"eh: removing {d}")
            shutil.rmtree(d)
            removed_any = True
    if not removed_any:
        print("eh: nothing to clean")
    return 0


def cmd_fullclean(args) -> int:
    """Like clean, plus wipes generated artefacts + persistent NVS.
    Preserves sdkconfig + sdkconfig.defconfig + .eh.target (IDF
    parity: user's saved Kconfig choices survive fullclean)."""
    _phase("FULLCLEAN")
    p = _paths()
    for d in (p.build_dir, p.bin_dir, p.project_dir / ".nvs"):
        if not d.exists():
            continue
        print(f"eh: removing {d}")
        try:
            shutil.rmtree(d)
        except PermissionError as e:
            print(f"eh: {e}; retrying via sudo rm -rf")
            rc = subprocess.run(["sudo", "rm", "-rf", str(d)]).returncode
            if rc != 0:
                sys.stderr.write(
                    f"eh: sudo rm -rf {d} failed (rc={rc})\n")
                return rc
    return 0


def cmd_show(args) -> int:
    """Diagnostic — print current state."""
    target = _read_target()
    p = _paths()
    print(f"target:           {target or '(unset)'}")
    print(f"project_dir:      {p.project_dir}")
    print(f"build_dir:        {p.build_dir}")
    print(f"bin_dir:          {p.bin_dir}{'' if p.bin_dir.exists() else ' (missing — populated after build)'}")
    print(f"sdkconfig:        {p.sdkconfig}{'' if p.sdkconfig.exists() else ' (missing)'}")
    sdk_h = p.build_cfg_dir / "sdkconfig.h"
    print(f"sdkconfig.h:      {sdk_h}{'' if sdk_h.exists() else ' (missing — run reconfigure)'}")
    sdk_cm = p.build_cfg_dir / "sdkconfig.cmake"
    print(f"sdkconfig.cmake:  {sdk_cm}{'' if sdk_cm.exists() else ' (missing — run reconfigure)'}")
    return 0


# ── dependency management (.deps/: esp-idf + esp-emu) ────────────────
# install.sh / install.fish drive `eh.py install`; builds + the esplab
# runner resolve their IDF/esp-emu through the same order below, so a
# .deps checkout (or a set-idf-path / set-esp-emu override) is picked up
# automatically without any env fiddling.
DEPS_DIR  = REPO_ROOT / ".deps"
CONF_FILE = DEPS_DIR / "eh.conf"

IDF_REPO        = "https://github.com/espressif/esp-idf.git"
IDF_DEFAULT_REF = "v6.1"     # carries the SDIO send-cap fix (tools/idf_patches/
                             # is a no-op here) and SPI slave-HD 3-wire mode.
IDF_PATCH_DIR   = REPO_ROOT / "tools" / "idf_patches"
IDF_TARGETS     = "all"   # install toolchains for all supported chips
# esp-emu's crate deps set a Rust floor (e.g. smoltcp 0.13 needs 1.91). Preflight
# warns when rustc is older; `rustup update` fixes it. Bump when deps need newer.
MIN_RUST        = (1, 91)

EMU_REPO        = "ssh://git@gitlab.espressif.cn:27227/security/esp-emulator.git"
# esp-emu's host-power-save wake fixes aren't on main yet; default to the branch
# that carries them. Flip to a tag/main once merged, or pass --emu-ref.
EMU_DEFAULT_REF = "feat/hosted-emu-test-coverage"


def _color_enabled() -> bool:
    return sys.stdout.isatty() and os.environ.get("NO_COLOR") is None


def _c(code: str) -> str:
    return code if _color_enabled() else ""


_RESET  = _c("\033[0m");  _BOLD = _c("\033[1m");  _DIM = _c("\033[2m")
_CYAN   = _c("\033[36m"); _GREEN = _c("\033[32m"); _BLUE = _c("\033[94m")
_YELLOW = _c("\033[33m"); _RED   = _c("\033[31m")


def _box(title: str, lines: list[str], color: str = _CYAN) -> None:
    """Draw a titled box; content lines are plain text (no embedded ANSI)."""
    width = max([len(title)] + [len(l) for l in lines] + [1])
    bar = "─" * (width + 2)
    print(f"{color}┌{bar}┐{_RESET}")
    print(f"{color}│{_RESET} {_BOLD}{title.ljust(width)}{_RESET} {color}│{_RESET}")
    print(f"{color}├{bar}┤{_RESET}")
    for l in lines:
        print(f"{color}│{_RESET} {l.ljust(width)} {color}│{_RESET}")
    print(f"{color}└{bar}┘{_RESET}")


# Grouped top-level help — one light-blue box per intention, one command per row.
_HELP_GROUPS = [
    ("Setup & dependencies", [
        ("install [--enable-test] [--host]", "fetch + build deps into .deps/ (ESP-IDF; --enable-test adds esp-emu)"),
        ("install --set-idf-path DIR",       "install but use your existing ESP-IDF (no clone)"),
        ("install --set-emu-path DIR",       "...use your existing esp-emu (implies --enable-test)"),
        ("install --uninstall",              "remove the managed .deps/"),
        ("set-idf-path DIR",                 "use an existing ESP-IDF          (eh.conf: idf_path)"),
        ("get-idf-path",                     "show the ESP-IDF builds resolve to"),
        ("set-emu-path DIR",                 "use an existing esp-emu          (eh.conf: emu_path)"),
        ("get-emu-path",                     "show the esp-emu tests resolve to"),
        ("patch-idf",                        "lift the SDIO send-cap in the resolved IDF"),
    ]),
    ("Configure", [
        ("set-target TARGET", "persist a port-type choice (one-time)"),
        ("defconfig",         "apply sdkconfig.defaults"),
        ("menuconfig",        "interactive Kconfig editor"),
        ("save-defconfig",    "write a minimal sdkconfig.defaults"),
        ("reconfigure",       "re-run the cmake configure step"),
        ("show",              "print current state (diagnostic)"),
    ]),
    ("Build & flash", [
        ("build [TARGET]", "cmake configure + build"),
        ("run",            "build, then run / flash the app"),
        ("clean",          "drop build artifacts"),
        ("fullclean",      "remove the whole build dir"),
    ]),
    ("Test", [
        ("test SUBSTRATE", "run the test suite (emu / hw / linux)"),
        ("manual-test",    "guided manual bring-up"),
        ("hw",             "hardware-bench runner"),
    ]),
]


def _group_box(title: str, rows: list) -> None:
    """One grouped help box: light-blue border, title in the top rule, padded."""
    cmdw = max(len(c) for c, _ in rows)
    lines = [f"  {c.ljust(cmdw)}   {d}" for c, d in rows]
    width = max([len(title) + 4] + [len(l) for l in lines])
    bar = "─" * (width + 2)
    dashes = "─" * (width + 2 - 4 - len(title))     # "── " + title + " "
    print(f"{_BLUE}┌── {_BOLD}{title}{_RESET}{_BLUE} {dashes}┐{_RESET}")
    print(f"{_BLUE}│{_RESET} {' ' * width} {_BLUE}│{_RESET}")
    for l in lines:
        print(f"{_BLUE}│{_RESET} {l.ljust(width)} {_BLUE}│{_RESET}")
    print(f"{_BLUE}│{_RESET} {' ' * width} {_BLUE}│{_RESET}")
    print(f"{_BLUE}└{bar}┘{_RESET}")


def _grouped_help() -> None:
    print("eh — orchestrator for esp_hosted (idf.py-subset CLI)\n")
    print("usage: eh <command> [options]     (eh <command> -h  for command detail)\n")
    for title, rows in _HELP_GROUPS:
        _group_box(title, rows)


def _confirm(prompt: str, assume_yes: bool = False) -> bool:
    if assume_yes:
        return True
    try:
        ans = input(f"{_BOLD}{prompt}{_RESET} [y/N] ").strip().lower()
    except EOFError:
        return False
    return ans in ("y", "yes")


def _read_conf() -> dict:
    conf: dict[str, str] = {}
    if CONF_FILE.is_file():
        for line in CONF_FILE.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                conf[k.strip()] = v.strip()
    return conf


def _write_conf(conf: dict) -> None:
    DEPS_DIR.mkdir(parents=True, exist_ok=True)
    body = ["# eh.py dependency overrides (set-idf-path / set-esp-emu)"]
    body += [f"{k}={conf[k]}" for k in sorted(conf)]
    CONF_FILE.write_text("\n".join(body) + "\n")


def _is_idf(p: Path | None) -> bool:
    return p is not None and (p / "export.sh").is_file()


def resolve_idf() -> Path | None:
    """override (eh.conf) → .deps/esp-idf → $IDF_PATH. No assumed home-dir
    locations: install defaults to .deps unless the user points elsewhere
    (set-idf-path) or has an IDF activated ($IDF_PATH)."""
    conf = _read_conf()
    cands: list[Path] = []
    if conf.get("idf_path"):
        cands.append(Path(conf["idf_path"]).expanduser())
    cands.append(DEPS_DIR / "esp-idf")
    if os.environ.get("IDF_PATH"):
        cands.append(Path(os.environ["IDF_PATH"]))
    for c in cands:
        if _is_idf(c):
            return c.resolve()
    return None


def resolve_emu_dir() -> Path | None:
    """override (eh.conf) → .deps/esp-emu. No assumed paths: set-esp-emu to
    point at an external checkout."""
    conf = _read_conf()
    cands: list[Path] = []
    if conf.get("emu_path") or conf.get("esp_emu"):
        cands.append(Path(conf.get("emu_path") or conf["esp_emu"]).expanduser())
    cands.append(DEPS_DIR / "esp-emu")
    for c in cands:
        if c.is_dir():
            return c.resolve()
    return None


def _managed_idf() -> Path | None:
    """IDF that install manages: an explicit override (set-idf-path -> eh.conf)
    or our own .deps/esp-idf clone. Excludes $IDF_PATH (honored for BUILDS via
    resolve_idf, but install defaults to .deps unless the user points at an IDF).
    Patches are only ever applied to .deps; an external override is reported, not
    modified (run `eh.py patch-idf` to apply there)."""
    conf = _read_conf()
    if conf.get("idf_path"):
        p = Path(conf["idf_path"]).expanduser()
        if _is_idf(p):
            return p.resolve()
    d = DEPS_DIR / "esp-idf"
    return d.resolve() if _is_idf(d) else None


def _managed_emu() -> Path | None:
    """esp-emu install OWNS: explicit override (set-esp-emu) or .deps/esp-emu."""
    conf = _read_conf()
    if conf.get("emu_path") or conf.get("esp_emu"):
        p = Path(conf.get("emu_path") or conf["esp_emu"]).expanduser()
        if p.is_dir():
            return p.resolve()
    d = DEPS_DIR / "esp-emu"
    return d.resolve() if d.is_dir() else None


def emu_binary(d: Path) -> Path:
    return d / "target" / "release" / "esp-emu"


def emu_stale(d: Path) -> bool:
    """True when esp-emu needs a (re)build: binary missing, or any source newer
    than it. Existence alone is not readiness - a checkout that has been pulled
    or hand-edited since the last build silently runs old code, and the failure
    then looks like a test bug rather than a stale binary (an emu missing a
    --thread-sim fragment the tests pass, say)."""
    b = emu_binary(d)
    if not b.is_file():
        return True
    bt = b.stat().st_mtime
    srcs = [d / "Cargo.toml", d / "Cargo.lock"]
    srcs += (d / "src").rglob("*.rs") if (d / "src").is_dir() else []
    return any(p.is_file() and p.stat().st_mtime > bt for p in srcs)


def emu_revision(d: Path) -> str:
    """`<short-sha> <branch>[ +dirty]` for reporting, or '' outside a git tree.
    Printed wherever we report the emu so a version mismatch is attributable at
    a glance instead of after an afternoon of debugging."""
    def _q(*a):
        try:
            r = subprocess.run(["git", *a], cwd=str(d), capture_output=True,
                               text=True, timeout=5)
            return r.stdout.strip() if r.returncode == 0 else ""
        except Exception:
            return ""
    sha = _q("rev-parse", "--short", "HEAD")
    if not sha:
        return ""
    br = _q("rev-parse", "--abbrev-ref", "HEAD")
    dirty = " +dirty" if _q("status", "--porcelain") else ""
    return f"{sha} {br}{dirty}".strip()


def emu_behind(d: Path) -> int:
    """Commits the emu checkout is behind its upstream, 0 if unknown/current.
    Uses only local refs - never fetches, so `install` stays offline-safe."""
    try:
        r = subprocess.run(["git", "rev-list", "--count", "HEAD..@{u}"],
                           cwd=str(d), capture_output=True, text=True, timeout=5)
        return int(r.stdout.strip()) if r.returncode == 0 else 0
    except Exception:
        return 0


def cmd_get_idf_path(args) -> int:
    p = resolve_idf()
    if getattr(args, "raw", False):
        if p:
            print(str(p)); return 0
        return 1
    if p:
        print(f"{_GREEN}ESP-IDF:{_RESET} {p}")
        return 0
    sys.stderr.write(f"{_YELLOW}ESP-IDF not found.{_RESET} "
                     f"Run ./install.sh or `eh.py set-idf-path <dir>`.\n")
    return 1


def cmd_set_idf_path(args) -> int:
    p = Path(args.path).expanduser().resolve()
    if not _is_idf(p):
        sys.stderr.write(f"{_RED}eh:{_RESET} no export.sh under {p} "
                         f"— not an ESP-IDF checkout\n")
        return 2
    conf = _read_conf(); conf["idf_path"] = str(p); _write_conf(conf)
    print(f"{_GREEN}idf-path set:{_RESET} {p} {_DIM}(overrides .deps){_RESET}")
    _report_idf_patch_status(p)
    return 0


def cmd_get_esp_emu(args) -> int:
    d = resolve_emu_dir()
    b = emu_binary(d) if d else None
    if getattr(args, "raw", False):
        if b and b.is_file():
            print(str(b)); return 0
        return 1
    if d:
        if not (b and b.is_file()):
            st = f"{_YELLOW}not built (cargo build --release){_RESET}"
        elif emu_stale(d):
            st = f"{_YELLOW}STALE - sources newer than binary{_RESET}"
        else:
            st = f"{_GREEN}built{_RESET}"
        rev = emu_revision(d)
        print(f"{_GREEN}esp-emu:{_RESET} {d}  [{st}]"
              + (f"  rev {rev}" if rev else ""))
        return 0
    sys.stderr.write(f"{_YELLOW}esp-emu not found.{_RESET} "
                     f"Run ./install.sh --with-emu or `eh.py set-esp-emu <dir>`.\n")
    return 1


def cmd_set_esp_emu(args) -> int:
    p = Path(args.path).expanduser().resolve()
    # accept either the repo dir or the built binary (target/release/esp-emu).
    d = p.parent.parent.parent if (p.name == "esp-emu" and p.is_file()) else p
    if not d.is_dir():
        sys.stderr.write(f"{_RED}eh:{_RESET} not a directory: {d}\n")
        return 2
    conf = _read_conf(); conf["emu_path"] = str(d); conf.pop("esp_emu", None); _write_conf(conf)
    note = "" if emu_binary(d).is_file() else \
           f"  {_YELLOW}(binary not built yet){_RESET}"
    print(f"{_GREEN}esp-emu set:{_RESET} {d}{note}")
    return 0


TESTVENV  = DEPS_DIR / "testvenv"
TEST_PKGS = ["pytest", "pytest-xdist", "pytest-rerunfailures", "pytest-timeout",
             "pyserial", "bumble"]
# (flaky-HW retry via `eh.py test hw --reruns N`; xdist for `--jobs N`;
#  pytest-timeout for the overall per-test cap; pyserial for the HW SerialDut)


def _ensure_testvenv() -> Path:
    """Managed venv for the test engine (.deps/testvenv). Created on first use;
    a stamp file re-installs only when TEST_PKGS changes (so a new dep like
    pytest-timeout lands in an existing venv without a slow reinstall each run)."""
    py = TESTVENV / "bin" / "python"
    stamp = TESTVENV / ".pkgs"
    want = "\n".join(sorted(TEST_PKGS))
    if py.is_file():
        if stamp.is_file() and stamp.read_text() == want:
            return py
        # TEST_PKGS changed → top up the existing venv.
        if subprocess.run([str(py), "-m", "pip", "install", "-q", *TEST_PKGS]).returncode == 0:
            stamp.write_text(want)
        return py
    _box("Test environment", [
        "Creating a Python venv for the test engine:",
        f"  {TESTVENV}",
        f"Installing: {', '.join(TEST_PKGS)}",
    ], _CYAN)
    TESTVENV.parent.mkdir(parents=True, exist_ok=True)
    if subprocess.run([sys.executable, "-m", "venv", str(TESTVENV)]).returncode:
        sys.stderr.write(f"{_RED}eh:{_RESET} venv creation failed\n"); sys.exit(1)
    if subprocess.run([str(py), "-m", "pip", "install", "-q",
                       "--upgrade", "pip", *TEST_PKGS]).returncode:
        sys.stderr.write(f"{_RED}eh:{_RESET} pip install failed "
                         f"(offline?). Set up {TESTVENV} manually.\n"); sys.exit(1)
    stamp.write_text(want)
    return py


def _per_test_timeout(rest) -> float:
    """The per-test hard cap in effect (pytest-timeout): --timeout=N / --timeout N
    override the pytest.ini default (900s). Used to SIZE the stall watchdog so the
    two stay proportional — lower --timeout, tighter wedge detection."""
    for i, t in enumerate(rest):
        v = None
        if t.startswith("--timeout="):
            v = t.split("=", 1)[1]
        elif t == "--timeout" and i + 1 < len(rest):
            v = rest[i + 1]
        if v is not None:
            try:
                return float(v.rstrip("s"))
            except ValueError:
                pass
    return 900.0


# Signal-safe reap: a SIGTERM to eh.py (CI-cancel) terminates by default and a Ctrl-C
# SIGINT reaches only eh.py (the run group is its own session, see start_new_session
# below) — either path would skip _supervise's killpg+reap and orphan the whole tree.
# We register every LIVE run so one installed handler can sweep them on any signal. No
# between-phase set is needed: each _supervise call fully reaps in its own finally, so
# a signal in the gap between phases finds nothing to clean.
_ACTIVE_SUPERVISED = []   # [(proc, run_dir)] currently-running supervised procs
_SIG_HANDLERS_INSTALLED = False
_SIG_CLEANUP_RUNNING = False


def _cleanup_supervised(proc, run_dir):
    """killpg the run group then reap its on-disk spawn registry. Never raises —
    cleanup must never crash the verdict or the signal path. pytest-timeout's os._exit
    and our own killpg both skip fixture teardown, so a worker's emus / Bumble / build
    children can outlive the run; killpg sweeps in-group stragglers (the tree is its own
    session — killpg targets proc.pid as the group leader) and reap_run_orphans kills
    any pid a dead worker recorded but never cleared."""
    if proc is not None:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError, OSError):
            pass
    try:
        sys.path.insert(0, str(REPO_ROOT / "tests"))
        from infra import lab as _lab
        _lab.reap_run_orphans(run_dir)
    except Exception:  # noqa: BLE001  cleanup must never crash the verdict
        pass


def _install_signal_cleanup():
    """Install once: on SIGTERM/SIGINT/SIGHUP to eh.py, killpg every active run group and
    reap every seen run's registry, then re-raise with the default handler so eh.py dies
    with the right status. SIGHUP matters because a tmux/SSH disconnect mid-run delivers
    SIGHUP (not SIGTERM/SIGINT) — without it, a dropped session orphans the whole group.
    No-op if ever reached off the main thread (signal.signal raises)."""
    global _SIG_HANDLERS_INSTALLED
    if _SIG_HANDLERS_INSTALLED:
        return

    def _handler(signum, _frame):
        global _SIG_CLEANUP_RUNNING
        if _SIG_CLEANUP_RUNNING:   # re-entrancy guard: a 2nd signal mid-sweep is a no-op
            return
        _SIG_CLEANUP_RUNNING = True
        active = list(_ACTIVE_SUPERVISED)
        for proc, _rd in active:   # killpg every group FIRST (fast destructive step, so a
            try:                   # short SIGTERM->SIGKILL grace window can't beat it)
                os.killpg(proc.pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError, OSError):
                pass
        for _proc, rd in active:   # THEN sweep each registry (the slower file I/O)
            _cleanup_supervised(None, rd)
        signal.signal(signum, signal.SIG_DFL)
        os.kill(os.getpid(), signum)

    for _s in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        try:
            signal.signal(_s, _handler)
        except (ValueError, OSError):   # not the main thread — skip, finally still covers
            return
    _SIG_HANDLERS_INSTALLED = True


def _pid_alive(pid: int) -> bool:
    """True if pid exists and is not a zombie (kill(pid, 0) succeeds on a zombie)."""
    try:
        with open(f"/proc/{pid}/stat") as fh:
            return fh.read().rsplit(")", 1)[1].split()[0] != "Z"
    except (OSError, IndexError):
        try:
            os.kill(pid, 0)
            return True
        except OSError:
            return False


def _supervise(cmd, cwd, env, run_dir, per_test_timeout, label="run") -> int:
    """Run pytest in its OWN process group and enforce the per-test timeout from
    OUTSIDE the workers. tests/conftest.py stamps each worker's state to
    run_dir/workers/<wid>; a worker stuck 'running' one nodeid past the stall budget
    is a live-but-wedged worker that xdist's controller would otherwise wait on
    forever (the in-worker pytest-timeout dies WITH the worker). We SIGTERM→SIGKILL
    the whole group (start_new_session makes the tree its own session; killpg targets
    proc.pid the group leader — emus + Bumble go with it), so the completeness gate
    + recovery retry run instead of hanging. Two knobs, both top→bottom overridable:
      EH_STALL_TIMEOUT   per-worker no-progress budget (default per-test cap + 300s)
      EH_SESSION_TIMEOUT overall wall cap (default 0 = off; last-resort backstop)
    Returns pytest's real exit code, or 1 on a watchdog kill — NOT a structural code
    (2/3/…), so _finalize_exit's merge renders RED + lists the did-not-run tests."""
    stall = float(env.get("EH_STALL_TIMEOUT") or (per_test_timeout + 300))
    sess_cap = float(env.get("EH_SESSION_TIMEOUT") or 0)
    wdir = Path(run_dir) / "workers"
    t_start = time.time()
    _install_signal_cleanup()
    env = dict(env, EH_SUPERVISED="1")
    proc = subprocess.Popen(cmd, cwd=cwd, env=env, start_new_session=True)
    _ACTIVE_SUPERVISED.append((proc, run_dir))
    killed = {"why": None}
    rc = None

    dead = set()

    def _stuck():
        out = []
        try:
            entries = list(wdir.iterdir())
        except OSError:
            return out
        for f in entries:
            try:
                # Ignore corpse heartbeats from an earlier run sharing this dir
                # (else a stale 'running' file fires the watchdog instantly).
                if f.stat().st_mtime < t_start:
                    continue
                fields = f.read_text().strip().split("\t")
                state, nodeid, ts = fields[0], fields[1], float(fields[2])
                pid = int(fields[3]) if len(fields) > 3 else None
            except (OSError, ValueError, IndexError):
                continue
            if state != "running":
                continue
            # A worker that exited (pytest-timeout, crash) can never clear its file;
            # xdist already reported it 'node down'. Not a wedge — the rest run on.
            if pid is not None and not _pid_alive(pid):
                if f.name not in dead:
                    dead.add(f.name)
                    sys.stderr.write(f"{_YELLOW}eh: {label} — worker {f.name} exited "
                                     f"during {nodeid.strip()}; not a wedge, run "
                                     f"continues on the others{_RESET}\n")
                continue
            out.append((f.name, nodeid.strip(), time.time() - ts))
        return out

    def _kill(why):
        killed["why"] = why
        sys.stderr.write(f"\n{_RED}eh: {label} — {why}; killing the run group{_RESET}\n")
        try:
            os.killpg(proc.pid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            return
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass

    # One supervisor: liveness AND resources on the same loop. Split loops meant the
    # part that could act (kill) was blind to memory, and the part that saw memory
    # could only record — which is how an oomd kill happened with nobody intervening.
    # 2s tick: /proc reads cost 0.03ms. Full ps attribution row every 15th tick (19ms).
    tick = float(env.get("EH_SUPERVISE_TICK") or 2)
    strikes_max = int(env.get("EH_STALL_STRIKES") or 3)
    strikes = {}

    def _watch():
        t0 = time.time()
        i = 0
        try:
            sys.path.insert(0, str(REPO_ROOT / "tests"))
            from infra import lab as _lab
        except Exception:  # noqa: BLE001
            _lab = None
        while proc.poll() is None:
            time.sleep(tick)
            i += 1
            if _lab is not None:
                try:
                    _lab.sample_fast()                       # 2s control signal
                    if i % 15 == 0:
                        _lab.sample_resources(run_dir)       # 30s attribution row
                except Exception:  # noqa: BLE001 — instrumentation never kills a run
                    pass
            if sess_cap and time.time() - t0 > sess_cap:
                _kill(f"session cap {sess_cap:.0f}s exceeded")
                return
            seen = set()
            for wid, nodeid, age in _stuck():
                seen.add(wid)
                if age <= stall:
                    strikes.pop(wid, None)
                    continue
                # Strikes, not a hair trigger: one over-budget poll under heavy load
                # is not the same as a deadlock.
                strikes[wid] = strikes.get(wid, 0) + 1
                if strikes[wid] < strikes_max:
                    sys.stderr.write(
                        f"{_DIM}eh: {label} — worker {wid} over stall budget "
                        f"{age:.0f}s/{stall:.0f}s on {nodeid} "
                        f"(strike {strikes[wid]}/{strikes_max}){_RESET}\n")
                    continue
                _kill(f"worker {wid} wedged {age:.0f}s on {nodeid} "
                      f"(stall budget {stall:.0f}s, {strikes_max} strikes)")
                return
            for wid in list(strikes):
                if wid not in seen:
                    strikes.pop(wid, None)


    def _flush_samples():
        """Final row on the MAIN thread: _watch is a daemon and is killed at
        interpreter exit, so a run shorter than the row cadence would record nothing."""
        try:
            sys.path.insert(0, str(REPO_ROOT / "tests"))
            from infra import lab as _lab
            _lab.sample_resources(run_dir)
        except Exception:  # noqa: BLE001
            pass

    th = threading.Thread(target=_watch, daemon=True)
    th.start()
    # try/finally: the killpg+reap must run on EVERY exit path (normal, watchdog kill,
    # exception, or a signal that our handler doesn't fully own) — not only inline after
    # a clean wait(). A run that "finished" must never leave background children.
    try:
        proc.wait()
        rc = proc.returncode
    finally:
        _flush_samples()
        _cleanup_supervised(proc, run_dir)
        try:
            _ACTIVE_SUPERVISED.remove((proc, run_dir))
        except ValueError:
            pass
    # A watchdog kill, or a signal death (Ctrl-C = -2, our SIGKILL = -9): report 1
    # so _finalize_exit's merge renders the verdict — a negative rc is "not in (0,1)",
    # which it would otherwise preserve as a bogus 254-ish shell code.
    return 1 if (killed["why"] or rc is None or rc < 0) else rc


def cmd_test(args) -> int:
    """eh.py test [emu-mcu|emu-linux|hw] [paths/node-ids] [--jobs N] [--sandbox MODE] [pytest flags]
    Default substrate emu; default sequential (safe/readable); default target the
    substrate's suite dir. --jobs N runs N benches in parallel (pytest-xdist);
    each worker gets its own socket + flash copy, so failures stay isolated.
    --sandbox none|auto|bwrap (bare == auto) isolates build+emu; native by default,
    falls back to native when the tool can't run."""
    rest = list(args.args or [])
    # argparse.REMAINDER only starts collecting at a non-flag token, so a flag-first
    # invocation needs `eh.py test -- --regression …`. Drop that separator here or it
    # reaches pytest as a path and the run collects nothing (exit 5).
    if rest and rest[0] == "--":
        rest.pop(0)
    substrate = "emu"
    # One shared corpus (tests/suites) runs on any substrate; the rest are
    # substrate-specific suites layered on top.
    #   emu = suites + emu-mcu (power-save) + emu-linux ; hw = suites (serial HW)
    _subs = ("emu", "emu-mcu", "emu-linux", "hw", "rpi", "linux", "suites")
    if rest and rest[0] in _subs:
        substrate = rest.pop(0)
    # --jobs N | --jobs=N | --jobs auto  ->  xdist -n  (absent => sequential)
    # --bench NAME -> lease that specific HW bench (default: the lab.local.json one)
    # --sandbox [none|auto|bwrap] -> isolation provider (bare == auto); the front door
    #   for EH_SANDBOX, which stays the plumbing xdist workers read. Flag wins over env.
    jobs, bench_name, sandbox_mode, resctl_mode, out, i = None, None, None, None, [], 0
    while i < len(rest):
        t = rest[i]
        if t == "--jobs":
            jobs = rest[i + 1] if i + 1 < len(rest) else "auto"; i += 2; continue
        if t.startswith("--jobs="):
            jobs = t.split("=", 1)[1]; i += 1; continue
        if t == "--bench":
            bench_name = rest[i + 1] if i + 1 < len(rest) else None; i += 2; continue
        if t.startswith("--bench="):
            bench_name = t.split("=", 1)[1]; i += 1; continue
        if t == "--sandbox":
            nxt = rest[i + 1] if i + 1 < len(rest) else None
            if nxt in ("none", "auto", "bwrap"):
                sandbox_mode = nxt; i += 2
            else:
                sandbox_mode = "auto"; i += 1   # bare --sandbox == auto
            continue
        if t.startswith("--sandbox="):
            sandbox_mode = t.split("=", 1)[1]; i += 1; continue
        if t == "--resctl":
            nxt = rest[i + 1] if i + 1 < len(rest) else None
            if nxt in ("none", "auto", "systemd", "cgroup", "nice"):
                resctl_mode = nxt; i += 2
            else:
                resctl_mode = "auto"; i += 1   # bare --resctl == auto
            continue
        if t.startswith("--resctl="):
            resctl_mode = t.split("=", 1)[1]; i += 1; continue
        out.append(t); i += 1
    rest = out
    if sandbox_mode not in (None, "none", "auto", "bwrap"):
        sys.stderr.write(f"{_RED}eh:{_RESET} --sandbox must be none|auto|bwrap "
                         f"(got {sandbox_mode!r})\n"); return 2
    if resctl_mode not in (None, "none", "auto", "systemd", "cgroup", "nice"):
        sys.stderr.write(f"{_RED}eh:{_RESET} --resctl must be none|auto|systemd|cgroup|"
                         f"nice (got {resctl_mode!r})\n"); return 2
    if resctl_mode:
        os.environ["EH_RESCTL"] = resctl_mode   # before any provider() call below
    # --sequence-diagram: render a per-test text sequence diagram (mermaid + ASCII)
    # from each test's archived host/CP logs after the run. Intercept (pytest
    # wouldn't know the flag).
    seqdiag = False
    if "--sequence-diagram" in rest:
        rest = [t for t in rest if t != "--sequence-diagram"]; seqdiag = True
    # --auto-apply-idf-patches: apply tools/idf_patches to the resolved IDF in place
    # and proceed, instead of aborting on an unpatched IDF. Opt-in (a run normally
    # never mutates its own IDF); CI passes it (or sets EH_AUTO_APPLY_IDF_PATCHES=1).
    auto_apply_patches = os.environ.get("EH_AUTO_APPLY_IDF_PATCHES", "").lower() in ("1", "true", "yes")
    if "--auto-apply-idf-patches" in rest:
        rest = [t for t in rest if t != "--auto-apply-idf-patches"]; auto_apply_patches = True
    # --prewarm: opt in to the two-phase build-all-then-run (see Phase 1 below).
    # Off by default; pass it (CI does) for cold-cache/parallel runs.
    prewarm = False
    if "--prewarm" in rest:
        rest = [t for t in rest if t != "--prewarm"]; prewarm = True
    # Resource control, default-on when the platform offers it (F4c). A long run
    # belongs in its own cgroup: it then outlives the launching shell AND cannot
    # push the box into an OOM kill. Needs no privilege — a transient USER unit.
    # Degrades to renice, then to nothing; --resctl none opts out entirely.
    # Before the systemd hand-off, so it reaches the terminal, not just the journal.
    if os.environ.get("EH_RESCTL_INNER") != "1":
        _ccw = _caps().ccache_prepare()
        if _ccw:
            print(f"{_YELLOW}eh: {_ccw}{_RESET}")
    if os.environ.get("EH_RESCTL_INNER") != "1":
        try:
            sys.path.insert(0, str(REPO_ROOT / "tests"))
            from infra import rescontrol as _rc
            if _rc.provider() == "systemd":
                # Re-exec the ORIGINAL command line: `rest` has already had --jobs/
                # --prewarm/--sandbox stripped by the parser above, so rebuilding from
                # it silently drops flags.
                argv = [sys.executable] + list(sys.argv)
                unit = f"eh-test-{int(time.time())}"
                # A scope, not a service: the run stays in THIS terminal (live output,
                # Ctrl-C, real exit code) and inherits the env; only the cgroup changes.
                wrapped = _rc.wrap(argv, unit=unit, scope=True)
                print(f"{_DIM}eh: resource limits ({', '.join(f'{k}={v}' for k, v in _rc.limits().items())}) "
                      f"via scope {unit}{_RESET}", flush=True)
                return subprocess.call(wrapped, cwd=os.getcwd(),
                                       env=dict(os.environ, EH_RESCTL_INNER="1"))
            _rc.apply_self()
        except Exception:  # noqa: BLE001 — resource control must never break a run
            pass
    tests_root = REPO_ROOT / "tests"
    # 'emu' runs the whole emulator matrix (mcu + linux) in one (parallel) session.
    if substrate == "emu":
        suite_dirs = [tests_root / "suites", tests_root / "emu-mcu", tests_root / "emu-linux"]
    elif substrate in ("hw", "rpi", "linux"):
        suite_dirs = [tests_root / "suites"]  # shared corpus on hw / RPi / emu-linux
    else:
        suite_dirs = [tests_root / substrate]
    for d in suite_dirs:
        if not d.is_dir():
            sys.stderr.write(f"{_RED}eh:{_RESET} no suite dir: {d}\n"); return 2
    # Inject the default target(s) only if the user named none (a path/node-id).
    named_target = any(not t.startswith("-") and
                       ("/" in t or "::" in t or t.endswith(".py"))
                       for t in rest)
    targets = [] if named_target else [str(d) for d in suite_dirs]
    # Caveat: emus hold inotify instances — on EMFILE hangs raise
    # `sudo sysctl -w fs.inotify.max_user_instances=1024`. `--jobs N` overrides.
    if jobs == "auto" and substrate != "hw":
        # Delegated to the single quota-aware authority: ~3 busy CPUs/test (2 emus +
        # a worker), bounded by physical cores AND memory (derivation in caps.py).
        _cap = _caps().emu_pair_capacity()
        print(f"{_DIM}eh: --jobs auto → {_cap} (caps: physical//3 + mem bound; "
              f"--jobs N / EH_EMU_PAIR_SLOTS override){_RESET}")
        jobs = str(_cap)
    # loadgroup: @pytest.mark.xdist_group(...) cohorts (emu_heavy / bt_bumble /
    # emu_boots) run serially on ONE worker so heavy cells can't starve the
    # timing-sensitive ones; ungrouped tests load-balance across the rest.
    # Worker restart stays OFF: on xdist 3.8.0, loadgroup + restart wedges the
    # controller after a crash. A crash ends scheduling instead; the completeness gate
    # and recovery retry run what did not. Timeouts no longer crash workers (signal).
    xdist = ["-n", jobs, "--dist", "loadgroup"] if jobs and jobs not in ("1", "0") else []
    if xdist and not any(t.startswith("--max-worker-restart") for t in rest):
        xdist += ["--max-worker-restart", os.environ.get("EH_MAX_WORKER_RESTART", "0")]
    # In parallel the per-test names and the live INFO trace interleave across
    # workers into unreadable noise; the end-of-run FAILURES + `-ra` summary +
    # report.md are the record. So under --jobs collapse to a progress line (dots)
    # and silence live logging. Sequential runs keep the inline step trace
    # (log_cli). Placed before *rest, so a user-passed -v / -o log_cli=true wins.
    quiet = ["-q", "-o", "log_cli=false"] if xdist else []
    # ── Phase 0: PROVISION + PROBE ──────────────────────────────────────────
    # Resolve the toolchain ONCE, fail-fast, then probe runner capabilities before any
    # build or emu launch: testvenv, IDF (+patches) below, and the caps snapshot that
    # sizes everything (--jobs, build pool, emu-pair cap) + refuses a too-small runner.
    _phase("PROVISION")
    py = _ensure_testvenv()
    # One run dir shared by the controller AND every xdist worker (workers can't
    # see the controller's in-process state), so the reporter + per-test log
    # archive all land under the same runs/<ts>/. Workers read it from the env.
    env = os.environ.copy()
    # --sandbox is the front door; it sets EH_SANDBOX for the workers (build_fw + the emu
    # launch read it via infra.sandbox). Flag wins over an inherited env value.
    if sandbox_mode is not None:
        env["EH_SANDBOX"] = sandbox_mode
    if resctl_mode:
        env["EH_RESCTL"] = resctl_mode
    # One run dir under the single lab root (tests/.work/runs/<ts>/), shared by
    # the controller AND every xdist worker (workers read it from the env).
    run_dir = tests_root / ".work" / "runs" / time.strftime("%Y%m%d-%H%M%S")
    env["EH_RUN_DIR"] = str(run_dir)
    # eh.py owns the FINAL (post-second_chance) matrix + summary render; tell the
    # reporter to defer its per-invocation matrix so a soon-to-be-recovered flake
    # isn't shown as a hard ✗ (see eh_reporter.summarize).
    env["EH_DEFER_MATRIX"] = "1"
    regression = "--regression" in rest or "--regression-exclude-sanity" in rest
    # The shared corpus reads this to pick its provider (default emu). hw/rpi/linux
    # select real/remote providers; everything else runs under 'emu'.
    env["EH_SUBSTRATE"] = substrate if substrate in ("hw", "rpi", "linux") else "emu"
    # Preflight: an ESP-firmware run needs the tools/idf_patches applied to the
    # resolved IDF (SW_AGGR/SDIO asserts at boot otherwise). Verify+apply+stop
    # BEFORE any build, so a wrong/unpatched IDF fails fast with a clear message
    # instead of building, booting, asserting, and flooding the logs.
    rc = _submodule_preflight()
    if rc:
        return rc
    if substrate.startswith("emu"):
        rc = _emu_preflight()
        if rc:
            return rc
    if env["EH_SUBSTRATE"] in ("emu", "hw"):
        rc = _idf_preflight(auto_apply=auto_apply_patches)
        if rc:
            return rc
    # Probe: snapshot runner capabilities (caps.json) and REFUSE a runner too small to
    # host even one emu pair — never silently serialize (R7 chameleon). EH_EMU_PAIR_SLOTS
    # is the escape hatch.
    caps = _caps()
    try:
        run_dir.mkdir(parents=True, exist_ok=True)
        caps.emit_caps_json(run_dir / "caps.json")
    except OSError:
        pass
    if env["EH_SUBSTRATE"] == "emu":
        try:
            caps.require_emu_capacity(1)
        except caps.CapsError as e:
            sys.stderr.write(f"{_RED}eh: {e}{_RESET}\n")
            return 2
    # Keep the two bridged emu guest-clocks locked. The spi_fd CP (bridge slave)
    # WFI-fast-forwards its guest clock far ahead of the wall-paced host during the
    # FD idle keep-alive (~60x seen), distorting CP task scheduling enough to
    # intermittently not clock an inbound frame-forward out over SPI — the spi_fd
    # flake (port_routing) and the under-load control_plane/wifi_connected races.
    # Pacing the slave's idle skip toward wall time mitigates it with no other-wire
    # regression. emu only; a user-set value still wins.
    if env["EH_SUBSTRATE"] == "emu":
        env.setdefault("EH_EMU_SLAVE_PACE", "1")
    # Under parallel load the emu runs slower than wall-clock (esp. the byte-by-byte
    # uart CP and spi_fd), so a healthy-but-starved heavy test (power-save cycles,
    # 40-RPC sweeps) can race a fixed per-expect timeout. Widen every expect budget
    # in proportion to the job count so that stops flaking. HW is real-time (no
    # scaling); an explicit EH_EXPECT_SCALE always wins.
    auto_scaled = False  # did WE set EH_EXPECT_SCALE (vs a user override)?
    if substrate != "hw" and "EH_EXPECT_SCALE" not in os.environ:
        try:
            _n = int(jobs) if jobs else 1
        except ValueError:
            _n = 1
        if _n >= 8:
            env["EH_EXPECT_SCALE"] = str(1 + _n // 8)  # jobs 15→2x, 24→4x, 32→5x
            auto_scaled = True
            print(f"{_DIM}eh: EH_EXPECT_SCALE={env['EH_EXPECT_SCALE']} "
                  f"(emu runs slow under {_n}-way load; widening expect budgets){_RESET}")
    cmd = [str(py), "-m", "pytest", "-c", str(tests_root / "pytest.ini"),
           *xdist, *quiet, *targets, *rest]

    # HW mutual exclusion: hold a lease on the bench for the run so a second
    # `eh.py test hw` (or a manual `hw acquire`) on this host can't collide. The
    # bench is auto-registered from lab.local.json (declared chips, no probe); a
    # crashed run's lease auto-expires. emu/linux need no lease (private benches).
    lease_db = lease_name = None
    if substrate == "hw":
        import json
        # ── fail-early pre-flight: cheap checks BEFORE any build/flash so a
        # misconfigured bench fails in <1s, not after a 60s build. ──
        pf = []
        if not resolve_idf():
            pf.append("ESP-IDF not found (eh.py install / set-idf-path)")
        try:
            _lc = json.loads((tests_root / "lab.local.json").read_text())
        except (OSError, ValueError):
            _lc = {}
        for _role, _key in (("host", "host_port"), ("cp", "cp_port")):
            _p = os.environ.get(f"EH_{'HOST' if _role == 'host' else 'CP'}_PORT") or _lc.get(_key)
            if not _p:
                pf.append(f"no {_key} (set tests/lab.local.json or EH_*_PORT)")
            elif not os.path.exists(_p):
                pf.append(f"{_role} port {_p} not present (bench unplugged?)")
        if pf:
            sys.stderr.write(f"{_RED}eh: HW bench not ready — aborting before build:{_RESET}\n")
            for m in pf:
                sys.stderr.write(f"  ✗ {m}\n")
            sys.stderr.write(f"  run {_DIM}eh.py hw doctor{_RESET} for details\n")
            return 2
        sys.path.insert(0, str(tests_root))
        from infra import lab_db
        lease_name = bench_name or "local"
        lease_db = lab_db.connect()
        if not lab_db.get_bench(lease_db, lease_name):
            lc = {}
            try:
                lc = json.loads((tests_root / "lab.local.json").read_text())
            except (OSError, ValueError):
                lc = {}
            if lc.get("host_port") and lc.get("cp_port"):
                lab_db.add_device(lease_db, f"{lease_name}-host", target=lc.get("host_target"),
                                  port=lc["host_port"])
                lab_db.add_device(lease_db, f"{lease_name}-cp", target=lc.get("cp_target"),
                                  port=lc["cp_port"])
                lab_db.add_bench(lease_db, lease_name,
                                 {"host": f"{lease_name}-host", "cp": f"{lease_name}-cp"},
                                 board=(lc.get("host_board_sdkconfig") or [None])[0],
                                 transports=lc.get("transports", ["sdio"]), caps=lc.get("caps", []))
        if lab_db.get_bench(lease_db, lease_name):
            try:
                lab_db.acquire(lease_db, lease_name, want=lab_db.DEFAULT_HOLD,
                               reason="eh.py test hw")
                print(f"{_DIM}eh: leased bench '{lease_name}' for this run{_RESET}")
            except (ValueError, RuntimeError) as e:
                sys.stderr.write(f"{_RED}eh:{_RESET} {e}\n"); return 1

    # ── Phase 1: prewarm build (emu only) ───────────────────────────────────
    # Cold-cache firmware builds run all-cores ninja; overlapping them with the
    # emu run starves the latency-sensitive emu guest clocks (esp. the slow uart
    # CP) -> spurious timeouts (the `rm -rf .work`/CI failure mode). So build
    # EVERY firmware the run needs FIRST with emus disabled (targets skip launch
    # under EH_PREWARM), then run below on a warm cache with no compilers
    # competing. ccache keeps repeat builds cheap; a build failure just surfaces
    # in the real run below. Opt-in via --prewarm (CI passes it); a warm-cache
    # local run doesn't need it.
    prewarm_secs = 0.0
    if env["EH_SUBSTRATE"] == "emu" and xdist and prewarm:
        pre_env = env.copy()
        pre_env["EH_PREWARM"] = "1"
        pre_env["EH_RUN_DIR"] = str(run_dir / "prewarm")
        # Size via caps.build_pool (concurrent BUILDS, not cores) — not the emu pool.
        buildpool = _caps().build_pool()
        # Build must NOT run under the 900s per-test cap (a cold BT build alone nears
        # it; cp+host is two). Give this phase its own budget from EH_BUILD_TIMEOUT
        # (per-firmware) and size the stall watchdog above it, in pre_env so the
        # external watchdog can't killpg a legitimately-slow build mid-compile.
        build_budget = float(os.environ.get("EH_BUILD_TIMEOUT") or 1800)
        prewarm_cap = build_budget * 2 + 300     # cp+host cold builds per prewarm item
        pre_env["EH_STALL_TIMEOUT"] = str(prewarm_cap + 300)
        pre_xdist = ["-n", str(buildpool), "--dist", "load",
                     "--max-worker-restart", str(2 * buildpool)]
        # Inject --timeout BEFORE *rest so a user-set --timeout still wins; else this
        # overrides pytest.ini's 900s for the build phase only.
        pre_cmd = [str(py), "-m", "pytest", "-c", str(tests_root / "pytest.ini"),
                   *pre_xdist, "-q", "-o", "log_cli=false",
                   f"--timeout={prewarm_cap:.0f}", *targets, *rest]
        print(f"{_DIM}eh: prewarm — building all firmware (emus off, {buildpool}-way, "
              f"no per-test cap) before the run{_RESET}")
        _pt = time.time()
        pre_rc = _supervise(pre_cmd, str(tests_root), pre_env, run_dir / "prewarm",
                            prewarm_cap, label="prewarm")
        prewarm_secs = time.time() - _pt
        if pre_rc != 0:
            print(f"{_YELLOW}eh: prewarm did not complete cleanly (rc={pre_rc}); the "
                  f"real run will build any missing firmware under its own cap{_RESET}")
        else:
            print(f"{_DIM}eh: prewarm done ({prewarm_secs:.0f}s); emu run on warm cache{_RESET}")

    print(f"{_DIM}$ {' '.join(cmd)}{_RESET}")
    _t0 = time.time()
    try:
        rc = _supervise(cmd, str(tests_root), env, run_dir, _per_test_timeout(rest))
        # ── second_chance: isolated retry of parallel-run failures ──────────
        # Any @second_chance test that FAILED phase 1 is re-run one-at-a-time
        # (-n0, no xdist) — it passes reliably alone but flakes under parallel emu
        # load. The isolated result is its FINAL outcome. Then eh.py renders the
        # merged matrix/summary and derives the pipeline exit code (below).
        retry_dir, hung = _run_second_chance(run_dir, py, tests_root, env, rest, auto_scaled)
        rc = _finalize_exit(run_dir, retry_dir, regression, rc, time.time() - _t0,
                            prewarm_secs, hung=hung)
    finally:
        if lease_db is not None:
            lab_db.release(lease_db, lease_name)  # free our run's lease
    if seqdiag:
        _render_seqdiags(run_dir)
    return rc


def _failed_second_chance(run_dir: Path):
    """nodeids of @second_chance tests that FAILED in the parallel phase (from the
    controller-owned events.jsonl)."""
    sys.path.insert(0, str(REPO_ROOT / "tests"))
    try:
        from infra import eh_reporter
    except Exception:  # noqa: BLE001
        return []
    def _clean(n):
        # Strip the xdist_group display suffix ("nodeid@group"): pytest can't
        # resolve it in the -n0 retry — one unmatched id usage-errors (exit 4)
        # and aborts the WHOLE retry, silently killing every recovery.
        i = n.rfind("@")
        return n[:i] if i > n.rfind("]") else n
    return sorted({_clean(e["nodeid"]) for e in eh_reporter._load_ends(run_dir)
                   if e.get("outcome") == "failed" and e.get("second_chance")})


def _incomplete_nodeids(run_dir: Path):
    """Expected-to-run nodeids (conftest collected.txt) that produced NO result — hung
    tests killed with their worker, or tests never assigned after an xdist abort. These
    are re-run isolated alongside the flaky failures so the inventory completes."""
    p = Path(run_dir) / "collected.txt"
    if not p.exists():
        return []
    sys.path.insert(0, str(REPO_ROOT / "tests"))
    try:
        from infra import eh_reporter
    except Exception:  # noqa: BLE001
        return []
    def _key(n):
        i = n.rfind("@")
        return n[:i] if i > n.rfind("]") else n
    expected = [ln.strip() for ln in p.read_text().splitlines() if ln.strip()]
    done = {_key(e["nodeid"]) for e in eh_reporter._load_ends(run_dir)}
    return sorted({_key(n) for n in expected if _key(n) not in done})


def _run_second_chance(run_dir: Path, py, tests_root: Path, env: dict, rest, auto_scaled):
    """Phase 2: re-run each failed @second_chance / did-not-run nodeid ISOLATED (-n0),
    ONE pytest invocation PER nodeid (F2). A single item's os._exit (pytest-timeout)
    or watchdog kill then costs only that item — never the whole recovery batch, which
    a single -n0 call over all ids did (one slow test nuked the rest as spurious
    did-not-run). Each item writes its events.jsonl into the shared second_chance/ dir
    (so retry logs stay under second_chance/logs/); we append each to an aggregate and
    publish it as second_chance/events.jsonl for the reporter. Returns
    (retry_dir, incomplete) or (None, []) if nothing qualified."""
    flaky = _failed_second_chance(run_dir)
    incomplete = _incomplete_nodeids(run_dir)
    nodeids = sorted(set(flaky) | set(incomplete))
    if not nodeids:
        return None, []
    print(f"{_DIM}eh: recovery — {len(flaky)} flaky-fail + {len(incomplete)} did-not-run "
          f"test(s); isolated per-item retry (-n0) [→ final ↻/✓/✗/hung in the matrix below]:{_RESET}")
    for n in nodeids:
        print(f"{_DIM}    → {n}{_RESET}")
    # Harvest a killed worker's logs BEFORE reclaiming its scratch — order matters.
    try:
        sys.path.insert(0, str(REPO_ROOT / "tests"))
        from infra import lab as _lab
        h, fz, freed = _lab.harvest_killed_scratch(run_dir, nodeids)
        if h or freed:
            print(f"{_DIM}eh: recovery — froze {fz} log(s) from {h} killed-worker "
                  f"scratch dir(s); reclaimed {freed / 1e9:.1f}GB "
                  f"(free now {_lab.free_gb():.0f}GB){_RESET}")
    except Exception as e:  # noqa: BLE001 — housekeeping must never break recovery
        print(f"{_DIM}eh: recovery — scratch harvest skipped ({e}){_RESET}")
    # Let the box settle: the parallel phase just tore down ~15 emu pairs and
    # their net shims; retrying into that residue re-hits the same load-induced
    # boot-RPC (5s deadline) timeouts the retry exists to escape.
    time.sleep(8)
    retry_dir = run_dir / "second_chance"
    retry_dir.mkdir(parents=True, exist_ok=True)
    env2 = env.copy()
    # Isolated = un-starved: drop the parallel-load expect widening WE added so the
    # retry runs at real pace (a user-set EH_EXPECT_SCALE still wins — never touched).
    if auto_scaled:
        env2.pop("EH_EXPECT_SCALE", None)
    # Drop any path/node-id the user named (we name the exact nodeids); keep flags
    # like --regression so collection keeps the same items.
    flags = [t for t in rest if not (not t.startswith("-") and
             ("/" in t or "::" in t or t.endswith(".py")))]
    agg = retry_dir / ".events.agg.jsonl"
    agg.unlink(missing_ok=True)
    ev_file = retry_dir / "events.jsonl"
    for n in nodeids:
        env3 = env2.copy()
        env3["EH_RUN_DIR"] = str(retry_dir)  # logs → second_chance/logs/<slug>.FAIL
        cmd = [str(py), "-m", "pytest", "-c", str(tests_root / "pytest.ini"),
               "-p", "no:cacheprovider", *flags, n]
        print(f"{_DIM}$ {' '.join(cmd)}{_RESET}")
        # Per-item run dir writes/truncates second_chance/events.jsonl; harvest it
        # before the next item overwrites it, so one item's kill can't lose the rest.
        _supervise(cmd, str(tests_root), env3, retry_dir, _per_test_timeout(rest),
                   label="recovery")
        try:
            if ev_file.exists():
                with open(agg, "a") as a:
                    a.write(ev_file.read_text())
        except OSError:
            pass
    # Publish the aggregate as the canonical events.jsonl the reporter merges.
    try:
        if agg.exists():
            os.replace(agg, ev_file)
    except OSError:
        pass
    return retry_dir, incomplete


def _finalize_exit(run_dir: Path, retry_dir, regression: bool, raw_rc: int, elapsed=None,
                   prewarm=None, hung=None) -> int:
    """Render the merged matrix + truthful summary and derive the pipeline exit code
    from the MERGED results: non-zero iff some test with a FINAL failure is NOT
    allow_fail. A structural pytest exit (2 interrupted / 3 internal / 4 usage /
    5 no-tests) is preserved — only pass/fail (0/1) is overridden by our merge.
    `hung` = nodeids that DID NOT RUN in the parallel phase (killed with a wedged
    worker) so the reporter flags them distinctly from an ordinary parallel-load flake."""
    sys.path.insert(0, str(REPO_ROOT / "tests"))
    try:
        from infra import eh_reporter
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(f"{_RED}eh:{_RESET} summary render unavailable: {e}\n")
        return raw_rc
    res = eh_reporter.summarize(run_dir, retry_dir=retry_dir, regression=regression,
                               elapsed=elapsed, prewarm=prewarm, hung=hung)
    if raw_rc not in (0, 1):
        return raw_rc  # collection/usage/internal error — don't mask it
    if res["blocking"] or res.get("not_run"):
        return 1
    # A run where every selected test skipped proves nothing; exiting 0 reports it
    # as a pass. Only reachable when the skips are environmental (no emu, no bench).
    c = res.get("counts") or {}
    if c and not c.get("passed") and not c.get("recovered") and c.get("skipped"):
        sys.stderr.write(f"{_RED}eh:{_RESET} every selected test SKIPPED "
                         f"({c['skipped']}) — nothing ran, so this is not a pass.\n")
        return 1
    return 0


def _render_seqdiags(run_dir: Path) -> None:
    """After the run, turn each test's archived host/CP logs into a text sequence
    diagram (mermaid + ASCII) — an at-a-glance overview above the raw logs."""
    logs = run_dir / "logs"
    sys.path.insert(0, str(REPO_ROOT / "tests"))
    try:
        from infra.seqdiag import render_dir
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(f"{_RED}eh:{_RESET} seqdiag unavailable: {e}\n"); return
    if not logs.is_dir():
        print(f"{_DIM}eh: no archived logs at {logs} (nothing to diagram){_RESET}"); return
    made = 0
    for d in sorted(p for p in logs.iterdir() if p.is_dir()):
        out = render_dir(d)
        if out:
            (d / "seq.md").write_text(out); made += 1
    print(f"{_DIM}eh: sequence diagrams → {logs}/<test>/seq.md ({made} test(s)){_RESET}")


def cmd_manual_test(args) -> int:
    """Launch one half of an emu pair for interactive testing:
        eh.py manual-test emu-cp  <cp_project_dir>   [--bus sdio|uart] [opts] [-- emu args]
        eh.py manual-test emu-mcu <host_project_dir> [--bus sdio|uart] [opts] [-- emu args]

    The CP binds the bridge socket (stale one deleted); the MCU host connects.
    Both default to the same socket (/tmp/eh_manual_<bus>.sock) so the two
    commands pair up with no coordination. If the project's merged_flash.bin is
    missing it is built first with the --bus transport. Everything but the socket
    is defaulted to the ESP-Hosted conventions and overridable."""
    is_cp = (args.role == "emu-cp")
    bus = args.bus
    proj = Path(args.project_dir).expanduser().resolve()
    if not (proj / "CMakeLists.txt").is_file():
        sys.stderr.write(f"{_RED}eh:{_RESET} not an IDF project: {proj}\n")
        return 2

    emu_d = resolve_emu_dir()
    emu = emu_binary(emu_d) if emu_d else None
    if not emu or not emu.is_file():
        sys.stderr.write(f"{_RED}eh:{_RESET} esp-emu not built "
                         f"(run ./install.sh --with-emu or `eh.py set-esp-emu <dir>`)\n")
        return 2

    chip = args.chip or ("esp32c6" if is_cp else "esp32p4")
    sock = args.sock or f"/tmp/eh_manual_{bus}.sock"

    merged = proj / "build" / "merged_flash.bin"
    if not merged.is_file():
        idf = resolve_idf()
        if not idf:
            sys.stderr.write(f"{_RED}eh:{_RESET} IDF not found (`eh.py set-idf-path <dir>`)\n")
            return 2
        overlay = ""
        if bus == "uart":
            overlay = ("CONFIG_EH_TRANSPORT_CP_UART=y" if is_cp
                       else "CONFIG_ESP_HOSTED_HOST_TRANSPORT_BUS_UART=y")
        defaults = "sdkconfig.defaults"
        if overlay:
            (proj / ".ovl.defaults").write_text(overlay + "\n")
            defaults = "sdkconfig.defaults;.ovl.defaults"
        _phase("BUILD")
        script = (f'. "{idf}/export.sh" >/dev/null 2>&1 || exit 90; '
                  f'cd "{proj}" || exit 91; '
                  f'idf.py -D SDKCONFIG_DEFAULTS="{defaults}" set-target {chip} build && '
                  f'idf.py merge-bin -o merged_flash.bin')
        if subprocess.run(["bash", "-lc", script]).returncode != 0:
            sys.stderr.write(f"{_RED}eh:{_RESET} build failed: {proj} [{chip}, {bus}]\n")
            return 1

    # elf for symbolized backtraces (host example emits iperf.elf; else first *.elf)
    elf = proj / "build" / "iperf.elf"
    if is_cp or not elf.is_file():
        elf = next(iter(sorted((proj / "build").glob("*.elf"))), None)
    wire = "--hosted" if bus == "sdio" else "--hosted-uart"

    # Scratch workspace for mutable run state (the emu writes NVS back into the
    # flash image, so run a COPY — the project's build/ stays pristine). emu-cp
    # owns the session: it wipes + recreates the workspace; emu-mcu just joins.
    ws = Path("/tmp/manual_eh_workspace")
    if is_cp:
        shutil.rmtree(ws, ignore_errors=True)
    ws.mkdir(parents=True, exist_ok=True)
    flash = ws / ("cp_flash.bin" if is_cp else "host_flash.bin")
    shutil.copy2(merged, flash)

    cmd = [str(emu), "--chip", chip, "--firmware", str(flash)]
    if elf:
        cmd += ["--elf", str(elf)]
    if is_cp:
        try:
            os.unlink(sock)           # slave binds a fresh socket
        except OSError:
            pass
        cmd += [wire, f"bridge:slave:{sock}",
                "--hosted-wake-gpio", str(args.wake_gpio),
                "--net", f"user,hostfwd=tcp:127.0.0.1:{args.wake_port}-:22"]
    else:
        cmd += [wire, f"bridge:host:{sock}", "--hosted-reset-gpio", str(args.reset_gpio)]
        if bus == "uart":
            cmd += ["--hosted-reset-active-high"]   # UART host reset is active-high
    cmd += args.args or []

    _phase("RUN")
    print(f"{_DIM}$ {' '.join(cmd)}{_RESET}")
    if is_cp:
        print(f"eh: CP on {bus}, socket {sock} — wake from another shell:\n"
              f"    printf 'wakeup-host' | nc -w1 127.0.0.1 {args.wake_port}")
    else:
        print(f"eh: MCU host on {bus}, socket {sock} — start the CP first "
              f"(`eh.py manual-test emu-cp <cp_dir> --bus {bus}`)")
    return subprocess.run(cmd).returncode


def _sh(cmd: list[str], cwd: Path | None = None) -> int:
    print(f"{_DIM}$ {' '.join(cmd)}{_RESET}")
    return subprocess.run(cmd, cwd=str(cwd) if cwd else None).returncode


def _fail(msg: str) -> int:
    sys.stderr.write(f"{_RED}install failed:{_RESET} {msg}\n")
    return 1


def _bootstrap_rust() -> int:
    print(f"{_YELLOW}Rust (cargo) not found — bootstrapping rustup…{_RESET}")
    return subprocess.run(
        "curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs "
        "| sh -s -- -y --no-modify-path", shell=True).returncode


def _hw_esptool_env() -> None:
    """Make esptool reachable from eh.py's own process by exporting
    IDF_PYTHON_ENV_PATH (mirrors what the HW provider does at test time)."""
    if os.environ.get("IDF_PYTHON_ENV_PATH"):
        return
    idf = resolve_idf()
    if not idf:
        return
    r = subprocess.run(
        ["bash", "-lc", f'. "{idf}/export.sh" >/dev/null 2>&1 && printf "%s" "$IDF_PYTHON_ENV_PATH"'],
        capture_output=True, text=True)
    p = r.stdout.strip()
    if p and os.path.isdir(p):
        os.environ["IDF_PYTHON_ENV_PATH"] = p


def _serial_ports() -> list:
    import glob
    ports: list[str] = []
    for pat in ("/dev/cu.usbserial*", "/dev/cu.usbmodem*", "/dev/tty.usbserial*",
                "/dev/ttyUSB*", "/dev/ttyACM*"):
        ports += glob.glob(pat)
    return sorted(set(ports))


def _port_busy(port: str) -> str:
    """Return the PID holding the port, or '' if free (best-effort via lsof)."""
    r = subprocess.run(["lsof", "-t", port], capture_output=True, text=True)
    return r.stdout.strip().split("\n")[0] if r.returncode == 0 and r.stdout.strip() else ""


def _hw_probe(port: str) -> dict:
    sys.path.insert(0, str(REPO_ROOT / "tests"))
    from infra.hardware import chip_to_idf_target, eh_test_hw_probe_device
    r = eh_test_hw_probe_device(port)
    r["target"] = chip_to_idf_target(r.get("chip"))
    return r


def cmd_hw(args) -> int:
    """Inspect the HW bench — the single control point for HW test setup.

        eh.py hw probe    scan serial ports and esptool-detect each chip
        eh.py hw doctor   is the configured bench ready for `eh.py test hw`?
        eh.py hw free     kill any process holding the configured bench ports
    """
    import json
    action = getattr(args, "hw_action", None) or "doctor"
    if action in ("probe", "doctor", "import"):  # only these need esptool
        _hw_esptool_env()
    lab_local = REPO_ROOT / "tests" / "lab.local.json"
    cfg = {}
    if lab_local.is_file():
        try:
            cfg = json.loads(lab_local.read_text())
        except ValueError:
            pass

    if action == "probe":
        ports = _serial_ports()
        if not ports:
            print("no serial ports found (/dev/cu.usbserial* | /dev/ttyUSB* | …)")
            return 1
        print(f"scanning {len(ports)} serial port(s)…\n")
        found = []
        for p in ports:
            pid = _port_busy(p)
            if pid:
                print(f"  {p:34s} BUSY (pid {pid})")
                continue
            r = _hw_probe(p)
            if r.get("chip"):
                print(f"  {p:34s} {r['chip']} rev {r.get('revision','?')}  → {r['target']}")
                found.append((p, r))
            else:
                print(f"  {p:34s} —  ({(r.get('error') or 'no ESP chip')[:44]})")
        if len(found) >= 2:
            print("\nsuggested tests/lab.local.json (edit which is host vs cp):")
            h, c = found[0], found[1]
            print(json.dumps({"host_port": h[0], "cp_port": c[0],
                              "host_target": h[1]["target"], "cp_target": c[1]["target"],
                              "transports": ["sdio"],
                              "host_board_sdkconfig": ["CONFIG_<YOUR_BOARD>=y"],
                              "caps": []}, indent=2))
        return 0

    if action == "free":
        freed = 0
        for key in ("host_port", "cp_port"):
            port = os.environ.get(f"EH_{'HOST' if key=='host_port' else 'CP'}_PORT") or cfg.get(key)
            pid = _port_busy(port) if port else ""
            if pid:
                subprocess.run(["kill", pid])
                print(f"freed {port} (killed pid {pid})")
                freed += 1
        print("no busy bench ports" if not freed else f"freed {freed} port(s)")
        return 0

    # ── lab DB: inventory + leasing (infra/lab_db.py) ──────────────────
    if action in ("import", "status", "benches", "devices", "acquire",
                  "release", "renew", "remove"):
        import time as _t
        sys.path.insert(0, str(REPO_ROOT / "tests"))
        from infra import lab_db
        conn = lab_db.connect()

        def _dur(s):
            if not s:
                return None
            s = str(s).strip().lower()
            m = {"s": 1, "m": 60, "h": 3600}.get(s[-1])
            return int(float(s[:-1]) * m) if m else int(s)

        if action == "import":  # bootstrap the DB from lab.local.json (one bench)
            name = args.name or "local"
            hp, cp = cfg.get("host_port"), cfg.get("cp_port")
            if not hp or not cp:
                print("nothing to import — lab.local.json has no host_port/cp_port"); return 1
            hpr = _hw_probe(hp) if os.path.exists(hp) else {}
            cpr = _hw_probe(cp) if os.path.exists(cp) else {}
            lab_db.add_device(conn, f"{name}-host", chip=hpr.get("chip"),
                              target=cfg.get("host_target") or hpr.get("target"),
                              rev=hpr.get("revision"), port=hp)
            lab_db.add_device(conn, f"{name}-cp", chip=cpr.get("chip"),
                              target=cfg.get("cp_target") or cpr.get("target"),
                              rev=cpr.get("revision"), port=cp)
            lab_db.add_bench(conn, name, {"host": f"{name}-host", "cp": f"{name}-cp"},
                             board=(cfg.get("host_board_sdkconfig") or [None])[0],
                             transports=cfg.get("transports", ["sdio"]), caps=cfg.get("caps", []))
            print(f"imported bench '{name}' (host={hp}, cp={cp})"); return 0

        if action == "devices":
            rows = lab_db.list_devices(conn)
            if not rows:
                print("no devices — run `eh.py hw import`"); return 0
            for d in rows:
                print(f"  {d['name']:16s} {d['chip'] or '?':14s} {d['target'] or '':9s} {d['port'] or ''}")
            return 0

        if action == "benches":
            rows = lab_db.list_benches(conn)
            if not rows:
                print("no benches — run `eh.py hw import`"); return 0
            for b in rows:
                rd = " ".join(f"{r['role']}={r['name']}" for r in lab_db.bench_roles(conn, b["id"]))
                print(f"  {b['name']:16s} [{b['source']}] {rd}")
            return 0

        if action == "status":
            for s in lab_db.status(conn):
                if s["lease"]:
                    l = s["lease"]
                    exp = _t.strftime("%H:%M", _t.localtime(l["expires_at"]))
                    print(f"  {s['bench']:16s} LEASED by {l['owner']} until {exp}"
                          + (f"  ({l['reason']})" if l["reason"] else ""))
                else:
                    print(f"  {s['bench']:16s} free" + ("" if s["active"] else " (inactive)"))
            return 0

        if action == "acquire":
            try:
                l = lab_db.acquire(conn, args.name, want=_dur(args.dur),
                                   maximum=_dur(args.maxdur), reason=args.reason)
            except (ValueError, RuntimeError) as e:
                print(f"acquire failed: {e}"); return 1
            b = conn.execute("SELECT name FROM bench WHERE id=?", (l["bench_id"],)).fetchone()
            print(f"acquired '{b['name']}' as {l['owner']} until "
                  f"{_t.strftime('%H:%M', _t.localtime(l['expires_at']))}"); return 0

        if action == "release":
            n = lab_db.release(conn, args.name, force=args.force)
            print(f"released {n} lease(s)" if n else
                  "no releasable lease (not yours, or still within the min hold — use --force)")
            return 0

        if action == "renew":
            if not args.name:
                print("renew needs a bench name"); return 1
            try:
                exp = lab_db.renew(conn, args.name, want=_dur(args.dur))
            except (ValueError, RuntimeError) as e:
                print(f"renew failed: {e}"); return 1
            print(f"renewed '{args.name}' until {_t.strftime('%H:%M', _t.localtime(exp))}"); return 0

        if action == "remove":
            if not args.name:
                print("remove needs a bench name"); return 1
            lab_db.remove_bench(conn, args.name)
            print(f"removed bench '{args.name}'"); return 0

    # doctor — readiness of the configured bench
    ok = True
    idf = resolve_idf()
    esptool_py = os.environ.get("IDF_PYTHON_ENV_PATH")
    print(f"{'✓' if idf else '✗'} ESP-IDF: {idf or 'NOT FOUND (run eh.py install / set-idf-path)'}")
    print(f"{'✓' if esptool_py else '✗'} esptool env: {esptool_py or 'not resolved (export.sh failed?)'}")
    ok = ok and bool(idf)
    if not cfg:
        print("✗ bench: no tests/lab.local.json — run `eh.py hw probe` to discover ports")
        return 1
    print(f"  bench: board={cfg.get('host_board_sdkconfig')} transports={cfg.get('transports', ['sdio'])} caps={cfg.get('caps', [])}")
    for role, key in (("host", "host_port"), ("cp", "cp_port")):
        port = cfg.get(key)
        if not port:
            print(f"✗ {role}: no {key} in lab.local.json"); ok = False; continue
        if not os.path.exists(port):
            print(f"✗ {role}: {port} does not exist"); ok = False; continue
        pid = _port_busy(port)
        if pid:
            print(f"✗ {role}: {port} BUSY (pid {pid}) — run `eh.py hw free`"); ok = False; continue
        r = _hw_probe(port)
        declared = cfg.get(f"{role}_target")
        match = (not declared) or (r.get("target") == declared)
        mark = "✓" if (r.get("chip") and match) else "✗"
        detail = f"{r.get('chip','no chip')} rev {r.get('revision','?')} → {r.get('target')}"
        if declared and not match:
            detail += f"  (MISMATCH: lab.local.json says {declared})"
        print(f"{mark} {role}: {port}  {detail}")
        ok = ok and bool(r.get("chip")) and match
    print(f"\n{'READY — eh.py test hw' if ok else 'NOT READY (fix ✗ above)'}")
    return 0 if ok else 1


def _auto_install_mode() -> str:
    # ARM Linux boards (RPi/imx/yocto) are host DUTs, not firmware-build dev
    # boxes — default them to the no-fetch host mode. Everything else -> full.
    machine = platform.machine().lower()
    if sys.platform.startswith("linux") and machine.startswith(("arm", "aarch")):
        return "host"
    return "full"


def _pkg_manager() -> tuple[str | None, list[str]]:
    """(label, install-argv-prefix) for this host's package manager, or (None, [])."""
    if platform.system() == "Darwin" and shutil.which("brew"):
        return "brew", ["brew", "install"]
    if shutil.which("apt") or shutil.which("apt-get"):
        return "apt", ["sudo", "apt", "install", "-y"]
    if shutil.which("dnf"):
        return "dnf", ["sudo", "dnf", "install", "-y"]
    if shutil.which("pacman"):
        return "pacman", ["sudo", "pacman", "-S", "--noconfirm"]
    return None, []


def _rustc_version(cargo: str | None = None) -> tuple[int, int] | None:
    """(major, minor) of the rustc that PAIRS with `cargo` (its sibling — so a
    Homebrew cargo reports Homebrew's rustc, a rustup cargo reports rustup's),
    or the PATH rustc when cargo is None. None if rustc can't be run."""
    if cargo:
        sib = Path(cargo).with_name("rustc")
        exe = str(sib) if sib.exists() else (shutil.which("rustc") or "rustc")
    else:
        exe = shutil.which("rustc") or str(Path.home() / ".cargo" / "bin" / "rustc")
    try:
        out = subprocess.run([exe, "--version"], capture_output=True, text=True).stdout
    except Exception:
        return None
    m = re.search(r"rustc (\d+)\.(\d+)", out or "")
    return (int(m.group(1)), int(m.group(2))) if m else None


def _pick_cargo() -> tuple[str | None, tuple[int, int] | None]:
    """Pick a cargo whose paired rustc meets MIN_RUST. Prefer the PATH cargo;
    if it's too old, fall back to rustup's (~/.cargo/bin/cargo) — so a too-old
    cargo shadowing a newer one on PATH (e.g. Homebrew ahead of rustup) doesn't
    block the emu build. Returns (path, (maj,min)); path is the best candidate
    even if none meet MIN_RUST (the caller then warns)."""
    cands: list[str] = []
    w = shutil.which("cargo")
    if w:
        cands.append(w)
    rc = Path.home() / ".cargo" / "bin" / "cargo"
    if rc.exists() and str(rc) not in cands:
        cands.append(str(rc))
    best, best_ver = (cands[0] if cands else None), None
    for c in cands:
        v = _rustc_version(c)
        if v and v >= MIN_RUST:
            return c, v                 # first cargo that satisfies the floor
        if best_ver is None:
            best, best_ver = c, v
    return best, best_ver


# tool -> package name per manager (system build prerequisites only; Rust is
# handled separately via rustup, not the OS package manager).
_PREFLIGHT_TOOLS = {
    "git":     {"brew": "git",    "apt": "git",     "dnf": "git",     "pacman": "git"},
    "cmake":   {"brew": "cmake",  "apt": "cmake",   "dnf": "cmake",   "pacman": "cmake"},
    "python3": {"brew": "python", "apt": "python3", "dnf": "python3", "pacman": "python"},
    "ninja":   {"brew": "ninja",  "apt": "ninja-build", "dnf": "ninja-build", "pacman": "ninja"},
}


def _preflight_full(do_idf: bool, do_emu: bool, assume_yes: bool) -> bool:
    """Check minimal build prerequisites before any fetch/build.

    Recognized OS package manager -> offer to install the missing tools; else
    -> list them. Rust too old for esp-emu -> point at `rustup update`. The user
    can always skip (proceed anyway) or abort. Returns True to proceed."""
    mgr, prefix = _pkg_manager()
    need = ["git", "cmake", "python3", "ninja"] if do_idf else []
    missing = [t for t in need if shutil.which(t) is None]

    rust_old = None
    emu_cargo = None
    if do_emu:
        emu_cargo, ver = _pick_cargo()          # best cargo, not just PATH cargo
        if emu_cargo and ver and ver < MIN_RUST:
            rust_old = ver

    if not missing and rust_old is None:
        return True

    lines: list[str] = []
    if missing:
        lines.append("Missing build tools:")
        lines += [f"  MISS  {t}" for t in missing]
        if mgr:
            lines += ["", f"Install with {mgr}:",
                      f"  {' '.join(prefix + [_PREFLIGHT_TOOLS[t][mgr] for t in missing])}"]
        else:
            lines += ["", "Unrecognized OS — install these with your package manager:",
                      f"  {', '.join(missing)}"]
    if rust_old:
        if lines:
            lines.append("")
        lines += [f"esp-emu's cargo ({emu_cargo}) uses rustc "
                  f"{rust_old[0]}.{rust_old[1]}, older than the needed "
                  f">= {MIN_RUST[0]}.{MIN_RUST[1]}.",
                  "  fix: rustup update stable  (or: brew upgrade rust),",
                  "       or put a newer cargo earlier on PATH.  No rust? https://rustup.rs"]

    can_auto = bool(missing and mgr)
    lines += ["", "Options:"]
    if can_auto:
        lines.append("  [i] install the tools now (runs the command above)")
    lines += ["  [s] skip — install them yourself / proceed anyway",
              "  [a] abort — do nothing"]
    _box("install preflight — action needed", lines, _YELLOW)

    if assume_yes:
        return True                              # -y: proceed without prompting
    choice = input(f"{_BOLD}Choose [{'i/' if can_auto else ''}s/a]{_RESET} ").strip().lower()
    if choice.startswith("a"):
        print(f"{_YELLOW}Aborted — nothing changed.{_RESET}")
        return False
    if can_auto and choice.startswith("i"):
        _sh(prefix + [_PREFLIGHT_TOOLS[t][mgr] for t in missing])
        still = [t for t in missing if shutil.which(t) is None]
        if still:
            print(f"{_YELLOW}Still missing: {', '.join(still)} — proceeding; build may fail.{_RESET}")
    return True                                  # skip / after-install -> proceed


def _uninstall_deps(assume_yes: bool) -> int:
    """Remove only what install created: the machine-local .deps/ (esp-idf +
    esp-emu clones and eh.conf overrides). External checkouts a user points at
    ($IDF_PATH, or a set-idf-path / set-esp-emu target) are user-owned and
    never touched."""
    if not DEPS_DIR.exists():
        _box("nothing to uninstall", [
            f"{DEPS_DIR} is absent — install created nothing here.",
            "Active (external, user-owned) checkouts, left as-is:",
            f"  ESP-IDF : {resolve_idf() or '(none)'}",
            f"  esp-emu : {resolve_emu_dir() or '(none)'}",
        ], _YELLOW)
        return 0
    items = sorted(p.name for p in DEPS_DIR.iterdir())
    _box("esp_hosted uninstall -> remove .deps/", [
        f"path : {DEPS_DIR}",
        f"holds: {', '.join(items) if items else '(empty)'}",
        "",
        "Removes what install downloaded here + clears the config (eh.conf),",
        "so install.sh can run again. External checkouts we did NOT install",
        "($IDF_PATH, or a set-idf-path/set-esp-emu target) are left untouched.",
    ], _CYAN)
    if not _confirm("Remove .deps/ ?", assume_yes):
        print(f"{_YELLOW}Aborted — nothing changed.{_RESET}")
        return 1
    shutil.rmtree(DEPS_DIR, ignore_errors=True)
    _box("uninstalled", [
        f"removed {DEPS_DIR} (clones + config)",
        "config cleared — ./install.sh can fetch fresh again.",
        "Still resolvable (external, untouched):",
        f"  ESP-IDF : {resolve_idf() or '(none)'}",
        f"  esp-emu : {resolve_emu_dir() or '(none)'}",
    ], _GREEN)
    return 0


_SDIO_SLAVE_SRC = "components/esp_driver_sdio/src/sdio_slave.c"
# The exact guard line that caps one SDIO send descriptor at 4092 B, and what we
# rewrite it to (drop the upper bound so SW aggregation can send larger frames).
# Matching the EXACT line is version-agnostic: it lands wherever the line exists
# (5.3.5 / 5.4.4 / 5.5.3 / 5.5.4 / 6.0.x) regardless of surrounding context, and
# touches nothing else — no HAL headers, no line-number/context coupling.
_SDIO_CAP_OLD = 'SDIO_SLAVE_CHECK(len > 0 && len <= 4092, "length out of range: (0, 4092]", ESP_ERR_INVALID_ARG);'
_SDIO_CAP_NEW = 'SDIO_SLAVE_CHECK(len > 0, "len <= 0", ESP_ERR_INVALID_ARG);'

def _sdio_patch_marked_ok(idf_dir: Path) -> bool:
    """User ran `patch-idf --suppress` for this IDF -> stay silent."""
    marked = _read_conf().get("sdio_patch_skip", "")
    return str(Path(idf_dir).resolve()) in marked.split(os.pathsep)

def _sdio_patch_needed(idf_dir: Path) -> bool:
    """True iff sdio_slave.c still contains the exact 4092 send-cap guard line (and
    the user hasn't marked this IDF not-needed). Absent = never had it / already
    lifted / fixed upstream. Read straight from the resolved IDF (IDF_PATH etc.)."""
    if _sdio_patch_marked_ok(idf_dir):
        return False
    try:
        text = (idf_dir / _SDIO_SLAVE_SRC).read_text()
    except OSError:
        return False
    return _SDIO_CAP_OLD in text

def _report_idf_patch_status(idf_dir: Path) -> None:
    """Rectangular notice: the 4092 cap is present, here's how to lift it. We never
    modify an IDF on our own — the user runs `patch-idf`. Silent when not needed."""
    if not _sdio_patch_needed(idf_dir):
        return
    _box("ESP-IDF: SDIO send-cap (SW aggregation)", [
        "This ESP-IDF still uses the 4092-byte SDIO send cap.",
        "For SDIO co-processor builds, we use SW aggregation, for higher throughput.",
        "This change is already available on latest master.",
        "Either you can use it, or apply a patch.",
        "",
        "This change is only needed for SDIO builds.",
        "",
        "To apply, run:",
        "```",
        "eh.py patch-idf",
        "```",
        "",
        "Once applied, verify the changes using:",
        f"  cd {idf_dir}",
        "  git diff",
        "",
        "To suppress/unsuppress warning, call above command with --suppress/--unsuppress",
    ], _YELLOW)

def _rule(label, color=""):
    """Full-width, centered rule: '----- label -----' across the terminal."""
    try:
        width = min(shutil.get_terminal_size((80, 20)).columns, 80)
    except Exception:
        width = 80
    mid = f" {label} "
    pad = max(0, width - len(mid))
    line = ("-" * (pad // 2)) + mid + ("-" * (pad - pad // 2))
    return f"{color}{line}{_RESET}" if color else line


def apply_idf_patches(idf_dir: Path) -> int:
    """Lift the SDIO 4092-byte send cap by rewriting the one guard line in
    sdio_slave.c (exact-line match -> version-agnostic; no context or HAL-header
    coupling, nothing else touched). Line absent (never had it / already lifted)
    -> silent no-op. Returns non-zero only if the file can't be read/written."""
    src = idf_dir / _SDIO_SLAVE_SRC
    try:
        text = src.read_text()
    except OSError as e:
        sys.stderr.write(f"{_RED}eh:{_RESET} cannot read {src}: {e}\n")
        print(_rule("patch failed", _RED))
        return 1
    if _SDIO_CAP_OLD not in text:
        return 0                      # nothing to do -> stay silent
    try:
        src.write_text(text.replace(_SDIO_CAP_OLD, _SDIO_CAP_NEW))
    except OSError as e:
        sys.stderr.write(f"{_RED}eh:{_RESET} cannot write {src}: {e}\n")
        print(_rule("patch failed", _RED))
        return 1
    print(_rule(f"idf: {idf_dir}"))
    print(f"   file: {_SDIO_SLAVE_SRC}")
    print(f"   {_RED}- {_SDIO_CAP_OLD}{_RESET}")
    print(f"   {_GREEN}+ {_SDIO_CAP_NEW}{_RESET}")
    print(_rule("patch successful", _GREEN))
    return 0


def cmd_patch_idf(args) -> int:
    # --idf-path pins the exact IDF (e.g. the one a build's CMake gate reported);
    # otherwise auto-resolve (eh.conf -> .deps/esp-idf -> $IDF_PATH). This matters
    # when .deps exists but the build uses a different $IDF_PATH: without the pin,
    # resolve_idf() would target .deps and silently no-op the intended IDF.
    idf = Path(args.idf_path).expanduser() if getattr(args, "idf_path", None) else resolve_idf()
    if idf is None:
        sys.stderr.write(f"{_RED}eh:{_RESET} no ESP-IDF resolved "
                         f"(set IDF_PATH, eh.py set-idf-path, or ./install.sh)\n")
        return 1
    if getattr(args, "suppress", False):
        conf = _read_conf()
        skip = [s for s in conf.get("sdio_patch_skip", "").split(os.pathsep) if s]
        rp = str(Path(idf).resolve())
        if rp not in skip:
            skip.append(rp)
        conf["sdio_patch_skip"] = os.pathsep.join(skip)
        _write_conf(conf)
        print(f"{_GREEN}eh:{_RESET} {idf} marked — SDIO send-cap notice suppressed")
        return 0
    if getattr(args, "unsuppress", False):
        conf = _read_conf()
        skip = [s for s in conf.get("sdio_patch_skip", "").split(os.pathsep)
                if s and s != str(Path(idf).resolve())]
        if skip:
            conf["sdio_patch_skip"] = os.pathsep.join(skip)
        else:
            conf.pop("sdio_patch_skip", None)
        _write_conf(conf)
        print(f"{_GREEN}eh:{_RESET} {idf} — SDIO send-cap notice re-enabled")
        return 0
    # check-before-apply: rewrite the guard line only if present; silent otherwise.
    return apply_idf_patches(Path(idf))


def _submodule_preflight() -> int:
    """The vendored protobuf-c is a submodule; a fresh worktree without it fails
    deep in CMake ("Cannot find source file: protobuf-c/.../protobuf-c.c"). Say so
    in one line instead. Returns 0 to proceed, non-zero to abort."""
    pbc = REPO_ROOT / ("common/serializers/third_party/msg_codec/"
                       "protobuf-c/protobuf-c/protobuf-c.c")
    if pbc.exists():
        return 0
    sys.stderr.write(f"{_RED}eh:{_RESET} vendored protobuf-c is missing "
                     f"({pbc.relative_to(REPO_ROOT)}).\n"
                     f"    run: git submodule update --init --recursive\n")
    return 1


def _emu_preflight() -> int:
    """An emu substrate with no emulator must fail, not skip. Every emu test calls
    pytest.skip when the binary is absent, and a run where nothing executed still
    exits 0 — a green result that proves nothing. Check once, up front.
    Returns 0 to proceed, non-zero to abort."""
    d = resolve_emu_dir()
    b = emu_binary(d) if d else None
    if d and b and b.exists():
        return 0
    what = "no emu directory resolved" if not d else f"not built at {b}"
    sys.stderr.write(f"{_RED}eh:{_RESET} emu substrate requested but esp-emu is "
                     f"unavailable ({what}).\n"
                     f"    ./install.sh --with-emu   or   eh.py set-esp-emu <path>\n")
    return 1


def _idf_preflight(auto_apply: bool = False) -> int:
    """Verify (or, with auto_apply, apply) that the resolved ESP-IDF has
    tools/idf_patches applied before an ESP-firmware test run. Default: a test must
    not change its own environment, so this only checks — unpatched => report and
    stop (run `eh.py patch-idf` or ./install.sh); without the patches the run
    builds+boots into a runtime assert (SW_AGGR aggregate > sdio_slave send cap) and
    floods the logs. With auto_apply (--auto-apply-idf-patches / EH_AUTO_APPLY_IDF_PATCHES,
    for unattended CI) it lifts the patch on the resolved IDF in place and proceeds.
    Returns 0 to proceed, non-zero to abort the suite."""
    idf = resolve_idf()
    if idf is None:
        sys.stderr.write(f"{_RED}eh:{_RESET} no ESP-IDF resolved — run ./install.sh "
                         f"or `eh.py set-idf-path <dir>` before testing.\n")
        return 1
    # A configured override that was skipped (e.g. deleted) is exactly how a
    # wrong/unpatched IDF sneaks in — say so.
    conf = _read_conf()
    ov = conf.get("idf_path")
    if ov and Path(ov).expanduser().resolve() != Path(idf):
        sys.stderr.write(f"{_YELLOW}eh: configured idf_path '{ov}' is unusable "
                         f"(missing/not an IDF); using {idf} instead.{_RESET}\n")
    if _sdio_patch_needed(Path(idf)):
        if auto_apply:
            print(f"{_DIM}eh: --auto-apply-idf-patches — patching resolved IDF "
                  f"{idf} in place{_RESET}")
            rc = apply_idf_patches(Path(idf))
            if rc:
                return rc
            if _sdio_patch_needed(Path(idf)):  # re-verify it took
                sys.stderr.write(f"{_RED}eh:{_RESET} patch did not apply to {idf}\n")
                return 1
            return 0
        _report_idf_patch_status(Path(idf))
        sys.stderr.write(f"{_RED}eh:{_RESET} aborting test — SW_AGGR/SDIO asserts at "
                         f"boot with the 4092 cap in place (a test run never modifies "
                         f"your IDF; pass --auto-apply-idf-patches to apply it).\n")
        return 1
    return 0


def _ensure_repo_submodules() -> int:
    """Populate the esp_hosted repo's own git submodules (protobuf-c for msg_codec,
    esp-idf-kconfig, esp_wifi_remote). CI checks the tree out with submodules
    disabled (GIT_SUBMODULE_STRATEGY: none), so a firmware build otherwise dies at
    cmake configure: 'Cannot find source file: protobuf-c/protobuf-c/protobuf-c.c'.
    No-op when the tree isn't a git checkout (a component-registry/tarball copy
    vendors these sources) or when already populated (fast path, no git call)."""
    if not (REPO_ROOT / ".git").exists():
        return 0
    sentinel = (REPO_ROOT / "common/serializers/third_party/msg_codec"
                / "protobuf-c/protobuf-c/protobuf-c.c")
    if sentinel.is_file():
        return 0
    print(f"{_CYAN}eh:{_RESET} initializing repo submodules "
          f"(protobuf-c / esp-idf-kconfig / esp_wifi_remote)…")
    if _sh(["git", "submodule", "update", "--init", "--recursive",
            "--depth", "1", "--jobs", "8"], REPO_ROOT):
        return _fail("esp_hosted submodules")
    return 0


def cmd_install(args) -> int:
    idf_dir, emu_dir = DEPS_DIR / "esp-idf", DEPS_DIR / "esp-emu"
    # Completion marker for our .deps IDF: written ONLY after clone + submodules
    # + toolchains all succeed. Its absence (with the dir present) = a half-done
    # / interrupted install that must be resumed, not reported as complete.
    idf_mark = DEPS_DIR / ".idf_ready"
    if getattr(args, "uninstall", False):
        return _uninstall_deps(args.yes)
    # The repo's OWN submodules (protobuf-c for msg_codec, kconfig, esp_wifi_remote)
    # must be present for ANY firmware/host build — ensure them first, before mode
    # branching, so a submodule-less CI checkout self-heals here.
    rc = _ensure_repo_submodules()
    if rc:
        return rc
    # Front-door path overrides: point install at existing checkouts (same eh.conf
    # write as `eh.py set-idf-path`/`set-emu-path`), then proceed without cloning
    # that leg. --set-emu-path also opts emu in.
    if getattr(args, "set_idf_path", None):
        p = Path(args.set_idf_path).expanduser().resolve()
        if not (p / "export.sh").is_file():
            sys.stderr.write(f"{_RED}eh:{_RESET} no export.sh under {p}\n"); return 1
        conf = _read_conf(); conf["idf_path"] = str(p); _write_conf(conf)
        print(f"{_GREEN}idf-path set:{_RESET} {p} {_DIM}(overrides .deps){_RESET}")
    if getattr(args, "set_emu_path", None):
        d = Path(args.set_emu_path).expanduser().resolve()
        if not d.is_dir():
            sys.stderr.write(f"{_RED}eh:{_RESET} not a directory: {d}\n"); return 1
        conf = _read_conf(); conf["emu_path"] = str(d); conf.pop("esp_emu", None); _write_conf(conf)
        args.with_emu = True
        print(f"{_GREEN}emu-path set:{_RESET} {d} {_DIM}(overrides .deps){_RESET}")
    # Two modes: 'full' (ESP-IDF + esp-emu) and 'host' (Linux host, no fetch).
    # No flag -> auto-detect from the machine (ARM Linux host -> 'host').
    mode = getattr(args, "mode", None)
    if mode is None:
        mode = _auto_install_mode()
        print(f"{_CYAN}eh:{_RESET} no mode given -> auto-selected "
              f"{_BOLD}--{mode}{_RESET} for this machine "
              f"({platform.system()}/{platform.machine()}). "
              f"Override with --full or --host.")
    do_idf = mode == "full" and not args.skip_idf
    # esp-emu (emulator test harness) is OPT-IN: not needed to build or flash,
    # only for `eh.py test emu`. Default full install is ESP-IDF only.
    do_emu = mode == "full" and args.with_emu and not args.skip_emu
    if mode == "host":
        # No IDF/esp-emu to fetch — the Linux host builds with plain toolchain.
        # But DO preflight the toolchain (fail-early) and tell the user exactly
        # what to run next, so an install never ends on a silent "nothing".
        need = {"cmake": "cmake", "gcc": "gcc", "make": "make"}
        have = {name: shutil.which(exe) for name, exe in need.items()}
        kdir = Path(f"/lib/modules/{platform.release()}/build")
        khdr = kdir.exists()
        missing = [n for n, p in have.items() if p is None]
        lines = [
            "Linux host needs no ESP-IDF / esp-emu — it builds with the",
            "system toolchain. Preflight of that toolchain:",
            "",
        ]
        for n, p in have.items():
            lines.append(f"  {'OK ' if p else 'MISS'}  {n:<6} {p or '(not found)'}")
        lines.append(f"  {'OK ' if khdr else 'MISS'}  kmod   "
                     f"{kdir if khdr else '(kernel headers missing)'}")
        if missing or not khdr:
            pkgs = " ".join(sorted(set(missing) | ({"linux-headers-$(uname -r)"}
                                                   if not khdr else set())))
            lines += ["",
                      "Install the missing pieces, then re-run this:",
                      f"  sudo apt install -y build-essential cmake {pkgs}".rstrip()]
            _box("host-linux preflight — INCOMPLETE", lines, _YELLOW)
            return 1
        lines += [
            "",
            "Toolchain OK. Nothing to download. Next:",
            "  . ./export.sh            # puts eh.py on PATH (bash/zsh)",
            "  . ./export.fish          # (fish)",
            "then build + load the kmod, and run a Linux host example:",
            "  cd host/linux/eh_host_linux_kmod/scripts",
            "  ./build.sh --bus sdio && sudo ./load.sh --bus sdio",
        ]
        _box("host-linux ready", lines, _GREEN)
        return 0
    # Install defaults to a self-contained .deps/ clone (which WE patch). An IDF
    # the user selected explicitly (set-idf-path) is reused as-is and never
    # patched on our own — missing patches are reported, not applied.
    force_idf = args.force or args.force_idf
    force_emu = args.force or args.force_emu
    # --force* = uninstall + fresh: drop the matching explicit override so the
    # fresh .deps/ clone (the default destination) is what gets used.
    if force_idf or force_emu:
        conf = _read_conf()
        if force_idf:
            conf.pop("idf_path", None)
        if force_emu:
            conf.pop("esp_emu", None); conf.pop("emu_path", None)
        if conf:
            _write_conf(conf)
        elif CONF_FILE.exists():
            CONF_FILE.unlink()
    resolved_idf = None if force_idf else _managed_idf()
    resolved_emu = None if force_emu else _managed_emu()
    emu_built = resolved_emu is not None and emu_binary(resolved_emu).exists()
    need_rust = do_emu and _pick_cargo()[0] is None

    # Completion, not mere presence. Our .deps IDF is "done" only with the
    # ready-marker; an external IDF the user pointed at is assumed ready. emu is
    # "done" only when its binary is built. A dir present without its completion
    # signal = interrupted -> resume it.
    idf_ours = resolved_idf is not None and resolved_idf == idf_dir.resolve()
    idf_done = resolved_idf is not None and (not idf_ours or idf_mark.exists())

    # We OWN .deps/esp-emu, so a stale binary there is ours to rebuild without
    # asking. An emu the user pointed us at with `set-esp-emu` is theirs: never
    # build in someone else's tree, just tell them it looks stale.
    emu_ours = resolved_emu is not None and resolved_emu == emu_dir.resolve()
    emu_is_stale = resolved_emu is not None and emu_stale(resolved_emu)
    emu_stale_external = emu_is_stale and not emu_ours

    need_idf   = do_idf and not idf_done               # clone/submodules/toolchains
    need_fetch = do_emu and resolved_emu is None        # clone esp-emu
    # Rebuild on staleness, not mere presence - but only for our own clone.
    need_build = do_emu and (not emu_built or (emu_is_stale and emu_ours))

    # Nothing to do for this mode -> report and exit; no prompt, no clone.
    if not need_idf and not need_fetch and not need_build:
        # Patch only OUR .deps clone. An external IDF the user pointed at is
        # never modified on our own — just report if it's missing the patch.
        if do_idf and resolved_idf:
            if idf_ours:
                if apply_idf_patches(resolved_idf):
                    return _fail("esp-idf patches")
            else:
                _report_idf_patch_status(resolved_idf)
        lines = [f"ESP-IDF : {resolved_idf}"]
        if do_emu:
            lines += [f"esp-emu : {resolved_emu}",
                      f"          binary {emu_binary(resolved_emu)}"]
            rev = emu_revision(resolved_emu)
            if rev:
                lines.append(f"          rev    {rev}")
            if not emu_ours:
                lines.append("          (external - set via set-esp-emu; "
                             "we never build in it)")
            if emu_stale_external:
                lines += ["",
                          f"{_YELLOW}esp-emu sources are NEWER than its binary "
                          f"— it may be running old code.{_RESET}",
                          "Rebuild it yourself:  cargo build --release  in "
                          f"{resolved_emu}"]
            behind = emu_behind(resolved_emu)
            if behind:
                lines += ["",
                          f"{_YELLOW}esp-emu is {behind} commit(s) behind its "
                          f"upstream (no fetch performed).{_RESET}",
                          "Update:  git -C "
                          f"{resolved_emu} pull  then re-run this installer."]
        lines += ["", "Force a refresh: --force (both) | --force-idf | --force-emu.",
                  "Next:  . ./export.sh"]
        if do_emu:
            lines.append("       then  python3 tools/eh.py test emu")
        else:
            lines.append("       (emulator tests need esp-emu: re-run with --with-emu)")
        _box("esp_hosted — already set up (nothing to do)", lines, _GREEN)
        return 0

    # Preflight minimal build prerequisites (offer install / skip / abort).
    if not _preflight_full(do_idf, do_emu, args.yes):
        return 1

    # esp-emu build happens in the existing checkout when we are only building it,
    # else in the fresh .deps clone.
    emu_build_dir = emu_dir if need_fetch else (resolved_emu or emu_dir)

    plan: list[str] = []
    if do_idf:
        if not need_idf:
            plan.append(f"ESP-IDF : reuse {resolved_idf}")
        else:
            plan += [f"ESP-IDF : clone {IDF_REPO}",
                     f"          ref '{args.idf_ref}' -> {idf_dir}",
                     f"          + install toolchains for {IDF_TARGETS}"]
    if do_emu:
        if need_rust:
            plan.append("Rust    : cargo missing -> bootstrap rustup")
        if need_fetch:
            plan += [f"esp-emu : clone {EMU_REPO}",
                     f"          ref '{args.emu_ref}' -> {emu_dir}"]
        else:
            plan.append(f"esp-emu : reuse {resolved_emu}"
                        + ("" if emu_built else " (build only)"))
        if need_build:
            why = "sources newer than binary" if (emu_built and emu_is_stale) \
                  else "not built yet"
            plan.append(f"esp-emu : cargo build --release  ({emu_build_dir})"
                        f"  [{why}]")
    plan.append("")
    if do_emu:
        plan += ["First run: ~20-40 min (ESP-IDF toolchain download dominates;",
                 "           esp-emu build ~2-5 min)."]
    else:
        plan += ["First run: ~20-40 min (ESP-IDF toolchain download dominates).",
                 "esp-emu (emulator test harness) not included — add --with-emu."]
    plan.append("Default destination is .deps/. Reuse an existing checkout instead:")
    if need_idf:
        plan.append(f"  eh.py set-idf-path {resolve_idf() or '<dir>'}")
    if need_fetch:
        plan.append(f"  eh.py set-esp-emu {resolve_emu_dir() or '<dir>'}")

    _box("esp_hosted install -> .deps/", plan, _CYAN)
    if not _confirm("Proceed?", args.yes):
        print(f"{_YELLOW}Aborted — nothing changed.{_RESET}")
        return 1

    DEPS_DIR.mkdir(parents=True, exist_ok=True)
    gi = DEPS_DIR / ".gitignore"
    if not gi.exists():
        gi.write_text("*\n")   # .deps is machine-local; never tracked

    if need_idf:
        idf_mark.unlink(missing_ok=True)         # will be re-marked only on full success
        # --force starts clean; otherwise resume (keep a good existing clone).
        if force_idf and idf_dir.exists():
            shutil.rmtree(idf_dir, ignore_errors=True)
        if idf_dir.exists() and not (idf_dir / ".git").exists():
            shutil.rmtree(idf_dir, ignore_errors=True)   # junk, not a real clone
        if not (idf_dir / ".git").exists():
            # partial clone handles any ref (branch/tag/commit); submodules below.
            if _sh(["git", "clone", "--filter=blob:none", IDF_REPO, str(idf_dir)]):
                return _fail("esp-idf clone")
            if _sh(["git", "checkout", args.idf_ref], idf_dir):
                return _fail(f"esp-idf checkout '{args.idf_ref}'")
        # Idempotent — safe to re-run when resuming a half-done install.
        # Shallow + parallel submodules (IDF pins ~30) — matches --shallow-submodules.
        if _sh(["git", "submodule", "update", "--init", "--recursive",
                "--depth", "1", "--jobs", "8"], idf_dir):
            return _fail("esp-idf submodules")
        if apply_idf_patches(idf_dir):
            return _fail("esp-idf patches")
        if _sh(["bash", str(idf_dir / "install.sh"), IDF_TARGETS], idf_dir):
            return _fail("esp-idf install.sh")
        idf_mark.write_text("ok\n")              # completion: all IDF steps done
        resolved_idf = idf_dir

    if do_emu:
        if need_rust and _bootstrap_rust():
            return _fail("rustup bootstrap")
        if need_fetch:
            if emu_dir.exists():
                shutil.rmtree(emu_dir, ignore_errors=True)
            if _sh(["git", "clone", EMU_REPO, str(emu_dir)]):
                return _fail("esp-emu clone")
            if _sh(["git", "checkout", args.emu_ref], emu_dir):
                return _fail(f"esp-emu checkout '{args.emu_ref}'")
        if need_build:
            cargo, cver = _pick_cargo()
            cargo = cargo or str(Path.home() / ".cargo/bin/cargo")
            if cargo != shutil.which("cargo"):
                print(f"{_CYAN}eh:{_RESET} using {cargo} for esp-emu build "
                      f"(PATH cargo is older)")
            if _sh([cargo, "build", "--release"], emu_build_dir):
                ver = cver or _rustc_version(cargo)
                if ver and ver < MIN_RUST:
                    print(f"{_YELLOW}rustc {ver[0]}.{ver[1]} < "
                          f"{MIN_RUST[0]}.{MIN_RUST[1]} for esp-emu — "
                          f"run: rustup update stable{_RESET}")
                return _fail("esp-emu cargo build")

    _box("install complete", [
        f"ESP-IDF : {resolved_idf if do_idf else '(skipped)'}",
        f"esp-emu : {emu_binary(emu_build_dir) if do_emu else '(skipped)'}",
        "",
        "Next:  . ./export.sh     (bash/zsh)",
        "       . ./export.fish   (fish)",
        "Builds + the esplab runner now use these automatically.",
    ], _GREEN)
    return 0


def _argv_has_flag(argv: list[str], *flags: str) -> bool:
    """True iff `argv` contains any of `flags` as a standalone token
    or in the `--flag=...` long-option form."""
    for tok in argv:
        if tok in flags:
            return True
        for f in flags:
            if f.startswith("--") and tok.startswith(f + "="):
                return True
    return False


def _early_dispatch_or_reject(cwd: Path) -> int | None:
    """Classify cwd BEFORE argparse runs and route accordingly.

    Why pre-parse: argparse rejects unknown subcommands with "invalid
    choice" before any of our dispatch logic gets a turn, so a user
    standing in an IDF dir typing `eh.py <typo>` saw an eh-side
    argparse error instead of being forwarded transparently to
    `idf.py`.  Classifying first means eh.py never tries to
    second-guess `idf.py`'s arg surface — same argv, same stdio,
    let idf.py succeed or fail on its own terms.

    Bypass conditions (return None — let argparse + the rest of
    main() handle normally):
      - `--help` / `-h`        : user wants eh.py's own help.
      - `show`                 : diagnostic-only; works from anywhere.
      - `--project-dir <X>`    : user spelled out where to operate.

    Otherwise:
      - IDF project signature  : exec idf.py with sys.argv[1:].
                                 Does not return on success.
      - Host C-app / py-app    : return None — local eh handles it.
      - Anything else          : print reject reason, return rc=2.
    """
    argv = sys.argv[1:]
    if _argv_has_flag(argv, "--help", "-h"):
        return None
    if "show" in argv:
        return None
    # Environment/dep commands are project-independent (run from repo root).
    if any(c in argv for c in ("install", "set-idf-path", "get-idf-path", "patch-idf",
                               "set-emu-path", "get-emu-path", "set-esp-emu",
                               "get-esp-emu", "test", "manual-test", "hw")):
        return None
    if _argv_has_flag(argv, "--project-dir"):
        return None

    if _is_idf_project_dir(cwd):
        try:
            rel = cwd.resolve().relative_to(REPO_ROOT.resolve())
            display = str(rel) if rel.parts else "."
        except ValueError:
            display = str(cwd.resolve())
        return _dispatch_to_idf_py(display)

    if _is_host_project_dir(cwd) is not None:
        return None  # host — fall through to argparse

    sys.stderr.write(
        "eh: cwd is not a recognized project directory.  eh.py "
        "expects one of:\n"
        "  - HOST project: CMakeLists.txt with "
        "`include($ENV{EH_PATH}/tools/cmake/hosted_project.cmake)`\n"
        "                  (C or py — py adds main/main.py; source "
        "`. ./export.sh` first to set EH_PATH)\n"
        "  - IDF project : CMakeLists.txt with "
        "`include($ENV{IDF_PATH}/tools/cmake/project.cmake)` "
        "(dispatched to idf.py)\n"
        "`cd` into such a directory and re-run.\n")
    return 2


def main() -> int:
    # Pre-argparse cwd classification: in IDF dirs we exec idf.py with
    # its own subcommand list there.  In rejected dirs we bail out
    # before argparse can print a misleading "invalid choice".  Host
    # dirs (and bypassed cases — --help, show, --project-dir) fall
    # through to the usual parse-and-dispatch path below.
    early = _early_dispatch_or_reject(Path.cwd())
    if early is not None:
        return early

    # Top-level help / no command → the grouped command overview (below the
    # per-command `eh <cmd> -h`, which argparse still handles).
    _argv = sys.argv[1:]
    if not _argv or _argv[0] in ("-h", "--help", "help"):
        _grouped_help()
        return 0

    p = argparse.ArgumentParser(
        prog="eh",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument(
        "--project-dir",
        default=None,
        help="where sdkconfig + .eh.target live "
             "(default: the example dir when cwd is inside one — "
             "mirrors idf.py; falls back to repo root otherwise).",
    )
    p.add_argument(
        "--build-dir",
        default=None,
        help="where generated sdkconfig.h + sdkconfig.cmake + "
             "intermediate build artefacts go "
             "(default: <project-dir>/build).",
    )
    p.add_argument(
        "--bin-dir",
        default=None,
        help="where the linked executable(s) get staged after a "
             "successful build (default: <project-dir>/bin).",
    )
    p.add_argument(
        "--kconfig-dir",
        default=str(REPO_ROOT),
        help="root of the Kconfig source tree (default: repo root).  "
             "Source statements resolve relative to this dir.",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("set-target",
                        help="persist a port-type choice (one-time)")
    sp.add_argument("target", choices=VALID_TARGETS + tuple(TARGET_ALIASES),
                    help="port type to build for")
    sp.set_defaults(fn=cmd_set_target)

    sp = sub.add_parser("defconfig",
                        help="apply the persisted target's defaults")
    sp.set_defaults(fn=cmd_defconfig)

    sp = sub.add_parser("menuconfig",
                        help="launch the curses TUI for interactive edits")
    sp.set_defaults(fn=cmd_menuconfig)

    sp = sub.add_parser("save-defconfig",
                        help="write a minimal defconfig (diff from defaults)")
    sp.add_argument("-o", "--output", default=None,
                    help="output file (default: sdkconfig.defconfig)")
    sp.set_defaults(fn=cmd_save_defconfig)

    sp = sub.add_parser("reconfigure",
                        help="regenerate build/config/sdkconfig.{h,cmake}")
    sp.set_defaults(fn=cmd_reconfigure)

    sp = sub.add_parser("show", help="print current state (diagnostic)")
    sp.set_defaults(fn=cmd_show)

    # ── dependency / environment management (.deps/) ──
    sp = sub.add_parser("install",
                        help="fetch + build deps (esp-idf, esp-emu) into .deps/")
    sp.add_argument("--idf-ref", default=IDF_DEFAULT_REF,
                    help=f"esp-idf git ref: branch/tag/commit (default {IDF_DEFAULT_REF})")
    sp.add_argument("--emu-ref", default=EMU_DEFAULT_REF,
                    help=f"esp-emu git ref: branch/tag/commit (default {EMU_DEFAULT_REF})")
    sp.add_argument("-y", "--yes", action="store_true",
                    help="skip the confirmation prompt")
    sp.add_argument("--force", action="store_true",
                    help="re-clone/rebuild both legs from scratch even if usable")
    sp.add_argument("--force-idf", action="store_true",
                    help="re-clone esp-idf only (leave esp-emu as-is)")
    sp.add_argument("--force-emu", action="store_true",
                    help="re-clone + rebuild esp-emu only (leave esp-idf as-is)")
    sp.add_argument("--skip-idf", action="store_true", help="leave esp-idf untouched")
    sp.add_argument("--set-idf-path", metavar="DIR",
                    help="use your existing ESP-IDF, no clone (== `eh.py set-idf-path`)")
    sp.add_argument("--set-emu-path", metavar="DIR",
                    help="use your existing esp-emu (implies --enable-test; == `eh.py set-emu-path`)")
    sp.add_argument("--with-emu", "--enable-test", dest="with_emu",
                    action="store_true",
                    help="also set up esp-emu, the emulator test harness "
                         "(OPTIONAL — not needed to build/flash; only for "
                         "`eh.py test emu`). Default full install is ESP-IDF only.")
    sp.add_argument("--skip-emu", action="store_true",
                    help="deprecated no-op: esp-emu is opt-in via --with-emu now")
    sp.add_argument("--uninstall", action="store_true",
                    help="remove the machine-local .deps/ (esp-idf + esp-emu "
                         "clones and eh.conf overrides); external checkouts "
                         "($IDF_PATH, or a set-* target) are left intact")
    # Two modes. No arg = auto (ARM Linux host -> --host, else --full).
    _im = sp.add_mutually_exclusive_group(required=False)
    _im.add_argument("--full", dest="mode", action="store_const", const="full",
                     help="ESP dev setup: ESP-IDF (build + flash firmware). Default "
                          "on a dev machine. Add --with-emu for the emulator.")
    _im.add_argument("--host", "--host-linux", dest="mode", action="store_const",
                     const="host",
                     help="Linux userspace host only — no ESP-IDF, no esp-emu, "
                          "nothing to fetch (RPi/imx/no-internet friendly). "
                          "Default on an ARM Linux host.")
    sp.set_defaults(fn=cmd_install)

    sp = sub.add_parser("set-idf-path",
                        help="use an existing ESP-IDF checkout (overrides .deps)")
    sp.add_argument("path")
    sp.set_defaults(fn=cmd_set_idf_path)

    sp = sub.add_parser("get-idf-path",
                        help="print the ESP-IDF that builds will use")
    sp.add_argument("--raw", action="store_true", help="print only the path")
    sp.set_defaults(fn=cmd_get_idf_path)

    sp = sub.add_parser("patch-idf",
                        help="lift the SDIO 4092 send cap in the resolved ESP-IDF (idempotent)")
    g = sp.add_mutually_exclusive_group()
    g.add_argument("--suppress", action="store_true",
                   help="suppress the SDIO send-cap notice for this IDF (no change made)")
    g.add_argument("--unsuppress", action="store_true",
                   help="re-enable the SDIO send-cap notice for this IDF (the default)")
    sp.add_argument("--idf-path", metavar="DIR",
                   help="patch THIS ESP-IDF instead of the auto-resolved one "
                        "(pass the path your build reports; overrides .deps/$IDF_PATH)")
    sp.set_defaults(fn=cmd_patch_idf)

    sp = sub.add_parser("set-emu-path", aliases=["set-esp-emu"],
                        help="use an existing esp-emu checkout (overrides .deps)")
    sp.add_argument("path")
    sp.set_defaults(fn=cmd_set_esp_emu)

    sp = sub.add_parser("test",
                        help="run test suites via pytest: test "
                             "[emu-mcu|emu-linux|hw] [paths] [--jobs N] [pytest flags]")
    sp.add_argument("args", nargs=argparse.REMAINDER,
                    help="optional substrate 'emu-mcu'|'emu-linux'|'hw' "
                         "(default emu-mcu), then paths/node-ids, --jobs N, "
                         "--auto-apply-idf-patches (apply IDF patches to the resolved "
                         "IDF in place before the run; for CI — else an unpatched IDF "
                         "aborts), --prewarm (build all firmware first with emus off, "
                         "then run on a warm cache — for cold-cache/CI parallel runs), "
                         "and pytest flags (e.g. -k wake --jobs 2 --reruns 2)")
    sp.set_defaults(fn=cmd_test)

    sp = sub.add_parser("hw",
                        help="HW bench: probe/doctor/free + benches (list/add/remove) "
                             "+ leases (status/acquire/release/renew)")
    sp.add_argument("hw_action", nargs="?", default="doctor",
                    choices=["probe", "doctor", "free", "import", "status",
                             "benches", "devices", "acquire", "release", "renew", "remove"],
                    help="probe=scan+detect ports; doctor=is the bench ready (default); "
                         "free=kill procs on bench ports; import=load lab.local.json into "
                         "the lab DB; status=who holds which bench; benches/devices=list; "
                         "acquire/release/renew=lease a bench; remove=delete a bench")
    sp.add_argument("name", nargs="?", help="bench name (for acquire/release/renew/remove)")
    sp.add_argument("--for", dest="dur", help="lease duration, e.g. 30m, 2h (default 30m)")
    sp.add_argument("--max", dest="maxdur", help="hard lease cap, e.g. 4h")
    sp.add_argument("--reason", help="what the lease is for (shown in status)")
    sp.add_argument("--force", action="store_true", help="release even before the min hold")
    sp.set_defaults(fn=cmd_hw)

    sp = sub.add_parser("manual-test",
                        help="launch one half of an emu pair (emu-cp | emu-mcu) "
                             "for interactive testing; builds if needed")
    sp.add_argument("role", choices=["emu-cp", "emu-mcu"],
                    help="emu-cp = coprocessor (binds socket); emu-mcu = MCU host (connects)")
    sp.add_argument("project_dir", help="path to the IDF project to run")
    sp.add_argument("--bus", choices=["sdio", "uart"], default="sdio",
                    help="hosted transport (default: sdio)")
    sp.add_argument("--sock", default=None,
                    help="bridge socket (default: /tmp/eh_manual_<bus>.sock)")
    sp.add_argument("--chip", default=None,
                    help="override chip (default: esp32c6 for cp, esp32p4 for host)")
    sp.add_argument("--wake-port", type=int, default=2222, dest="wake_port",
                    help="CP wake hostfwd port (default: 2222)")
    sp.add_argument("--wake-gpio", type=int, default=2, dest="wake_gpio")
    sp.add_argument("--reset-gpio", type=int, default=54, dest="reset_gpio")
    sp.add_argument("args", nargs="*", help="extra args forwarded to esp-emu (after --)")
    sp.set_defaults(fn=cmd_manual_test)

    sp = sub.add_parser("get-emu-path", aliases=["get-esp-emu"],
                        help="print the esp-emu the runner will use")
    sp.add_argument("--raw", action="store_true", help="print only the binary path")
    sp.set_defaults(fn=cmd_get_esp_emu)

    # idf.py-style build/run wrappers around cmake.
    sp = sub.add_parser("build", help="cmake configure + build (optional single target)")
    sp.add_argument("target", nargs="?", default=None,
                    help="single target name (default: build all)")
    sp.add_argument("-j", "--jobs", type=int, default=None,
                    help="parallel jobs (default: cmake's -j auto)")
    sp.set_defaults(fn=cmd_build)

    for _name, _help in (("run", "build target then exec the binary"),
                         ("monitor", "alias for run (exec the built binary)")):
        sp = sub.add_parser(_name, help=_help)
        sp.add_argument("target", nargs="?", default=None,
                        help="target name (auto-detected from cwd "
                             "if omitted in an example dir)")
        sp.add_argument("-j", "--jobs", type=int, default=None,
                        help="parallel jobs (default: nproc)")
        sp.add_argument("args", nargs=argparse.REMAINDER,
                        help="forwarded to the binary (use -- to separate)")
        sp.set_defaults(fn=cmd_run)

    sp = sub.add_parser("clean", help="cmake --build clean (drop build artifacts)")
    sp.set_defaults(fn=cmd_clean)

    sp = sub.add_parser("fullclean",
                        help="remove build dir + sdkconfig + target file")
    sp.set_defaults(fn=cmd_fullclean)

    args = p.parse_args()

    # Environment/dep commands are project-independent — dispatch before the
    # project-dir + kconfig machinery below (which none of them need).
    if args.cmd in ("install", "set-idf-path", "get-idf-path", "patch-idf",
                    "set-emu-path", "get-emu-path", "set-esp-emu", "get-esp-emu",
                    "manual-test", "hw"):
        return int(args.fn(args))

    # Auto-detect: strict cwd-only check.  cwd's CMakeLists.txt must
    # carry the host-project signature (add_executable + esp_hosted
    # link) — mirrors idf.py's "cwd must have CMakeLists.txt with
    # project()" gate.  No downward search, no umbrella-from-repo-root
    # short-circuit.
    auto_project_dir = None
    auto_build_dir   = None
    auto_target      = None
    is_host_leaf     = False
    if args.cmd in ("build", "run", "monitor", "clean", "fullclean",
                    "set-target", "defconfig", "menuconfig",
                    "save-defconfig", "reconfigure", "show"):
        # Single detection path — hosted_project.cmake signature
        # covers both C apps (main/main.c → add_executable) and
        # py apps (main/main.py → add_custom_target). The leaf source
        # only differs for the `run` exec step (handled in cmd_run).
        target_name = _is_host_project_dir(Path.cwd())
        if target_name is not None:
            is_host_leaf     = True
            auto_project_dir = Path.cwd()
            auto_build_dir   = Path.cwd() / "build"
            auto_target      = target_name

    # Classification already happened in _early_dispatch_or_reject()
    # before argparse ran.  If we got here, cwd is either a valid
    # host project dir, an explicit --project-dir invocation, a
    # `show` diagnostic, or `--help`.  All of those proceed normally.

    if args.project_dir:
        project_dir = Path(args.project_dir).resolve()
    elif auto_project_dir is not None:
        project_dir = auto_project_dir.resolve()
    else:
        project_dir = REPO_ROOT

    if args.build_dir:
        build_dir = Path(args.build_dir).resolve()
    elif auto_build_dir is not None:
        build_dir = auto_build_dir.resolve()
    else:
        build_dir = project_dir / "build"

    if args.bin_dir:
        bin_dir = Path(args.bin_dir).resolve()
    else:
        bin_dir = project_dir / "bin"

    if hasattr(args, "target") and args.target is None and auto_target:
        args.target = auto_target
    kconfig_dir = Path(args.kconfig_dir).resolve()
    if not (kconfig_dir / "Kconfig").exists():
        sys.stderr.write(f"eh: missing Kconfig in kconfig-dir: {kconfig_dir}\n")
        return 1

    global _PATH_CFG
    _PATH_CFG = _PathCfg(project_dir=project_dir, build_dir=build_dir,
                         kconfig_dir=kconfig_dir, bin_dir=bin_dir)
    return int(args.fn(args))


if __name__ == "__main__":
    sys.exit(main())
