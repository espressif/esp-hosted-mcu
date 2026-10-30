# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""eh_fsm.h engine unit test: built with the host C compiler, no target needed."""
import os
import shutil
import subprocess

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
INC = os.path.join(HERE, '..', '..', '..', 'common', 'eh_common', 'include')


@pytest.mark.sanity
def test_eh_fsm_engine(lab_tmp):
    cc = shutil.which('cc') or shutil.which('gcc')
    if not cc:
        pytest.skip('no host C compiler')
    exe = os.path.join(str(lab_tmp), 'test_eh_fsm')
    subprocess.run([cc, '-std=c99', '-Wall', '-Werror', '-I', os.path.join(HERE, 'stub'),
                    '-I', INC, os.path.join(HERE, 'test_eh_fsm.c'), '-o', exe], check=True)
    r = subprocess.run([exe], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    assert 'all passed' in r.stdout
