"""Wi-Fi AP profiles for hardware benches: pick the AP by name, location, SSID,
band or tag, or by where this machine is plugged in.

Profiles live outside git:
  ~/.config/esp_hosted/wifi_aps.json   shared by every checkout and worktree
  tests/lab.local.json, key "wifi_aps"  this checkout only (gitignored)
  $EH_WIFI_APS_FILE                     any other file

Each file holds a list (or {"wifi_aps": [...]}) of profiles:
  {"name": "office-2g", "location": "office", "ssid": "...", "password": "...",
   "band": "2g", "subnet": "192.168.50.0/24", "peer": "", "tags": ["lab"]}
band is 2g, 5g or dual. subnet lets the bench find its AP by location: the
machine's interface address in that subnet is the AP's LAN, and it is also the
iperf peer unless "peer" is set.

Selection order (resolve):
  1. EH_TP_SSID (+ EH_TP_PASS): used as given.
  2. EH_WIFI=<query>: every word must match some field, case-insensitive.
  3. Location: the profile whose subnet holds one of this machine's addresses.
  4. The legacy "wifi_ap" in tests/lab.local.json, then tests/env.json.
Several matches or none fail with the candidate list (passwords masked).

CLI:  python3 -m infra.wifi_aps list | find <query> | which | add --name ...
"""
import ipaddress
import json
import os
import re
import subprocess
import sys
from pathlib import Path

_TESTS = Path(__file__).resolve().parents[1]
USER_FILE = Path(os.path.expanduser("~/.config/esp_hosted/wifi_aps.json"))
LAB_FILE = _TESTS / "lab.local.json"
FIELDS = ("name", "location", "ssid", "band", "subnet", "peer", "tags")
# Only these CP targets have a 5 GHz radio.
_5G_TARGETS = {"esp32c5"}


class WifiApError(RuntimeError):
    pass


def _read(path):
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return []
    if isinstance(data, dict):
        data = data.get("wifi_aps", [])
    return [dict(p, _file=str(path)) for p in data if isinstance(p, dict) and p.get("ssid")]


def load_profiles(env=os.environ):
    files = [USER_FILE, LAB_FILE]
    if env.get("EH_WIFI_APS_FILE"):
        files.insert(0, Path(env["EH_WIFI_APS_FILE"]))
    out = []
    for f in files:
        out += _read(f)
    return out


def mask(p):
    q = {k: v for k, v in p.items() if not k.startswith("_")}
    if q.get("password"):
        q["password"] = "***"
    return q


def _fmt(p):
    m = mask(p)
    return ", ".join(f"{k}={m[k]}" for k in ("name", "location", "ssid", "band", "subnet") if m.get(k))


def band_ok(p, cp_target):
    """A 2.4 GHz-only CP cannot use a 5g-only profile."""
    if not cp_target or cp_target.lower() in _5G_TARGETS:
        return True
    return (p.get("band") or "2g").lower() != "5g"


def match(profiles, query):
    words = query.lower().split()

    def hay(p):
        vals = [p.get(k, "") for k in FIELDS]
        vals = [" ".join(v) if isinstance(v, list) else str(v) for v in vals]
        return " ".join(vals).lower()
    return [p for p in profiles if all(w in hay(p) for w in words)]


def local_ipv4():
    """This machine's IPv4 addresses (loopback excluded)."""
    for cmd in (["ifconfig", "-a"], ["ip", "-4", "addr"]):
        try:
            txt = subprocess.run(cmd, capture_output=True, text=True, timeout=5).stdout
        except (OSError, subprocess.SubprocessError):
            continue
        ips = [ip for ip in re.findall(r"inet (?:addr:)?(\d+\.\d+\.\d+\.\d+)", txt)
               if not ip.startswith("127.")]
        if ips:
            return ips
    return []


def _in_subnet(ip, subnet):
    try:
        return ipaddress.ip_address(ip) in ipaddress.ip_network(subnet, strict=False)
    except ValueError:
        return False


def peer_for(p, ips):
    if p.get("peer"):
        return p["peer"]
    return next((ip for ip in ips if p.get("subnet") and _in_subnet(ip, p["subnet"])), "")


def _pick(cands, what):
    if len(cands) == 1:
        return cands[0]
    lines = "\n  ".join(_fmt(p) for p in cands) or "(none)"
    raise WifiApError(f"{what}: {len(cands)} Wi-Fi profiles match, need exactly one:\n  {lines}")


def _legacy():
    for f in (LAB_FILE, _TESTS / "env.json"):
        try:
            ap = json.loads(Path(f).read_text()).get("wifi_ap")
        except (OSError, ValueError):
            ap = None
        if ap and ap.get("ssid"):
            return dict(ap, _source=f"legacy wifi_ap in {Path(f).name}")
    return None


def resolve(env=os.environ, cp_target=None, ips=None):
    """The AP this run should use, as {ssid, password, band, peer, name, _source},
    or None when nothing is configured."""
    cp_target = cp_target or env.get("EH_CP_TARGET", "")
    ips = local_ipv4() if ips is None else ips
    profiles = load_profiles(env)

    if env.get("EH_TP_SSID"):
        p = next((q for q in profiles if q["ssid"] == env["EH_TP_SSID"]), {})
        return {"name": p.get("name", ""), "ssid": env["EH_TP_SSID"],
                "password": env.get("EH_TP_PASS", p.get("password", "")),
                "band": p.get("band", ""),
                "peer": env.get("EH_TP_PEER") or peer_for(p, ips),
                "_source": "EH_TP_SSID"}

    usable = [p for p in profiles if band_ok(p, cp_target)]
    if env.get("EH_WIFI"):
        p = _pick(match(usable, env["EH_WIFI"]), f"EH_WIFI={env['EH_WIFI']!r} (cp={cp_target or '?'})")
        src = f"EH_WIFI={env['EH_WIFI']}"
    else:
        here = [p for p in usable if p.get("subnet") and any(_in_subnet(ip, p["subnet"]) for ip in ips)]
        if not here:
            return _legacy()
        if len(here) > 1:
            # Two profiles on one LAN (e.g. 2g and 5g SSIDs): prefer a 2g one
            # for a 2.4 GHz-only CP, else ask.
            two = [p for p in here if (p.get("band") or "2g").lower() in ("2g", "dual")]
            here = two if cp_target.lower() not in _5G_TARGETS and len(two) == 1 else here
        p = _pick(here, f"location (local {', '.join(ips)})")
        src = "location"
    return {"name": p.get("name", ""), "ssid": p["ssid"], "password": p.get("password", ""),
            "band": p.get("band", ""), "peer": env.get("EH_TP_PEER") or peer_for(p, ips),
            "_source": f"{src} -> {p.get('name') or p['ssid']} ({p['_file']})"}


def apply_env(env=os.environ, cp_target=None):
    """Fill EH_TP_SSID / EH_TP_PASS / EH_TP_PEER from the resolved profile,
    keeping any value the caller already set. Returns the profile or None."""
    ap = resolve(env, cp_target)
    if ap:
        # An exported-but-empty variable counts as unset.
        for k, v in (("EH_TP_SSID", ap["ssid"]), ("EH_TP_PASS", ap["password"]),
                     ("EH_TP_PEER", ap.get("peer", ""))):
            if v and not env.get(k):
                env[k] = v
    return ap


def _add(args):
    import argparse
    ap = argparse.ArgumentParser(prog="wifi_aps add")
    for f in ("name", "location", "ssid", "password", "band", "subnet", "peer"):
        ap.add_argument(f"--{f}", default="", required=f in ("name", "ssid"))
    ap.add_argument("--tags", default="", help="comma list")
    ap.add_argument("--file", default=str(USER_FILE))
    a = ap.parse_args(args)
    path = Path(a.file)
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        data = json.loads(path.read_text())
    except (OSError, ValueError):
        data = {}
    lst = data.get("wifi_aps", []) if isinstance(data, dict) else data
    lst = [p for p in lst if p.get("name") != a.name]
    lst.append({k: v for k, v in {"name": a.name, "location": a.location, "ssid": a.ssid,
                                   "password": a.password, "band": a.band or "2g",
                                   "subnet": a.subnet, "peer": a.peer,
                                   "tags": [t for t in a.tags.split(",") if t]}.items() if v})
    if isinstance(data, dict):
        data["wifi_aps"] = lst
    else:
        data = lst
    path.write_text(json.dumps(data, indent=2) + "\n")
    os.chmod(path, 0o600)
    print(f"saved {a.name} to {path}")


def main(argv):
    cmd = argv[0] if argv else "which"
    try:
        if cmd == "list":
            for p in load_profiles():
                print(_fmt(p), f"  [{p['_file']}]")
        elif cmd == "find":
            for p in match(load_profiles(), " ".join(argv[1:])):
                print(_fmt(p))
        elif cmd == "which":
            ap = resolve()
            print(json.dumps(mask(ap), indent=2) if ap else "no Wi-Fi AP configured", file=sys.stdout)
            if ap:
                print(f"source: {ap['_source']}")
        elif cmd == "add":
            _add(argv[1:])
        else:
            print(__doc__)
            return 2
    except WifiApError as e:
        print(e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
