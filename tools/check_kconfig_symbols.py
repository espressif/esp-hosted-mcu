#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Dead-gate guard for the ESP_HOSTED_* / EH_* Kconfig namespace.

Kconfig silently evaluates an UNDEFINED symbol as false, so a `depends on` / `select` /
`if` (Kconfig) or `if(CONFIG_...)` (CMake) that names a symbol no `config`/`menuconfig`
defines does not error — it silently makes the gated menu / feature / build permanently
dead. Renames leave exactly these dangling gates with no build failure and no test able
to catch them (e.g. an example menu that can never be selected, or a feature whose
auto-init is never linked).

Scope is deliberately narrow so every hit is a real bug, not noise: only the two
constructs that have NO legitimate "optional fallback" semantics —
  * Kconfig  `depends on` / `select` / `imply` / `if` / `visible if`
  * CMake    `if(CONFIG_...)` / `elseif(CONFIG_...)`
gating on an undefined symbol in our namespace. C-preprocessor `#ifdef CONFIG_X ...
#else ...` is intentionally NOT scanned: that is the portable master-config idiom (a
symbol absent on one port falls back to a default), indistinguishable from a rename at
this layer — those belong in a human review, not a mechanical gate. IDF/component
symbols are out of scope (never our namespace); cross-port PORT_TYPE_* guards (defined
in other ports' Kconfig) are excluded.

  python3 tools/check_kconfig_symbols.py           # report (exit 0 always)
  python3 tools/check_kconfig_symbols.py --strict  # exit 1 if any dead gate
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP = {".deps", ".git", "build", "managed_components", ".work", "__pycache__",
        "build_ped", "node_modules"}
NS = ("ESP_HOSTED_", "EH_")
DEF_RE = re.compile(r"^\s*(?:menu)?config\s+([A-Za-z0-9_]+)")
IDENT = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]+)\b")
# Kconfig LOGIC lines only (not `default`/`range`, whose RHS can be a symbol VALUE).
KLOGIC = re.compile(r"^\s*(depends on|select|imply|visible if)\b|(^\s*if\s)|(\sif\s)")
CMAKE_IF = re.compile(r"^\s*(?:if|elseif)\s*\(")
CREF = re.compile(r"CONFIG_((?:ESP_HOSTED_|EH_)[A-Za-z0-9_]+)")
_KEYWORDS = {"if", "on", "depends", "select", "imply", "visible", "y", "n", "m"}
_CROSS_PORT = re.compile(r"HOST_PORT_TYPE_")
# Known dead gates, reported but not failed: (file, symbol) -> why it is left as is.
KNOWN = {
    ("examples/network_split/iperf/mcu_host/main/Kconfig.projbuild", "ESP_HOSTED_CP_EXT_NW_SPLIT"):
        "MQTT/HTTP client menu, deferred",
}


def _ns(sym):
    return any(sym.startswith(p) for p in NS)


def _walk(pred):
    for dp, dns, fns in os.walk(ROOT):
        dns[:] = [d for d in dns if d not in SKIP]
        for fn in fns:
            if pred(fn):
                yield os.path.join(dp, fn)


def _defined():
    out = set()
    for f in _walk(lambda fn: fn.startswith("Kconfig")):
        try:
            for ln in open(f, errors="replace"):
                m = DEF_RE.match(ln)
                if m:
                    out.add(m.group(1))
        except OSError:
            pass
    return out


def main(argv):
    strict = "--strict" in argv
    defined = _defined()
    viol = []   # (relpath, lineno, symbol, context, kind)

    # 1) Kconfig logic gates
    for f in _walk(lambda fn: fn.startswith("Kconfig")):
        try:
            lines = open(f, errors="replace").read().splitlines()
        except OSError:
            continue
        for i, ln in enumerate(lines, 1):
            s = ln.strip()
            if DEF_RE.match(ln) or s.startswith(("help", "#")) or not s:
                continue
            if not KLOGIC.search(ln):
                continue
            for sym in IDENT.findall(ln.split("#", 1)[0]):
                if sym in _KEYWORDS or _CROSS_PORT.search(sym):
                    continue
                if _ns(sym) and sym not in defined:
                    viol.append((os.path.relpath(f, ROOT), i, sym, s[:72], "Kconfig"))

    # 2) CMake if(CONFIG_...) gates
    for f in _walk(lambda fn: fn == "CMakeLists.txt" or fn.endswith(".cmake")):
        try:
            lines = open(f, errors="replace").read().splitlines()
        except OSError:
            continue
        for i, ln in enumerate(lines, 1):
            if not CMAKE_IF.match(ln):
                continue
            for sym in CREF.findall(ln):
                if _CROSS_PORT.search(sym):
                    continue
                if sym not in defined:
                    viol.append((os.path.relpath(f, ROOT), i, sym, ln.strip()[:72], "CMake"))

    known = [v for v in viol if (v[0], v[2]) in KNOWN]
    viol = [v for v in viol if (v[0], v[2]) not in KNOWN]
    print("== dead gates: ESP_HOSTED_/EH_ symbol gated on but defined nowhere ==")
    for f, i, sym, ctx, kind in viol:
        print(f"  [{kind}] {sym}\n      {f}:{i}   {ctx}")
    for f, i, sym, ctx, kind in known:
        print(f"  [{kind}, known: {KNOWN[(f, sym)]}] {sym}\n      {f}:{i}   {ctx}")
    n = len({v[2] for v in viol})
    print(f"\n{n} dead-gate symbol(s) across {len(viol)} site(s).")
    if strict and viol:
        print("FAIL (--strict): a gate references a symbol no Kconfig defines — it is "
              "permanently dead. Repoint it to the real symbol or remove it.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
