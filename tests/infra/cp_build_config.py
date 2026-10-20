"""Read back the sdkconfig the bench actually built for the CP.

Kconfig drops an overlay line silently when a select or a choice overrides it,
so a test that depends on a config value checks the built one."""
import os

import pytest


def built_cp_config(bench, key):
    """Value of `key` in the CP build's sdkconfig, or None when unset."""
    elf = (bench.get('artifacts') or {}).get('cp_elf')
    if not elf:
        pytest.fail('bench exposed no cp_elf, cannot read the built config')
    d = os.path.dirname(os.path.abspath(elf))
    for _ in range(5):
        f = os.path.join(d, 'sdkconfig')
        if os.path.isfile(f):
            for line in open(f):
                if line.startswith(key + '='):
                    return line.strip().split('=', 1)[1]
            return None
        d = os.path.dirname(d)
    pytest.fail(f'no sdkconfig found above {elf}')
