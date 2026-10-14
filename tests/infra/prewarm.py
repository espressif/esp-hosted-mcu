# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Prewarm: build each firmware a run needs once, before any test starts.

Input is the plan record (EH_PLAN) of the same test selection. Requests are
deduplicated and built through build_fw, so the build slots, image sharing and
ninja's own up-to-date check apply exactly as in the run. A request whose inject
source is another image's output (unknown until that build) is left to the test.

    python -m infra.prewarm <plan.jsonl>
"""
import concurrent.futures
import json
import os
import sys
import time

from infra import build_fw

# Up-to-date checks are I/O-bound; real builds are gated by lab's build slots.
_WORKERS = int(os.environ.get("EH_PREWARM_WORKERS") or 8)


def requests(path):
    """Unique buildable requests from a plan record, in first-seen order."""
    seen, out = set(), []
    with open(path) as f:
        for line in f:
            r = json.loads(line)
            # Not an example (e.g. the IDF ot_cli peer), or inject built from another
            # image's output: the test builds it, outside its own clock.
            if r["example"].startswith("idf:") or any(
                    not os.path.isfile(s) for s in r["inject_src"].values()):
                continue
            k = (r["example"], r["role"], r["target"], tuple(r["overlay"]),
                 tuple(sorted(r["inject_src"].items())))
            if k not in seen:
                seen.add(k)
                out.append(r)
    return out


def _build(r):
    if r["target"] == "linux":
        return build_fw.build_linux_host(r["example"], r["role"], r["overlay"])
    return build_fw.build(r["example"], r["role"], r["target"], r["overlay"],
                          r["inject_src"] or None)


def main(path):
    reqs = requests(path)
    t0, failed = time.time(), []
    with concurrent.futures.ThreadPoolExecutor(_WORKERS) as ex:
        futs = {ex.submit(_build, r): r for r in reqs}
        for f in concurrent.futures.as_completed(futs):
            try:
                f.result()
            except Exception as e:  # noqa: BLE001 - the run reports it again in its test
                r = futs[f]
                failed.append(f"{r['app']} [{r['target']}] {'+'.join(r['overlay'])}: {e}")
    print(f"prewarm: {len(reqs)} unique requests in {time.time() - t0:.0f}s, "
          f"{len(failed)} failed")
    for line in failed:
        print(f"  FAILED {line}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
