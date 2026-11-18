#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#
# Gate: coprocessor and host sources read Kconfig values only through the
# master config headers (EH_CP_* / EH_HOST_*; on the CP, eh_cp_master_config.h
# and the eh_cp_cfg_*.h parts it includes), never as raw CONFIG_ESP_HOSTED_*,
# CONFIG_BT_* or CONFIG_BTDM_*. A feature-private header is not a redirector.
# CONFIG_IDF_TARGET_* and SOC_* are not checked yet.
#
# Gate: host and port sources that use a macro defined in
# eh_host_port_master_config.h include that header themselves. It is not
# force-included, and an undefined macro in #if reads as 0 without a warning.
# .inc fragments are exempt; the file that includes them carries the include.
#
# Usage: check_config_redirect.py [FILE ...]
#   With files (pre-commit), only those files are checked.
#   Without files, the whole scope is scanned. Older code still has
#   violations, so the hook checks changed files only.

import fnmatch
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ROOTS = ("coprocessor", "host")
EXTS = (".c", ".h", ".cpp", ".hpp")

# Redirector headers, and trees that use a different config system.
ALLOW = (
    "coprocessor/eh_cp_core/include/eh_cp_master_config.h",
    "coprocessor/eh_cp_core/include/eh_cp_cfg_*.h",
    "coprocessor/eh_cp_core/include/eh_cp_idf_compat.h",
    "host/eh_host_config/include/eh_host_port_master_config.h",
    "host/linux/eh_host_linux_kmod/*",
    "*/build/*",
    "*/managed_components/*",
)

HOST_CFG = "host/eh_host_config/include/eh_host_port_master_config.h"
# Apps (examples/) get these macros through the public esp_hosted.h / eh_host.h.
INC_ROOTS = ("host", "port")
INC_LINE = re.compile(r'^\s*#\s*include\s*[<"]eh_host_port_master_config\.h[>"]', re.M)

RAW = re.compile(r"\bCONFIG_(?:ESP_HOSTED|BT|BTDM)_[A-Z0-9_]+")
# Comments and string/char literals; a mention there is not a read.
NOISE = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'',
    re.DOTALL,
)


def in_scope(rel):
    return (rel.startswith(tuple(r + "/" for r in ROOTS))
            and rel.endswith(EXTS)
            and not any(fnmatch.fnmatch(rel, p) for p in ALLOW))


def in_inc_scope(rel):
    return (rel.startswith(tuple(r + "/" for r in INC_ROOTS))
            and rel.endswith(EXTS)
            and not any(fnmatch.fnmatch(rel, p) for p in ALLOW))


def host_cfg_macros():
    text = (REPO / HOST_CFG).read_text()
    names = set(re.findall(r"^\s*#\s*define\s+([A-Za-z_]\w*)", text, re.M))
    names -= set(re.findall(r"^\s*#\s*ifndef\s+(\w+)\s*\n\s*#\s*define\s+\1\b",
                            text, re.M)[:1])
    return re.compile(r"\b(" + "|".join(sorted(names, key=len, reverse=True)) + r")\b")


def check_include(path, rel, uses):
    raw = path.read_text(errors="replace")
    if INC_LINE.search(raw):
        return []
    text = NOISE.sub(blank, raw)
    for no, line in enumerate(text.splitlines(), 1):
        m = uses.search(line)
        if m:
            return [f"{rel}:{no}: {m.group(1)}"]
    return []


def blank(match):
    return re.sub(r"[^\n]", " ", match.group(0))


def check(path, rel):
    text = NOISE.sub(blank, path.read_text(errors="replace"))
    bad = []
    for no, line in enumerate(text.splitlines(), 1):
        for sym in RAW.findall(line):
            bad.append(f"{rel}:{no}: {sym}")
    return bad


def main(argv):
    if argv:
        paths = [Path(a).resolve() for a in argv]
    else:
        paths = [p for r in sorted(set(ROOTS + INC_ROOTS)) for p in (REPO / r).rglob("*")]
    bad, missing = [], []
    uses = host_cfg_macros()
    for p in paths:
        try:
            rel = p.relative_to(REPO).as_posix()
        except ValueError:
            continue
        if p.is_file() and in_scope(rel):
            bad += check(p, rel)
        if p.is_file() and rel != HOST_CFG and in_inc_scope(rel):
            missing += check_include(p, rel, uses)
    if bad:
        print("Raw CONFIG_ESP_HOSTED_* / CONFIG_BT_* / CONFIG_BTDM_* outside a master "
              "config header. Add an EH_CP_* / EH_HOST_* alias to "
              "eh_cp_master_config.h or eh_host_port_master_config.h and use that:")
        print("\n".join(bad))
    if missing:
        print('Uses a macro from eh_host_port_master_config.h without including it. '
              'Add #include "eh_host_port_master_config.h" before the first include:')
        print("\n".join(missing))
    return 1 if bad or missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
