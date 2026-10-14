# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Per-test log archiver — freezes each run's CP/host/build logs into the record.

The bench fixtures tee each emulator/host subprocess to `lab_tmp/*.log` (the
per-test scratch under the run dir). At teardown this copies them into the frozen
record `runs/<ts>/logs/<test>/` (`.FAIL`-suffixed on failure) next to `live.log`,
so cp/host/build logs sit together for whoever reads the error.

Huge logs are head+tail capped (a wedged CP can spew 100s of MB of EMFILE spam) so
the record stays readable. Full logs remain in the scratch dir until the run dir
is cleaned.
"""
import logging
import os
import shutil
from pathlib import Path

import pytest

from infra import lab

# Cap policy + the capped copy live in `lab`: the controller's killed-worker harvest
# (lab.harvest_killed_scratch) must freeze logs identically to this teardown.
_CAP_LINES = lab.CAP_LINES
_CAP_BYTES = lab.CAP_BYTES


def pytest_configure(config):
    # Mirror the 'eh' step trace ([build]/[emu]/[expect]/[cmd]) into the shared
    # live.log from EVERY process — controller AND each xdist worker. Without this
    # the trace runs on workers and never reaches live.log, so `tail -f live.log`
    # goes blind during builds under --jobs. O_APPEND single-line writes stay
    # atomic across processes; the reporter opens the same file in append mode.
    run_dir = os.environ.get("EH_RUN_DIR")
    if not run_dir:
        return
    live = Path(run_dir) / "live.log"
    eh = logging.getLogger("eh")
    if any(getattr(h, "_eh_live", False) for h in eh.handlers):
        return
    try:
        Path(run_dir).mkdir(parents=True, exist_ok=True)
        h = logging.FileHandler(live, mode="a")
    except OSError:
        return
    h.setFormatter(logging.Formatter("%(asctime)s  %(message)s", "%H:%M:%S"))
    h.setLevel(logging.INFO)
    h._eh_live = True
    eh.addHandler(h)
    eh.setLevel(logging.INFO)


_copy_capped = lab.copy_capped


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    # Stash each phase's report so the fixture can tell pass from fail at teardown.
    rep = (yield).get_result()
    setattr(item, f"_eh_rep_{rep.when}", rep)


@pytest.fixture(autouse=True)
def _eh_logcap(request, lab_tmp):
    """Archive this test's bench logs after it runs. Autouse + lab_tmp so it
    shares the exact scratch dir the bench fixtures write their logs into."""
    yield
    if not os.environ.get("EH_RUN_DIR"):
        return
    setup = getattr(request.node, "_eh_rep_setup", None)
    call = getattr(request.node, "_eh_rep_call", None)
    failed = bool((setup and setup.failed) or (call and call.failed))
    logs = sorted(Path(lab_tmp).glob("*.log"))
    if logs:
        dest = lab.test_logs_dir(request.node.nodeid, failed)
        try:
            dest.mkdir(parents=True, exist_ok=True)
            for lg in logs:
                _copy_capped(lg, dest / lg.name)
        except OSError:
            pass
    # Selective retention (R8): a PASS keeps only the capped record in logs/ — purge the
    # raw per-test scratch (raw logs + COW flash = real bytes on a non-CoW fs) NOW, not
    # at age-expiry. A FAIL keeps its scratch for debugging; the age-sweep is the backstop.
    if not failed:
        shutil.rmtree(lab_tmp, ignore_errors=True)
