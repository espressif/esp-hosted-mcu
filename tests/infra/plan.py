# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Build plan: the images a run needs, without building them.

`eh.py test emu --plan` runs the collected tests with EH_PLAN set: every firmware
request is recorded (build_fw.plan_record) and the bench skips before building.
summarize() turns the record into the image count and which tests share each image."""
import collections
import json


def images(path):
    """{(app, target, overlay, inject): [test, ...]} from a plan record file."""
    img = collections.OrderedDict()
    with open(path) as f:
        for line in f:
            r = json.loads(line)
            key = (r["app"], r["target"], tuple(r["overlay"]), tuple(r["inject"]))
            img.setdefault(key, []).append(r["test"].split(" ")[0])
    return img


def summarize(path, out):
    img = images(path)
    cp = sum(1 for k in img if k[1] == "esp32c6")
    out(f"plan: {sum(len(v) for v in img.values())} requests -> {len(img)} images "
        f"(cp {cp}, host {len(img) - cp})")
    for app, n in collections.Counter(k[0] for k in img).most_common():
        out(f"  {n:3}  {app}")
    return len(img)
