#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#
# CI gate: files kept as identical copies (host and CP sides cannot share an
# include path) must have the same MD5.
#
# tools/mirrored_files.txt lists them: one group per line, two or more
# repo-relative paths separated by spaces. `#` starts a comment.

import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIST = ROOT / 'tools' / 'mirrored_files.txt'


def _md5(path):
    return hashlib.md5(path.read_bytes()).hexdigest()


def main():
    errors = 0
    groups = 0
    for n, line in enumerate(LIST.read_text().splitlines(), 1):
        paths = line.split('#', 1)[0].split()
        if not paths:
            continue
        where = f'{LIST.relative_to(ROOT)}:{n}'
        if len(paths) < 2:
            print(f'{where}: a group needs two or more paths')
            errors += 1
            continue
        groups += 1
        sums = {}
        for p in paths:
            f = ROOT / p
            if not f.is_file():
                print(f'{where}: missing file {p}')
                errors += 1
                continue
            sums[p] = _md5(f)
        if len(set(sums.values())) > 1:
            print(f'{where}: copies differ; make them identical:')
            for p, s in sums.items():
                print(f'  {s}  {p}')
            errors += 1
    if errors:
        return 1
    print(f'mirrored files: {groups} group(s) identical')
    return 0


if __name__ == '__main__':
    sys.exit(main())
