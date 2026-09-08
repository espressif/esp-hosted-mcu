#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#
# CI gate: coprocessor and common sources must not use a printf/scanf
# conversion that newlib-nano cannot parse.
#
# Nano's length-modifier table is the three characters "hlL". It drops the C99
# additions (z, j, t, hh) and all 64-bit conversions. A dropped modifier is not
# cosmetic: nano does not consume the argument, so every later argument shifts
# and a following "%s" dereferences an integer. ESP_LOGx hides this from
# -Wformat, because the format string reaches the compiler through a macro.
#
# IDF 5.5.x and earlier enable nano by default on ESP32-C2, where printf comes
# from ROM and cannot be replaced. IDF 6.x defaults that chip to picolibc.
#
# Use "%u" or "%d" with a cast. Print a 64-bit value as two 32-bit halves.

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ROOTS = ("coprocessor", "common")
SKIP = ("build", "managed_components")

# %<flags><width>.<prec><modifier><conv>, plus the PRI*64 macro spellings.
FORBIDDEN = re.compile(
    r"""%[-+ #0-9.]*(?:z|j|t|hh|ll)[udixXo]     # C99 modifiers and 64-bit
      | %[-+ #0-9.]*[aA](?![a-zA-Z])            # C99 hex float
      | \bPRI[udixX]?(?:64|LEAST64|FAST64)\b    # <inttypes.h> 64-bit macros
    """,
    re.VERBOSE,
)


def sources():
    for root in ROOTS:
        for path in sorted((REPO / root).rglob("*")):
            if path.suffix not in (".c", ".h"):
                continue
            if any(part in SKIP for part in path.parts):
                continue
            yield path


def main():
    hits = []
    for path in sources():
        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            if FORBIDDEN.search(line):
                hits.append(f"{path.relative_to(REPO)}:{lineno}: {line.strip()}")

    if hits:
        print("\n".join(hits))
        print()
        print(f"{len(hits)} line(s) use a conversion that newlib-nano cannot parse.")
        print("A dropped modifier shifts every later argument, so a following %s")
        print("dereferences an integer.")
        print("Use %u or %d with a cast. Print a 64-bit value as two 32-bit halves.")
        return 1

    print("No nano-unsafe printf conversions in %s." % ", ".join(ROOTS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
