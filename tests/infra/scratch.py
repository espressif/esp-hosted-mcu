# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Build-scratch primitives shared by BOTH builders (tests/infra/build_fw.py, the
pytest path, and tools/esplab/runner.py, the CLI/HTTP path).

Neutral: no pytest, no CLI, no HTTP. Kept as ONE authority so a fix here lands on
every build path at once — the F1 snowball fix once had to be applied twice
because build_fw reached into runner.py backwards via importlib instead of both
importing a common module. That inversion is gone: both import this.
"""
import hashlib
import os
import re
import shutil
import subprocess
from pathlib import Path

from infra import lab  # disk floor + ENOSPC classification (no cycle: lab imports no infra)

# One authoritative "not a build input" copy-exclusion list. build/managed_components
# are regenerated; dependencies.lock/sdkconfig* are generated fresh from
# defaults+overlay in the scratch (never inherited); *.bin are artifacts.
_IGNORE_BASE = ("build", "managed_components", "dependencies.lock",
                "sdkconfig", "sdkconfig.old", "*.bin")


def ignore(*extra):
    """shutil ignore-patterns for a scratch copy: the base exclusions plus any
    per-purpose extras (e.g. the linux-host path adds .nvs/bin/.eh.target)."""
    return shutil.ignore_patterns(*_IGNORE_BASE, *extra)


def copy_scratch(src, dst, ign, dirs_exist_ok=False):
    """copytree that never DEREFERENCES a symlink into a real copy. Examples carry a
    `components/esp_hosted -> <repo root>` symlink; copytree's default (symlinks=False)
    follows it and copies the WHOLE repo — including tests/.work (every prior build
    cache) — into each build, snowballing to tens of GB and blowing the build budget.
    Skip symlinks during the copy, then recreate each pointing at its ORIGINAL absolute
    target so it still resolves from the deeper scratch dir (os.walk followlinks=False
    finds them without descending into the symlinked repo)."""
    src, dst = Path(src), Path(dst)

    def _ign(d, names):
        skip = set(ign(d, names))
        skip.update(n for n in names if os.path.islink(os.path.join(d, n)))
        return skip

    shutil.copytree(src, dst, ignore=_ign, dirs_exist_ok=dirs_exist_ok)
    for dirpath, dirnames, filenames in os.walk(src, followlinks=False):
        for n in dirnames + filenames:
            sp = os.path.join(dirpath, n)
            if os.path.islink(sp):
                out = dst / os.path.relpath(sp, src)
                out.parent.mkdir(parents=True, exist_ok=True)
                if out.is_symlink() or out.exists():
                    out.unlink()
                os.symlink(os.path.realpath(sp), out)


_OVERRIDE_RE = re.compile(
    r'^([ \t]*)override_path:[ \t]*([\'"]?)([^\'"\n]+)\2[ \t]*\n', re.M)


def absolutize_overrides(src, proj):
    """Fix `override_path:` in the scratch copy's idf_component.yml files.

    A dep whose target is ALSO provided as a local `components/<name>` symlink (created
    absolute by copy_scratch) has its `override_path` line DROPPED: the
    component-manager COPIES an override_path that points outside the project — and when
    that target is the repo root it drags the whole tree (incl. tests/.work) into every
    build, snowballing to tens of GB — whereas a local `components/<name>` is used IN
    PLACE. Deps with no local symlink keep an ABSOLUTIZED override_path (resolved against
    the ORIGINAL example location) so they still resolve from the scratch's depth."""
    src, proj = Path(src), Path(proj)
    local = set()
    comps = proj / 'components'
    if comps.is_dir():
        for c in comps.iterdir():
            if c.is_symlink():
                try:
                    local.add(os.path.realpath(c))
                except OSError:
                    pass
    for yml_src in src.rglob('idf_component.yml'):
        yml_dst = proj / yml_src.relative_to(src)
        if not yml_dst.is_file():
            continue

        def fix(m):
            val = m.group(3).strip()
            if not val:
                return m.group(0)
            target = val if os.path.isabs(val) else str((yml_src.parent / val).resolve())
            target = os.path.realpath(target)
            if target in local:
                return ''  # provided in place by a components/<name> symlink → drop
            return f'{m.group(1)}override_path: "{target}"\n'

        text = yml_dst.read_text()
        new = _OVERRIDE_RE.sub(fix, text)
        if new != text:
            yml_dst.write_text(new)


def cow_flash(merged, dst_dir):
    """Per-run writable flash image (COW level 2). reflink where the fs supports it
    (btrfs/xfs: instant, zero extra disk), else a plain copy. The cached base is
    never written to, so runs are isolated and reproducible. The dest name is
    derived from the source so distinct images (e.g. cp vs host) never collide in
    the same dir."""
    src = Path(merged)
    # F3 disk floor: reflink is a btrfs/xfs fast-path — on ext4 `cp --reflink=auto`
    # silently degrades to a full physical copy, so this IS a byte consumer. The
    # 2026-10-04 run died exactly here (OSError ENOSPC in copyfile).
    lab.ensure_free(f"flash copy {src.name}")
    Path(dst_dir).mkdir(parents=True, exist_ok=True)
    tag = hashlib.sha1(str(src.resolve()).encode()).hexdigest()[:8]
    dst = Path(dst_dir) / f"flash_{tag}.bin"
    # reflink is a Linux (btrfs/xfs) fast-path; `cp --reflink` doesn't exist on
    # macOS `cp` (errors out). Try it, fall back to a portable copy everywhere else.
    try:
        subprocess.run(["cp", "--reflink=auto", merged, str(dst)],
                       check=True, capture_output=True)
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        err = getattr(e, "stderr", b"") or b""
        if lab.is_enospc(err.decode(errors="replace") if isinstance(err, bytes) else f"{err}"):
            msg = f"ENOSPC copying flash image {src.name} → {dst_dir}"
            lab.note_disk_failure(msg)
            raise lab.DiskExhausted(msg)
        try:
            shutil.copyfile(merged, str(dst))
        except OSError as oe:
            if not lab.is_enospc(oe):
                raise
            msg = f"ENOSPC copying flash image {src.name} → {dst_dir}"
            lab.note_disk_failure(msg)
            raise lab.DiskExhausted(msg) from oe
    return str(dst)
