# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Firmware build cache — the copy-on-write build layer for the test framework.

Principle (see .meta2/changes/eh-testlab-framework/execution-model.md §2):
  * Most suites share ONE binary and never rebuild it.
  * A suite whose sdkconfig `overlay` changes the resolved config gets its OWN
    binary, keyed by a content hash — the base is never mutated (COW level 1).
  * Per-run writable flash is a reflink copy (COW level 2): parallel runs and NVS
    writes never touch the cached base.

Cheap and reused across the whole run; safe to call concurrently for distinct keys.
"""
import fcntl
import contextlib
import functools
import hashlib
import importlib.util
import json
import re
import logging
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

from infra import caps     # single sizing authority (quota-aware CPU/mem limits)
from infra import lab      # single source of truth for the .work/ layout
from infra import sandbox  # shared tool-agnostic isolation provider (EH_SANDBOX)
from infra import scratch  # shared build-scratch primitives (both builders import it)

_REPO = lab._REPO
_WORK = lab.WORK      # sandbox-bindable root; cache/ = reused build artifacts
_CACHE = lab.CACHE
_EH = _REPO / "tools" / "eh.py"

_log = logging.getLogger("eh")  # streamed live by pytest log_cli


def _env_float(key, default):
    try:
        return float(os.environ.get(key) or default)
    except (TypeError, ValueError):
        return float(default)


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def _idf_path() -> str:
    eh = _load("eh", _EH)
    p = eh.resolve_idf()
    if not p:
        raise RuntimeError("ESP-IDF not found (run ./install.sh or eh.py set-idf-path)")
    return str(p)


def _idf_path_opt():
    """IDF path or None — the Linux (posix) host build needs no IDF (builds with
    the system toolchain), so an IDF-less host (RPi/imx/yocto) must not be a hard
    error there."""
    try:
        return _idf_path()
    except RuntimeError:
        return None


@functools.lru_cache(maxsize=None)
def idf_identity() -> str:
    """IDF commit + applied tools/idf_patches + path; no tree scan, so it is stable on a
    cold disk. A change wipes every build dir (lab.check_idf_identity)."""
    p = _idf_path_opt()
    if not p:
        return ""
    try:
        head = subprocess.run(["git", "-C", p, "rev-parse", "HEAD"],
                              capture_output=True, text=True, timeout=30).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        head = "unknown"
    h = hashlib.sha1()
    for f in sorted((_REPO / "tools" / "idf_patches").glob("*.patch")):
        h.update(f.read_bytes())
    return f"{head}+{h.hexdigest()[:12]}@{p}"


@functools.lru_cache(maxsize=None)
def idf_has_macro(name: str) -> bool:
    """True if the RESOLVED IDF defines `name` anywhere in components/.

    For capability gates, probe the MACRO the firmware itself keys on — never an IDF
    version string. A version gate silently drifts from the code it guards (same class
    as the dangling-Kconfig audit), and backports break it outright. Cached per macro.
    """
    idf = _idf_path_opt()
    if not idf:
        return False
    try:
        r = subprocess.run(["grep", "-rl", "--include=*.h",
                            rf"\b{name}\b", str(Path(idf) / "components")],
                           capture_output=True, timeout=120)
        return bool(r.stdout.strip())
    except (OSError, subprocess.SubprocessError):
        return False


def _config_hash(example_rel: str, role: str, target: str, overlay, inject=None) -> str:
    parts = [example_rel, role, target, *sorted(overlay or [])]
    # Injected files (e.g. a staged CP OTA image in the host's LittleFS dir) get
    # their own scratch dir — else a bench that injects and one that doesn't share
    # a workdir with mismatched fingerprints and thrash-rebuild it in parallel,
    # racing the emu to a half-written ELF. Stable identity only (dest+src path),
    # not mtime/size — content changes rebuild in-place via cfg_fp.
    parts += [f"inject:{dest}={src}" for dest, src in sorted((inject or {}).items())]
    return hashlib.sha1("|".join(parts).encode()).hexdigest()[:8]


# Two classes of build input, because they need different rebuild actions:
#  - CODE: a .c/.h edit → fast incremental `idf.py build`.
#  - CFG: a CMakeLists/Kconfig/sdkconfig.defaults/overlay change → a full
#    reconfigure (`set-target`), because `idf.py build` won't re-read defaults
#    once sdkconfig exists (the bug that left new Kconfig feats uncompiled).
_CODE_EXTS = {".c", ".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh", ".inc",
              ".s", ".S", ".ld"}
_CFG_EXTS = {".cmake", ".csv", ".yml", ".yaml"}
_CFG_NAMES = {"CMakeLists.txt", "Kconfig", "Kconfig.projbuild", "idf_component.yml"}
_SRC_SKIP_DIRS = {"build", "managed_components", ".git", "__pycache__",
                  "node_modules", "dist", ".work"}


def _classify(fn: str):
    ext = os.path.splitext(fn)[1]
    if ext in _CODE_EXTS:
        return "src"
    # `Kconfig*` catches orsource'd sub-files (Kconfig.host.spi, Kconfig.cp.spi_hd,
    # Kconfig.ext, …), not just the literal "Kconfig" — else a per-transport Kconfig
    # edit silently reuses a stale sdkconfig on a same-overlay cached build.
    if (fn in _CFG_NAMES or fn.startswith("Kconfig") or ext in _CFG_EXTS
            or fn.startswith("sdkconfig.defaults")):
        return "cfg"
    return None


def _fingerprint_roots(example_rel: str, role: str):
    """The source trees a build compiles: the example role dir, the example's
    SIBLING common/ (shared across the example's roles, e.g. api_exerciser/common),
    the role's component tree, and the shared repo common/ + port/ trees. A change
    under any of these must invalidate the cache (missing the example-sibling
    common/ left a stale binary after editing examples/<ex>/common/*.c)."""
    src = _REPO / "examples" / example_rel / role
    ex_common = _REPO / "examples" / example_rel / "common"
    # Shared example components (e.g. esp_hosted_bt_host_stack) are pulled in via
    # override_path; edits there must invalidate the cache too.
    shared_components = _REPO / "examples" / "common_components"
    component = "coprocessor" if role == "cp" else "host"
    return [src, ex_common, shared_components, _REPO / component,
            _REPO / "common", _REPO / "port"]


def _fingerprints(example_rel: str, role: str, target: str, overlay, inject=None):
    """Return (src_fp, cfg_fp): content-cheap fingerprints (path+mtime_ns+size)
    over the build's trees, split by class. One walk. A false positive (mtime
    touched, same content) costs a fast rebuild; a false negative (skip a real
    change) is what the wide roots prevent. Overlay lines + injected files
    (e.g. a staged CP OTA image) fold into cfg_fp so a changed input rebuilds."""
    hs, hc = hashlib.sha1(), hashlib.sha1()
    seed = "|".join([example_rel, role, target]).encode()
    hs.update(seed)
    hc.update(seed + b"|" + "|".join(sorted(overlay or [])).encode())
    hc.update(f"|idf:{idf_identity()}".encode())  # an IDF switch must not reuse a stale image
    for dest, srcpath in sorted((inject or {}).items()):
        try:
            st = os.stat(srcpath)
            hc.update(f"|inject:{dest}:{st.st_mtime_ns}:{st.st_size}".encode())
        except OSError:
            hc.update(f"|inject:{dest}:missing".encode())
    for root in _fingerprint_roots(example_rel, role):
        if not root.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in _SRC_SKIP_DIRS]
            for fn in filenames:
                cls = _classify(fn)
                if not cls:
                    continue
                fp = os.path.join(dirpath, fn)
                try:
                    st = os.stat(fp)
                except OSError:
                    continue
                rec = f"{os.path.relpath(fp, _REPO)}:{st.st_mtime_ns}:{st.st_size}".encode()
                (hs if cls == "src" else hc).update(rec)
    return hs.hexdigest(), hc.hexdigest()


def _defaults_hash(workdir: Path) -> str:
    """Content of every sdkconfig.defaults* + the overlay: IDF does not re-read them once
    sdkconfig exists, so a change here (only here) forces a fresh sdkconfig."""
    h = hashlib.sha1()
    for f in sorted(workdir.glob("sdkconfig.defaults*")) + [workdir / ".ovl.defaults"]:
        if f.is_file():
            h.update(f.name.encode() + b"\0" + f.read_bytes() + b"\0")
    # A component added or removed changes the CMake graph, which ninja alone never sees.
    for c in sorted(workdir.glob("components/*/CMakeLists.txt")):
        h.update(str(c.relative_to(workdir)).encode() + b"\0")
    return h.hexdigest()


def _image_key(app: str, target: str, workdir: Path, inject: dict) -> str:
    """Identity of the image a configured build dir will produce: the app, its resolved
    sdkconfig (comments dropped), its top CMakeLists (scratch injections) and staged files."""
    h = hashlib.sha1(f"{app}|{target}".encode())
    cfg = (workdir / "sdkconfig").read_text(errors="replace").splitlines()
    h.update("\n".join(l for l in cfg if l and not l.startswith("#")).encode())
    h.update((workdir / "CMakeLists.txt").read_bytes())
    for dest in sorted(inject):
        h.update(dest.encode() + (workdir / dest).read_bytes())
    return h.hexdigest()


_SHARED = lab.SHARED

# Dry run with no real work. IDF's bootloader is an ExternalProject with BUILD_ALWAYS, so
# its steps (and the app size check) always show up, as does the output-less SDIO cap
# check; anything else is real work.
_NOOP_CHECK = ("ninja -C build -n all | grep -E '^\\[[0-9]+/[0-9]+\\]' "
               "| grep -vE \"bootloader|check_sizes.py|check_sdio_cap\" | grep -q . && exit 1 || exit 0")


def _write_if_changed(path: Path, data: bytes) -> None:
    if not (path.is_file() and path.read_bytes() == data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


@contextlib.contextmanager
def _flock(name: str):
    (_WORK / ".locks").mkdir(parents=True, exist_ok=True)
    with open(_WORK / ".locks" / f"{name}.lock", "w") as lk:
        lab.flock_with_heartbeat(lk)
        yield


def _note(label: str, msg: str) -> None:
    """One line per build decision, also kept in the run dir so a run can be audited."""
    _log.info("[build] %s — %s", label, msg)
    run = os.environ.get("EH_RUN_DIR")
    if run:
        try:
            with open(os.path.join(run, "builds.log"), "a") as f:
                f.write(f"{time.strftime('%H:%M:%S')} {label} — {msg}\n")
        except OSError:
            pass


def _owner_of(key: str, workdir: Path, key_of):
    """(dir, proj) of another built dir producing image `key`, else None."""
    try:
        owner, owner_proj = (_SHARED / key).read_text().split()
        owner = Path(owner)
        if (owner != workdir and (owner / "build" / "merged_flash.bin").is_file()
                and key_of(owner) == key):
            return owner, Path(owner_proj)
    except (OSError, ValueError):
        pass
    return None


def _register(key: str, workdir: Path, proj: Path) -> None:
    _SHARED.mkdir(parents=True, exist_ok=True)
    tmp = _SHARED / f".{key}.{os.getpid()}.{threading.get_ident()}"
    tmp.write_text(f"{workdir} {proj}\n")
    os.replace(tmp, _SHARED / key)


def _drop_defaulted(example_rel: str, role: str, target: str, overlay) -> list:
    """Sorted overlay minus lines the app's own sdkconfig.defaults[.<target>] already set to
    the same value: such a request is the plain image and must not get its own."""
    src = _REPO / "examples" / example_rel / role
    eff = {}
    for f in (src / "sdkconfig.defaults", src / f"sdkconfig.defaults.{target}"):
        if f.is_file():
            for line in f.read_text().splitlines():
                if line.startswith("CONFIG_") and "=" in line:
                    k, v = line.split("=", 1)
                    eff[k] = v.strip()
    keep = set()
    for o in overlay or []:
        k, _, v = o.partition("=")
        if eff.get(k) != v.strip():
            keep.add(o)
    return sorted(keep)  # order/duplicates do not make a new image


def plan_record(example_rel: str, role: str, target: str, overlay, inject=None) -> dict:
    """Plan mode (EH_PLAN): note the image a test asks for instead of building it."""
    inject = {k: str(v) for k, v in (inject or {}).items()}
    rec = {"app": f"{example_rel}/{role}", "example": example_rel, "role": role,
           "target": target, "overlay": sorted(overlay or []), "inject": sorted(inject),
           "inject_src": inject, "test": os.environ.get("PYTEST_CURRENT_TEST", "")}
    with open(os.environ["EH_PLAN"], "a") as f:
        f.write(json.dumps(rec) + "\n")
    return {"merged": "", "elf": "plan.elf", "bin": None, "dir": "", "build_log": ""}


def _result(workdir: Path, build_log: Path) -> dict:
    return {"merged": str(workdir / "build" / "merged_flash.bin"),
            "elf": str(next(workdir.glob("build/*.elf"))), "bin": None,
            "dir": str(workdir), "build_log": str(build_log)}


def _build_shared(owner: Path, owner_proj: Path, key: str, stage, key_of, alias_proj: Path,
                  label: str, _run_in) -> dict:
    """Serve a request from another dir producing the identical image: stage this request's
    sources into it and let ninja bring it up to date. None once the two images differ."""
    with _flock(owner_proj.name):
        stage(owner, owner_proj)
        if key_of(owner) != key:
            return None
        # Only a configure picks up an added component; that dir's own build does it.
        hf = owner_proj / ".defaults_hash"
        if not hf.is_file() or hf.read_text().strip() != _defaults_hash(owner):
            return None
        log = owner / "build.log"
        if _run_in(_NOOP_CHECK, owner / ".noop.log", cwd=owner) != 0:
            with lab.build_slot():
                jobs = caps.build_jobs(lab.build_capacity(), lab.live_emu_pairs())
                if _run_in(f'ninja -C build -j {jobs} all && idf.py merge-bin -o merged_flash.bin',
                           log, cwd=owner) != 0:
                    raise RuntimeError(f"firmware build failed: {label} (shared {owner}) — see {log}")
            if key_of(owner) != key:   # a Kconfig/CMake edit regenerated its sdkconfig
                return None
        lab.touch_last_used(owner_proj)
        lab.touch_last_used(alias_proj)
        return _result(owner, log)


_SECRET_KEY = re.compile(r"PASSWORD|PASSPHRASE|PSK|SECRET|TOKEN", re.I)


def _shown(overlay):
    """Overlay for a log line: secret values (Wi-Fi passwords etc.) masked."""
    return "+".join(re.sub(r"=.*", '="***"', o) if _SECRET_KEY.search(o.split("=", 1)[0]) else o
                    for o in overlay)


def build(example_rel: str, role: str, target: str, overlay=None, inject=None) -> dict:
    with lab.test_clock_paused():
        return _build(example_rel, role, target, overlay, inject)


def _build(example_rel: str, role: str, target: str, overlay=None, inject=None) -> dict:
    """Build firmware for examples/<example_rel>/<role> at <target> (+ sdkconfig
    <overlay> lines) in a persistent per-config dir, and let ninja decide what to
    rebuild: unchanged inputs = no-op, an edit = only what compiles it. Returns
    {'merged', 'elf', 'dir', 'build_log'} pointing into the build dir (read in place).
    A request whose resolved image another dir already produces is served from that dir.

    Lock order: per-config lock, then image-key lock, then a build slot last; nothing
    waits on a lock while holding a slot.

    `inject` = {dest_rel: abs_src_file}: files staged into the build copy (e.g. a CP app
    image in the host's LittleFS OTA dir); part of the config key."""
    overlay = _drop_defaulted(example_rel, role, target, overlay)
    inject = dict(inject or {})
    if os.environ.get("EH_PLAN"):
        return plan_record(example_rel, role, target, overlay, inject)
    src = _REPO / "examples" / example_rel / role
    if not (src / "CMakeLists.txt").is_file():
        raise FileNotFoundError(f"no such example role: {src}")
    lab.check_idf_identity(idf_identity())
    key = _config_hash(example_rel, role, target, overlay, inject)
    proj = _CACHE / f"{example_rel.replace('../', '').replace('/', '__')}__{role}__{key}"
    common_src = _REPO / "examples" / example_rel / "common"
    has_common = common_src.is_dir()
    # A sibling common/ shared across roles needs a nested scratch (proj/<role>
    # + proj/common) so the role CMake's ../../common resolves in the copy exactly as
    # in-tree; without common/, the role dir IS the scratch (flat).
    workdir = (proj / role) if has_common else proj
    merged = workdir / "build" / "merged_flash.bin"
    build_log = workdir / "build.log"
    hash_file = proj / ".defaults_hash"
    alias = proj / ".alias"
    app = f"{example_rel}/{role}"
    label = (f"{app} [{target}]" + (f" overlay={_shown(overlay)}" if overlay else ""))

    def stage(wd: Path, pj: Path) -> None:
        # Refresh the copy: only changed files are rewritten, so mtimes stay put and
        # ninja sees exactly what changed. Never the overlay: it is per config.
        ignore = scratch.ignore()
        wd.parent.mkdir(parents=True, exist_ok=True)
        scratch.copy_scratch(src, wd, ignore, dirs_exist_ok=True)
        if has_common:
            scratch.copy_scratch(common_src, pj / "common", ignore, dirs_exist_ok=True)
        for dest, srcpath in inject.items():
            _write_if_changed(wd / dest, Path(srcpath).read_bytes())
        # Fixed app version: else IDF stamps `git describe` and every commit relinks every image.
        _write_if_changed(wd / "version.txt", b"eh-test\n")
        scratch.absolutize_overrides(src, wd)
        if _GCOV_SENTINEL in overlay:
            _inject_gcov_instrument(wd / "CMakeLists.txt")

    def key_of(wd: Path) -> str:
        return _image_key(app, target, wd, inject)

    with _flock(proj.name):
        stage(workdir, proj)
        defaults = "sdkconfig.defaults"
        if overlay:
            _write_if_changed(workdir / ".ovl.defaults", ("\n".join(overlay) + "\n").encode())
            defaults = "sdkconfig.defaults;.ovl.defaults"
        dhash = _defaults_hash(workdir)
        has_build = (workdir / "build" / "build.ninja").is_file()
        configured = has_build and hash_file.is_file() and hash_file.read_text().strip() == dhash
        bt = _env_float("EH_BUILD_TIMEOUT", 1800)  # per-firmware build-phase cap
        idf = _idf_path()
        home = Path.home()

        def _run(cmd, logf, append=False, cwd=None):
            script = (
                # Clean IDF env: drop any inherited venv / IDF activation and pin
                # IDF_PATH so export.sh activates the resolved IDF.
                'unset VIRTUAL_ENV IDF_PYTHON_ENV_PATH PYTHONHOME PYTHONPATH; '
                f'export IDF_PATH="{idf}"; '
                f'. "{idf}/export.sh" >/dev/null 2>&1 || exit 90; '
                # ccache shared across build dirs of the same config: base_dir makes
                # include paths relative; IDF's -fmacro-prefix-map embeds the dir.
                f'export IDF_CCACHE_ENABLE=1 CCACHE_BASEDIR="{_CACHE}" CCACHE_NOHASHDIR=1 '
                'CCACHE_IGNOREOPTIONS="-fmacro-prefix-map=*" '
                + " ".join(f'{k}="{v}"' for k, v in caps.ccache_env().items()) + '; '
                f'cd "{cwd or workdir}" || exit 91; {cmd}')
            argv = sandbox.wrap(["bash", "-lc", script],
                                rw_paths=[lab.WORK, caps.ccache_dir(), home / ".espressif",
                                          home / ".cache"],
                                net=True)
            with open(logf, "a" if append else "w") as f:
                return lab.run_with_heartbeat(argv, bt, stdout=f, stderr=subprocess.STDOUT)

        def shared(owner: Path, owner_proj: Path, k: str):
            res = _build_shared(owner, owner_proj, k, stage, key_of, proj, label, _run)
            if res:
                _note(label, f"same image as {owner_proj.name}")
            return res

        if alias.is_file():
            try:
                owner, owner_proj, k, ahash = alias.read_text().split()
            except (OSError, ValueError):
                ahash = None
            if ahash == dhash and (Path(owner) / "build" / "merged_flash.bin").is_file():
                res = shared(Path(owner), Path(owner_proj), k)
                if res:
                    return res
            alias.unlink()

        # No-op check without a build slot: ninja's own dry run.
        if configured and merged.is_file():
            if _run(_NOOP_CHECK, workdir / ".noop.log") == 0:
                lab.touch_last_used(proj)
                _note(label, "up to date")
                return _result(workdir, build_log)

        def compile_(mode, what=None):
            with lab.build_slot():
                lab.ensure_free(f"build {label}")  # F3 disk floor
                slots, pairs = lab.build_capacity(), lab.live_emu_pairs()
                jobs = caps.build_jobs(slots, pairs)
                make = f'ninja -C build -j {jobs} all && idf.py merge-bin -o merged_flash.bin'

                def _attempt(mode):
                    # incremental: ninja only. reconfigure: defaults changed, so regenerate
                    # sdkconfig in place (sdkconfig.h is rewritten only if its content
                    # changes, so only config-dependent files recompile). fresh: first use,
                    # or recovery of a wedged build dir.
                    if mode == "incremental":
                        cmd = make
                    else:
                        (workdir / "sdkconfig").unlink(missing_ok=True)
                        hash_file.unlink(missing_ok=True)
                        if mode == "fresh":
                            shutil.rmtree(workdir / "build", ignore_errors=True)
                            cmd = f'idf.py -D SDKCONFIG_DEFAULTS="{defaults}" set-target {target} && {make}'
                        else:
                            cmd = f'idf.py -D SDKCONFIG_DEFAULTS="{defaults}" reconfigure && {make}'
                    _log.info("[build] %s — %s, jobs=%d (budget %d, %d live emu pairs, %d build slots)…",
                              label, mode, jobs, caps.cpu_budget(), pairs, slots)
                    return _run(cmd, build_log)

                t0 = time.time()
                try:
                    rc = _attempt(mode)
                    if rc != 0 and mode != "fresh":
                        _log.warning("[build] %s — %s build failed; one fresh retry", label, mode)
                        mode = "fresh"
                        rc = _attempt(mode)
                except subprocess.TimeoutExpired:
                    raise RuntimeError(
                        f"build phase exceeded EH_BUILD_TIMEOUT={bt:.0f}s: {label} "
                        f"(raise it or check the build); log: {build_log}")
                if rc != 0:
                    tail = ""
                    try:
                        tail = build_log.read_text(errors="replace")[-2000:]
                    except Exception:
                        pass
                    if lab.is_enospc(tail):
                        # A full disk mid-build is a disk failure, not this cell's.
                        msg = f"ENOSPC during build of {label} — see {build_log}"
                        lab.note_disk_failure(msg)
                        raise lab.DiskExhausted(msg)
                    if "No module named" in tail or "not spawned within an ESP-IDF" in tail:
                        raise RuntimeError(
                            f"ESP-IDF Python env not ready for {label} — idf.py couldn't import "
                            f"its venv packages. Finish the install (./install.sh) or repair "
                            f"tools, then retry. See {build_log}")
                    # Inline the log tail: on CI the container (and build.log) is gone after.
                    raise RuntimeError(f"firmware build failed: {label} — see {build_log}\n"
                                       f"----- build.log tail -----\n{tail.strip() or '(empty)'}\n"
                                       f"----- end build.log -----")
            hash_file.write_text(dhash)
            lab.touch_last_used(proj)
            _register(key_of(workdir), workdir, proj)
            _note(label, f"{what or mode} build, {time.time() - t0:.0f}s")
            return _result(workdir, build_log)

        mode = "incremental" if configured else ("reconfigure" if has_build else "fresh")
        if mode != "fresh":
            return compile_(mode)
        # New dir: configure first (no slot: seconds of CMake, not a build), then
        # either serve the request from a dir already producing that image or build it.
        # The image-key lock makes a concurrent request for the same image wait and share.
        shutil.rmtree(workdir / "build", ignore_errors=True)
        (workdir / "sdkconfig").unlink(missing_ok=True)
        hash_file.unlink(missing_ok=True)
        rc = _run(f'idf.py -D SDKCONFIG_DEFAULTS="{defaults}" set-target {target}', build_log)
        if rc != 0:
            return compile_("fresh")
        k = key_of(workdir)
        with _flock(f"img-{k}"):
            found = _owner_of(k, workdir, key_of)
            if not found:
                return compile_("incremental", "fresh")
            alias.write_text(f"{found[0]} {found[1]} {k} {dhash}\n")
        res = shared(*found, k)
        if res:
            shutil.rmtree(workdir / "build", ignore_errors=True)
            return res
        alias.unlink(missing_ok=True)
        with _flock(f"img-{k}"):
            return compile_("incremental", "fresh")


def _partition(build: Path, label: str):
    """(offset, size) of partition `label` from the built binary partition table."""
    tbl = (build / "partition_table" / "partition-table.bin").read_bytes()
    for i in range(0, len(tbl) - 31, 32):
        if tbl[i:i + 2] != b"\xaa\x50":
            break
        if tbl[i + 12:i + 28].rstrip(b"\0").decode() == label:
            return int.from_bytes(tbl[i + 4:i + 8], "little"), int.from_bytes(tbl[i + 8:i + 12], "little")
    raise KeyError(f"no partition {label!r} in {build}")


def flash_with_fs(fw, part, files, dst_dir):
    """A per-bench copy of fw's flash whose LittleFS partition `part` holds `files`
    ({name: src}), in the format joltwallet/littlefs builds (littlefs-python, test venv)."""
    build = Path(fw["dir"]) / "build"
    off, size = _partition(build, part)
    src = Path(dst_dir) / f"{part}_src"
    shutil.rmtree(src, ignore_errors=True)
    src.mkdir(parents=True)
    for name, f in files.items():
        shutil.copyfile(f, src / name)
    img = Path(dst_dir) / f"{part}.bin"
    subprocess.run([str(Path(sys.executable).parent / "littlefs-python"), "create",
                    str(src), str(img), f"--fs-size={size}", "--name-max=64",
                    "--block-size=4096"], check=True, capture_output=True)
    out = Path(dst_dir) / "host_flash.bin"
    shutil.copyfile(fw["merged"], out)
    with open(out, "r+b") as f:
        end = f.seek(0, os.SEEK_END)
        if end < off:  # merged image stops at its last flashed region; pad as erased flash
            f.write(b"\xff" * (off - end))
        f.seek(off)
        f.write(img.read_bytes())
    return str(out)


def stash_logs(dest_dir, **named) -> None:
    """Copy each build result's build.log into dest_dir as <name>_build.log, so a
    test's build + cp + host logs co-locate in the run report dir for drill-down."""
    for name, fw in named.items():
        bl = (fw or {}).get("build_log")
        if bl and os.path.exists(bl):
            try:
                shutil.copyfile(bl, os.path.join(str(dest_dir), f"{name}_build.log"))
            except OSError:
                pass


def prebuild(specs) -> None:
    """Speculative prebuild-ahead: build several (example, role, target, overlay)
    specs concurrently before tests fan out, so the first run overlaps builds
    instead of stalling per-suite. Each build is lock-guarded, so this is safe
    even when xdist workers call it at once. Failures are swallowed — the lazy
    build in the bench fixture is the fallback."""
    from concurrent.futures import ThreadPoolExecutor
    specs = list(specs)
    if not specs:
        return
    workers = min(len(specs), caps.build_pool())  # one CPU-bound sizing authority
    with ThreadPoolExecutor(max_workers=workers) as ex:
        futs = [ex.submit(build, *s) for s in specs]
        for f in futs:
            try:
                f.result()
            except Exception:
                pass


def slave_overlay(cp_target):
    """Bench-derived sdkconfig overlay that tells the host which CP chip it
    drives, so esp_wifi_remote exports the right slave SoC caps (e.g.
    SLAVE_SOC_WIFI_SUPPORT_5G for a C5). The chip is a bench property (the CP the
    bench actually has), injected as an overlay — NOT pinned per example. Applied
    over the example's neutral default; the choice's later value wins."""
    if not cp_target:
        return []
    return [f"CONFIG_SLAVE_IDF_TARGET_{cp_target.upper()}=y"]


# --- on-target gcov (app_trace GCOV over USB-Serial-JTAG) --------------------
# A comment line (harmless in sdkconfig.defaults) that rides in the overlay so it
# folds into the config hash → the instrumented firmware gets its OWN scratch dir
# and never clobbers the normal build; build() detects it to inject --coverage.
_GCOV_SENTINEL = "# EH_GCOV_INSTRUMENT"
_GCOV_MARK = "EH gcov instrumentation"
# Instrument ONLY ESP-Hosted components (eh_*, msg_codec, main) — whole-project
# --coverage overflows DRAM by ~700KB (gcov counters for all of IDF/WiFi/mbedTLS
# land in .dram0.data). Third-party/IDF stay uninstrumented; the aggregator
# filters generated files (gen_v2.c) out anyway. Appended AFTER project() because
# that's when the component lib targets exist.
_GCOV_CMAKE = (
    f"\n# --- {_GCOV_MARK} (scratch copy only) ---\n"
    "idf_build_get_property(_eh_cov_comps BUILD_COMPONENTS)\n"
    "foreach(_eh_c IN LISTS _eh_cov_comps)\n"
    '  if(_eh_c MATCHES "^eh_" OR _eh_c STREQUAL "msg_codec" OR _eh_c STREQUAL "main")\n'
    "    idf_component_get_property(_eh_lib ${_eh_c} COMPONENT_LIB)\n"
    "    if(_eh_lib)\n"
    "      target_compile_options(${_eh_lib} PRIVATE --coverage)\n"
    "      target_link_libraries(${_eh_lib} INTERFACE --coverage)\n"
    "    endif()\n"
    "  endif()\n"
    "endforeach()\n\n")


def gcov_overlay():
    """Overlay that turns a CP/MCU firmware into a coverage build: enables the
    app_trace GCOV path (dumped later by OpenOCD `esp gcov` over USB-Serial-JTAG)
    and flags build() to whole-project-instrument the scratch copy. Union the
    resulting .gcda with the Linux-host gcov for one full-codebase number."""
    return [_GCOV_SENTINEL,
            "CONFIG_APPTRACE_ENABLE=y",
            "CONFIG_APPTRACE_DEST_JTAG=y",
            "CONFIG_APPTRACE_GCOV_ENABLE=y",
            "CONFIG_APPTRACE_LOCK_ENABLE=y"]


def _inject_gcov_instrument(cml):
    """Append per-component --coverage AFTER project() in the scratch CMakeLists
    (component lib targets only exist post-project()). Idempotent; only ever
    touches the COW copy, never the real example."""
    if not cml.is_file():
        return
    txt = cml.read_text()
    if _GCOV_MARK in txt:
        return
    cml.write_text(txt.rstrip() + "\n" + _GCOV_CMAKE)


def build_linux_host(example_rel: str, role: str, overlay=None) -> dict:
    with lab.test_clock_paused():
        return _build_linux_host(example_rel, role, overlay)


def _build_linux_host(example_rel: str, role: str, overlay=None) -> dict:
    """Build (or reuse) a Linux userspace host binary (posix target) OUT-OF-TREE.

    The example is a read-only input: it's COW-copied to a per-config scratch
    (config-hash keyed, sdkconfig excluded from the copy) so the generated
    sdkconfig lives in OUR workspace, never inside the example — no stale in-tree
    sdkconfig to fight the layered defaults. Optional ordered <overlay> sdkconfig
    lines (bench/suite/case layers) apply on top via EH_SDKCONFIG_OVERLAY. Same
    (example, role, overlay) + unchanged sources => cache hit, no rebuild.

    Mirrors build() (the ESP path) but drives the posix hosted build via eh.py."""
    if os.environ.get("EH_PLAN"):
        return plan_record(example_rel, role, "linux", overlay)
    overlay = list(overlay or [])
    target = "posix"
    src = _REPO / "examples" / example_rel / role
    if not (src / "CMakeLists.txt").is_file():
        raise FileNotFoundError(f"no such linux host: {src}")
    key = _config_hash(example_rel, role, target, overlay)
    proj = _CACHE / f"{example_rel.replace('/', '__')}__{role}__{key}"
    common_src = _REPO / "examples" / example_rel / "common"
    has_common = common_src.is_dir()
    workdir = (proj / role) if has_common else proj
    srcfp_file, cfgfp_file = proj / ".srcfp", proj / ".cfgfp"
    src_fp, cfg_fp = _fingerprints(example_rel, role, target, overlay)

    def _bin():
        if not workdir.exists():
            return None
        for p in sorted(workdir.glob("build/**/*")):
            if p.is_file() and os.access(p, os.X_OK) and (
                    "c_app" in p.name or "py_app" in p.name):
                return p
        return None

    def _hit():
        b = _bin()
        if (b and srcfp_file.is_file() and cfgfp_file.is_file()
                and srcfp_file.read_text().strip() == src_fp
                and cfgfp_file.read_text().strip() == cfg_fp):
            try:
                lab.touch_last_used(proj)
            except OSError:
                pass
            return {"merged": None, "elf": None, "bin": str(b),
                    "dir": str(workdir), "build_log": str(workdir / "build.log")}
        return None

    hit = _hit()
    if hit:
        _log.info("[build-linux] %s/%s%s — cache hit", example_rel, role,
                  f" overlay={_shown(overlay)}" if overlay else "")
        return hit

    locks = _WORK / ".locks"
    locks.mkdir(parents=True, exist_ok=True)
    # F4: global build-concurrency slot, taken BEFORE the per-config lock (reverse
    # order deadlocks). Only a real build takes one — a cache hit returned above pays
    # nothing. Bounds build memory across prewarm AND the run phase.
    with lab.build_slot(), open(locks / f"{proj.name}.lock", "w") as lk:
        lab.flock_with_heartbeat(lk)
        hit = _hit()
        if hit:
            return hit
        # Exclude sdkconfig* + runtime artifacts from the copy → the scratch's
        # sdkconfig is generated fresh from defaults+overlay (never inherited),
        # and root-owned run leftovers (.nvs from a sudo'd host, staged bin/)
        # don't break the copy or pollute the workspace.
        ignore = scratch.ignore(".nvs", "bin", ".eh.target")
        fresh = not workdir.exists()
        if fresh:
            workdir.parent.mkdir(parents=True, exist_ok=True)
            scratch.copy_scratch(src, workdir, ignore)
        else:
            scratch.copy_scratch(src, workdir, ignore, dirs_exist_ok=True)
        if has_common:
            scratch.copy_scratch(common_src, proj / "common", ignore, dirs_exist_ok=True)
        scratch.absolutize_overrides(src, workdir)
        env_overlay = ""
        if overlay:
            ovl = workdir / ".ovl.defaults"
            ovl.write_text("\n".join(overlay) + "\n")
            env_overlay = f'export EH_SDKCONFIG_OVERLAY="{ovl}"; '
        build_log = workdir / "build.log"
        idf = _idf_path_opt()  # optional: posix host builds with the system toolchain
        idf_src = f'. "{idf}/export.sh" >/dev/null 2>&1; ' if idf else ''
        script = (
            f'export EH_PATH="{_REPO}"; '
            f'export EH_CMAKE_JOBS={caps.build_jobs(lab.build_capacity(), lab.live_emu_pairs())}; '
            f'{idf_src}'
            f'{env_overlay}'
            f'cd "{workdir}" || exit 91; '
            f'python3 "{_EH}" set-target posix && python3 "{_EH}" build'
        )
        def _run_linux_build(clean):
            if clean:  # drop a wedged build/ so set-target reconfigures from scratch
                shutil.rmtree(workdir / "build", ignore_errors=True)
            _log.info("[build-linux] %s/%s%s — %s%s…", example_rel, role,
                      f" overlay={_shown(overlay)}" if overlay else "",
                      "clean rebuild" if clean else "building",
                      " (fresh)" if fresh else "")
            with open(build_log, "w") as blf:
                return subprocess.run(["bash", "-lc", script], stdout=blf,
                                      stderr=subprocess.STDOUT).returncode

        lab.ensure_free(f"linux build {example_rel}/{role}")  # F3 disk floor
        rc = _run_linux_build(clean=False)
        if rc != 0:
            # One clean-rebuild retry (same policy as the ESP build path): a wedged
            # scratch self-heals; a real code/config break fails again → terminate.
            _log.warning("[build-linux] %s/%s build failed — one clean-rebuild retry",
                         example_rel, role)
            rc = _run_linux_build(clean=True)
        if rc != 0:
            try:
                ltail = build_log.read_text(errors="replace")[-2000:]
            except OSError:
                ltail = ""
            if lab.is_enospc(ltail):
                msg = f"ENOSPC during linux build of {example_rel}/{role} — see {build_log}"
                lab.note_disk_failure(msg)
                raise lab.DiskExhausted(msg)
            raise RuntimeError(f"linux host build failed (clean-rebuild retry also failed): "
                               f"{example_rel}/{role} — see {build_log}")
        b = _bin()
        if not b:
            raise RuntimeError(f"linux host build produced no binary: {workdir}")
        srcfp_file.write_text(src_fp)
        cfgfp_file.write_text(cfg_fp)
        lab.touch_last_used(proj)
        return {"bin": str(b), "dir": str(workdir), "build_log": str(build_log)}


# COW level 2 lives in the shared scratch module; re-exported so existing callers
# (targets/emu.py, targets/linux.py) keep using build_fw.cow_flash unchanged.
cow_flash = scratch.cow_flash
