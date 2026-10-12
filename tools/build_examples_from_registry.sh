#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#
# Build the PUBLISHED esp_hosted examples straight from the ESP Component Registry
# — the true consumer path. For a component version (stated, else newest), it
# enumerates that version's examples, fetches each one-by-one via
# `idf.py create-project-from-example` (pins the version, strips override_path
# like a real user gets), derives each example's targets from the
# sdkconfig.defaults.<chip> it ships (host roles -> $HOST_TARGETS), builds each,
# and maintains a LIVE report + resumable state.
#
# Usage:
#   . ~/esp-idf/export.sh
#   tools/build_examples_from_registry.sh [VERSION] [--rebuild-always] [--lean]
#     VERSION          registry version (default: newest published)
#     --rebuild-always rebuild PASS cells too (default: resume — skip PASS)
#     --lean           breadth-first: build ONE scenario-correct target per
#                      example/role (picked from its derived set — so a classic-BT
#                      cp still gets esp32, a thread cp esp32c6, a host esp32p4),
#                      to cover every scenario fast before a full-matrix run.
#     --force-component-ver[=SPEC]
#                      pin the fetched example's esp_hosted dep to SPEC (default ~3)
#                      so the solver can't silently downgrade to 2.x via
#                      esp_wifi_remote's '<3.0' cap. Proves the CURRENT major builds
#                      before we edit the real manifests.
#
# Output tree:  /tmp/hosted/registry_example_build_sweep_<version>/
#   progress.html         live report (auto-refresh; open in a browser)
#   state.tsv             persistent per-cell status (drives resume)
#   <YYYYmmdd-HHMMSS>/logs/<example/path>/<target>.log   this run's build logs
# Old run dirs (>4 days) are pruned on start.
#
# Env: ONLY=<ERE> (subset), HOST_TARGETS=... (default esp32p4), NS/NAME (component).
# Requires: idf.py + idf-build-apps (IDF exported), curl, python3.
set -u

NS="${NS:-espressif}"; NAME="${NAME:-esp_hosted}"
API="https://components.espressif.com/api/components/${NS}/${NAME}"
HOST_TARGETS="${HOST_TARGETS:-esp32p4}"
REBUILD_ALWAYS=0
LEAN=0
FORCE_VER=""
ARGVER=""
for a in "$@"; do
    case "$a" in
        --rebuild-always) REBUILD_ALWAYS=1 ;;
        --lean) LEAN=1 ;;
        --force-component-ver) FORCE_VER='~3' ;;               # pin esp_hosted dep to 3.x (default)
        --force-component-ver=*) FORCE_VER="${a#--force-component-ver=}" ;;
        --only=*) ONLY="${a#--only=}" ;;          # ERE over example paths; picks a build item + all its scenarios/targets
        --*) echo "unknown flag: $a" >&2; exit 2 ;;
        *) ARGVER="$a" ;;
    esac
done

# ---- CPU budget: confine the WHOLE sweep (every build/ninja/gcc descendant) to
# most-but-not-all cores, applied ONCE at launch via a taskset re-exec — so the box
# never loses all cores no matter how ninja parallelises. Maximise, don't hog. ----
NPROC="$(nproc)"
EH_MAX_CPUS="${EH_MAX_CPUS:-$(( NPROC * 3 / 4 ))}"; [ "$EH_MAX_CPUS" -lt 1 ] && EH_MAX_CPUS=1
JOBS="${JOBS:-3}"; [ "$JOBS" -lt 1 ] && JOBS=1
# Bound concurrency by MEMORY too: taskset caps CPU, not RAM, and an OOM-killed cc1
# is a FALSE failure. Each concurrent IDF build peaks ~EH_MEM_PER_JOB_MB.
EH_MEM_PER_JOB_MB="${EH_MEM_PER_JOB_MB:-4500}"
_memmb=$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo 2>/dev/null); [ -n "$_memmb" ] || _memmb=$((JOBS*EH_MEM_PER_JOB_MB))
_jmem=$(( _memmb / EH_MEM_PER_JOB_MB )); [ "$_jmem" -lt 1 ] && _jmem=1
[ "$JOBS" -gt "$_jmem" ] && { echo "note: memory cap JOBS ${JOBS} -> ${_jmem} (${_memmb}MB avail / ${EH_MEM_PER_JOB_MB}MB per build)" >&2; JOBS="$_jmem"; }
PER_JOB=$(( EH_MAX_CPUS / JOBS )); [ "$PER_JOB" -lt 1 ] && PER_JOB=1
if [ -z "${EH_TASKSET_APPLIED:-}" ] && command -v taskset >/dev/null 2>&1; then
    export EH_TASKSET_APPLIED=1 EH_MAX_CPUS JOBS
    exec taskset -c "0-$(( EH_MAX_CPUS - 1 ))" bash "$0" "$@"
fi

command -v idf.py          >/dev/null || { echo "idf.py not on PATH — source export.sh first" >&2; exit 2; }
command -v idf-build-apps  >/dev/null || { echo "idf-build-apps not on PATH — pip install idf-build-apps" >&2; exit 2; }
command -v ccache          >/dev/null || { echo "ccache not on PATH — REFUSING to run cold/slow builds. Install ccache (IDF_CCACHE_ENABLE needs it; without it every build recompiles from scratch)." >&2; exit 2; }
command -v curl            >/dev/null || { echo "curl not on PATH" >&2; exit 2; }
HAVE_ESPTOOL=1; command -v esptool.py >/dev/null || { HAVE_ESPTOOL=0; echo "WARN: esptool.py not found — merged.bin artifacts will be skipped" >&2; }

BASE=/tmp/hosted
mkdir -p "$BASE"
# Prune run dirs (the <datetime> level) older than 4 days, then empty version dirs.
find "$BASE" -mindepth 2 -maxdepth 2 -type d -mtime +4 -exec rm -rf {} + 2>/dev/null || true
find "$BASE" -mindepth 1 -maxdepth 1 -type d -empty -delete 2>/dev/null || true

# Shared ccache so the big matrix is CPU-bound, not cold every build.
export IDF_CCACHE_ENABLE="${IDF_CCACHE_ENABLE:-1}"
export CCACHE_DIR="${CCACHE_DIR:-$HOME/.cache/eh_reg_ccache}"; mkdir -p "$CCACHE_DIR"

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

echo "=== [1/4] resolve version + example list ==="
fetched=0
for attempt in 1 2 3 4 5; do
    if curl -fsS -m 30 -o "$TMP/component.json" "$API"; then fetched=1; break; fi
    echo "  registry metadata fetch attempt $attempt failed; retry in $((attempt*3))s..." >&2; sleep $((attempt*3))
done
[ "$fetched" = 1 ] || { echo "registry metadata fetch failed after 5 attempts: $API" >&2; exit 2; }
python3 - "$TMP/component.json" "$TMP/ver.txt" "$TMP/examples.txt" "$ARGVER" <<'PY' || exit 3
import json, sys
comp, ver_out, ex_out, want = sys.argv[1:5]
try:
    d = json.load(open(comp))
except Exception as e:
    sys.stderr.write(f"registry JSON parse failed: {e}\n"); sys.exit(4)
# Schema guards — fail loudly (exit 4) if the registry shape drifts from what we assume.
if not isinstance(d, dict) or not isinstance(d.get("versions"), list):
    sys.stderr.write("registry JSON schema changed: top-level 'versions' list missing\n"); sys.exit(4)
vers = {v["version"]: v for v in d["versions"]
        if isinstance(v, dict) and v.get("version") and not v.get("yanked_at")}
if not vers:
    sys.stderr.write("registry JSON: no usable (non-yanked) versions\n"); sys.exit(4)
if not want:
    def key(s):
        parts = [int(p) if p.isdigit() else 0 for p in s.split("-")[0].split(".")]
        return (0 if "-" in s else 1, parts)   # releases rank above -dev
    want = sorted(vers, key=key)[-1]
v = vers.get(want)
if not v:
    sys.stderr.write(f"VERSION {want!r} not found; have {sorted(vers)[-8:]}\n"); sys.exit(3)
if not isinstance(v.get("examples"), list):
    sys.stderr.write(f"registry JSON schema changed: version {want} has no 'examples' list\n"); sys.exit(4)
exs = [e["name"] for e in v["examples"]
       if isinstance(e, dict) and e.get("name") and "/linux_802_3_host/" not in e["name"]]  # linux apps not idf-buildable
if not exs:
    sys.stderr.write(f"registry JSON: version {want} lists no buildable examples "
                     "(schema drift — 'name' key gone?)\n"); sys.exit(4)
open(ver_out, "w").write(want)
open(ex_out, "w").write("\n".join(exs))
PY
VER="$(cat "$TMP/ver.txt")"
readarray -t EXAMPLES < "$TMP/examples.txt"
[ -n "$VER" ] && [ "${#EXAMPLES[@]}" -gt 0 ] || { echo "no buildable examples for ${VER:-?}" >&2; exit 3; }
if [ -n "${ONLY:-}" ]; then
    mapfile -t EXAMPLES < <(printf '%s\n' "${EXAMPLES[@]}" | grep -E "$ONLY" || true)
fi

ROOT="$BASE/registry_example_build_sweep_${VER}"
RUN="$ROOT/$(date +%Y%m%d-%H%M%S)"
STATE="$ROOT/state.tsv"; HTML="$ROOT/progress.html"
mkdir -p "$RUN/logs"; touch "$STATE"
# Resume hygiene: a prior run interrupted mid-build leaves cells marked RUNNING.
# Those were NOT built — reset to PENDING so this run rebuilds them (only PASS is
# ever skipped) and the report doesn't show phantom "running" rows.
sed -i 's/\tRUNNING\t/\tPENDING\t/' "$STATE" 2>/dev/null || true
echo "  version: $VER   examples: ${#EXAMPLES[@]}   root: $ROOT"
[ "$REBUILD_ALWAYS" = 1 ] && echo "  --rebuild-always: rebuilding PASS cells too"
[ "$LEAN" = 1 ] && echo "  --lean: breadth-first — ONE scenario-correct target per example/role"
[ -n "$FORCE_VER" ] && echo "  --force-component-ver: pinning esp_hosted dep to '$FORCE_VER' (override registry '*' -> no 2.x downgrade)"

role_of() { echo "${1##*/}"; }

# Targets come from the FETCHED example (the published artifact the consumer gets),
# NOT the repo. Preference: a README "Supported Targets" declaration if the example
# carries one (IDF convention); otherwise the example's own scenario configs
# (sdkconfig.defaults.<chip> that shipped with it). Role-aware: a CP example runs on
# every chip it ships; a host example runs on the MCU-host targets (p4/h2) only — the
# per-chip files in a host dir are host-on-that-chip tuning, not build targets.
HOST_CHIPS_RE='^(esp32p4|esp32h2)$'
targets_from_fetched() {   # $1 = fetched project dir  $2 = role
    local dir="$1" role="$2" f t chips="" host="" rd decl
    rd="$(find "$dir" -maxdepth 2 -iname 'README*' 2>/dev/null | head -1)"
    if [ -n "$rd" ]; then   # declared "Supported Targets" wins (normalise ESP32-C6 -> esp32c6)
        decl="$(grep -iA2 'supported *targets' "$rd" 2>/dev/null | grep -oiE 'esp32-?[a-z]?[0-9]*' \
                | tr 'A-Z' 'a-z' | tr -d '-' | sort -u | tr '\n' ' ')"
        [ -n "$decl" ] && { echo "$decl"; return; }
    fi
    for f in "$dir"/sdkconfig.defaults.esp32*; do
        [ -e "$f" ] || continue; t="${f##*sdkconfig.defaults.}"; chips="$chips $t"
    done
    case "$role" in
        cp|cp_*)             echo "${chips:-esp32c6}" ;;
        mcu_host|esp_host)   for t in $chips; do [[ "$t" =~ $HOST_CHIPS_RE ]] && host="$host $t"; done
                             echo "${host:-$HOST_TARGETS}" ;;
        *)                   echo "$HOST_TARGETS" ;;
    esac
}

# --lean: collapse a derived target set to ONE representative — the highest-priority
# chip that is ACTUALLY in the set, so the pick stays scenario-correct (a classic-BT
# cp ships only esp32 -> esp32; a thread cp ships c6/h2 -> esp32c6; a host -> esp32p4).
lean_pick() {   # $1=role, rest=targets ; echo one scenario-appropriate target
    local role="$1"; shift; local first="$1" set=" $* " pref p
    case "$role" in
        cp|cp_*)           pref="esp32c5 esp32c6 esp32c3 esp32c61 esp32c2 esp32s3 esp32s2 esp32h2 esp32" ;;  # c5/c6 first; esp32 last => only classic-BT (esp32-only) lands there
        mcu_host|esp_host) pref="esp32p4 esp32h2" ;;
        *)                 pref="esp32p4 esp32h2" ;;
    esac
    for p in $pref; do case "$set" in *" $p "*) echo "$p"; return ;; esac; done
    echo "$first"
}

# --force-component-ver: pin the fetched example's DIRECT esp_hosted dep to a spec
# (default ~3) so the solver can't silently downgrade to 2.x via esp_wifi_remote's
# '<3.0' cap. Rewrites the fetched (throwaway) manifest; caller drops the resolved
# lock + managed_components so the build re-solves against the pinned major.
force_esp_hosted_ver() {   # $1=manifest path  $2=version spec
    python3 - "$1" "$2" <<'PY'
import sys
mf, ver = sys.argv[1], sys.argv[2]
try:
    import yaml
except Exception:
    sys.stderr.write("force_esp_hosted_ver: PyYAML unavailable (source IDF)\n"); sys.exit(3)
with open(mf) as f: d = yaml.safe_load(f) or {}
deps = d.get('dependencies') or {}
for k in list(deps):
    if str(k).split('/')[-1] == 'esp_hosted':
        v = deps[k]
        if isinstance(v, dict): v['version'] = ver
        else: deps[k] = {'version': ver}
with open(mf, 'w') as f: yaml.safe_dump(d, f, sort_keys=False, default_flow_style=False)
PY
}

# ---- work list is built in the PLAN phase below, from each FETCHED example ----
: > "$ROOT/worklist.tsv"

# ---- state helpers (state.tsv: example \t target \t status \t logrel) ----
state_get() { awk -F'\t' -v e="$1" -v t="$2" '$1==e&&$2==t{print $3; exit}' "$STATE"; }
state_set() {   # ex target status logrel
    local tmp; tmp="$(mktemp)"
    awk -F'\t' -v e="$1" -v t="$2" '!($1==e&&$2==t)' "$STATE" > "$tmp"
    printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "${4:-}" >> "$tmp"
    mv "$tmp" "$STATE"
}

# ---- HTML renderer (written once, called after each state change) ----
cat > "$ROOT/render.py" <<'PY'
import sys, html, time, os
root, worklist, state, ver, out = sys.argv[1:6]
st = {}
if os.path.exists(state):
    for l in open(state):
        p = l.rstrip("\n").split("\t")
        if len(p) >= 3: st[(p[0], p[1])] = (p[2], p[3] if len(p) > 3 else "")
rows = [l.rstrip("\n").split("\t") for l in open(worklist) if l.strip()]
BADGE = {"PASS":"#1a7f37","FAIL":"#cf222e","FETCH_FAIL":"#82071e","NO_PROJECT":"#82071e",
         "TIMEOUT":"#a40e26","RUNNING":"#bf8700","PENDING":"#8b949e"}
cnt = {}
for ex, t, r in rows:
    s = st.get((ex, t), ("PENDING", ""))[0]; cnt[s] = cnt.get(s, 0) + 1
_fail = cnt.get("FAIL",0)+cnt.get("FETCH_FAIL",0)+cnt.get("NO_PROJECT",0)+cnt.get("TIMEOUT",0)
tot = len(rows); done = cnt.get("PASS",0)+_fail
pct = (done*100//tot) if tot else 0
def chip(s):
    return f'<span style="background:{BADGE.get(s,"#8b949e")};color:#fff;padding:2px 8px;border-radius:10px;font-size:12px;font-weight:600">{s}</span>'
o = []
o.append(f"""<!doctype html><html><head><meta charset=utf-8>
<meta http-equiv=refresh content=8>
<title>esp_hosted registry build — {html.escape(ver)}</title>
<style>
 body{{font:14px/1.5 -apple-system,Segoe UI,Roboto,sans-serif;margin:0;background:#f6f8fa;color:#1f2328}}
 header{{background:#24292f;color:#fff;padding:16px 24px}}
 header h1{{margin:0;font-size:18px}} header .sub{{opacity:.8;font-size:13px;margin-top:4px}}
 .bar{{height:10px;background:#d0d7de;border-radius:6px;overflow:hidden;margin:10px 24px}}
 .bar > i{{display:block;height:100%;width:{pct}%;background:#1a7f37}}
 .counts{{padding:4px 24px 14px;font-size:13px}} .counts b{{margin-right:14px}}
 table{{border-collapse:collapse;width:calc(100% - 48px);margin:0 24px 24px;background:#fff;box-shadow:0 1px 3px #0002}}
 th,td{{text-align:left;padding:7px 10px;border-bottom:1px solid #eaeef2;font-size:13px}}
 th{{background:#f6f8fa;position:sticky;top:0}}
 tr.RUNNING{{background:#fff8e5}} tr.FAIL,tr.FETCH_FAIL,tr.NO_PROJECT{{background:#fff0f0}}
 code{{font:12px ui-monospace,monospace}} a{{color:#0969da}}
</style></head><body>
<header><h1>esp_hosted — registry example build sweep</h1>
<div class=sub>component <code>espressif/esp_hosted</code> · version <b>{html.escape(ver)}</b> · updated {time.strftime('%Y-%m-%d %H:%M:%S')} · auto-refresh 8s</div></header>
<div class=bar><i></i></div>
<div class=counts><b>{done}/{tot} done ({pct}%)</b>
 ✅ PASS {cnt.get('PASS',0)} · ❌ FAIL {_fail} · ⏳ RUNNING {cnt.get('RUNNING',0)} · · PENDING {cnt.get('PENDING',0)}</div>
<table><tr><th>#</th><th>Example</th><th>Role</th><th>Target</th><th>Status</th><th>Log</th><th>Files</th></tr>""")
for i, (ex, t, r) in enumerate(rows, 1):
    s, logrel = st.get((ex, t), ("PENDING", ""))
    log = f'<a href="{html.escape(logrel)}">log</a>' if logrel else '·'
    artrel = logrel.replace('/logs/', '/artifacts/')[:-4] if logrel.endswith('.log') else ''
    files = f'<a href="{html.escape(artrel)}/">bins</a>' if (s == 'PASS' and artrel) else '·'
    o.append(f'<tr class="{s}"><td>{i}</td><td><code>{html.escape(ex)}</code></td><td>{r}</td>'
             f'<td><code>{t}</code></td><td>{chip(s)}</td><td>{log}</td><td>{files}</td></tr>')
o.append("</table></body></html>")
open(out, "w").write("\n".join(o))
PY
render() { python3 "$ROOT/render.py" "$ROOT" "$ROOT/worklist.tsv" "$STATE" "$VER" "$HTML"; }

echo "=== [2/4] report: $HTML ==="
render

LOCK="$ROOT/.lock"; : > "$LOCK"   # CPU budget already applied at launch (taskset re-exec, see top)

# state.tsv is read-modify-write; JOBS workers update concurrently, so every
# state change + report render happens under one flock -> no lost/torn updates.
sync_state() { ( flock 9; state_set "$1" "$2" "$3" "${4:-}"; render ) 9>"$LOCK"; }

# Keep the flashables for a completed build: resolved sdkconfig, the app bin, and a
# single merged image. Returns 0 ONLY if the app bin exists — proof the build really
# produced output; the caller folds this into the PASS gate so a partial/aborted
# build is never recorded as done.
save_artifacts() {   # $1 proj  $2 target  $3 example
    local proj="$1" t="$2" ex="$3" bdir="$1/build_$2" art appbin
    appbin="$(find "$bdir" -maxdepth 1 -name '*.bin' ! -name 'bootloader*' ! -name '*partition*' ! -name 'ota_data*' 2>/dev/null | head -1)"
    [ -n "$appbin" ] || return 1
    art="$RUN/artifacts/$ex/$t"; mkdir -p "$art"
    cp "$proj/sdkconfig" "$art/sdkconfig" 2>/dev/null || true
    cp "$appbin" "$art/" 2>/dev/null || true
    [ "${HAVE_ESPTOOL:-0}" = 1 ] && \
        ( cd "$bdir" && esptool.py --chip "$t" merge_bin -o "$art/merged.bin" @flash_args >/dev/null 2>&1 ) || true
    return 0
}

# Build worker: build every PLANNED target of $1 from its already-fetched project.
run_build() {
    local ex="$1" d proj wex wt rc logrel
    d="$RUN/proj/${ex//\//__}"
    proj="$(dirname "$(dirname "$(find "$d" -maxdepth 3 -path '*/main/idf_component.yml' 2>/dev/null | head -1)")")"
    while IFS="$(printf '\t')" read -r wex wt _; do
        { [ "$wex" = "$ex" ] && [ "$wt" != "-" ]; } || continue
        [ "$REBUILD_ALWAYS" != 1 ] && [ "$(state_get "$ex" "$wt")" = PASS ] && { echo "  [skip PASS] $ex $wt"; continue; }
        if [ ! -d "$proj" ]; then sync_state "$ex" "$wt" FETCH_FAIL ""; continue; fi
        logrel="$(basename "$RUN")/logs/$ex/$wt.log"; mkdir -p "$(dirname "$ROOT/$logrel")"
        sync_state "$ex" "$wt" RUNNING "$logrel"
        # PER_JOB caps ninja; launch-time taskset confines the whole sweep. Builds can
        # hang -> hard EH_BUILD_TIMEOUT. PASS only on rc0 AND an app bin (save_artifacts);
        # timeout(124) -> TIMEOUT; else -> FAIL. A partial build is never recorded done.
        CMAKE_BUILD_PARALLEL_LEVEL="$PER_JOB" timeout "${EH_BUILD_TIMEOUT:-1800}" \
            idf-build-apps build -p "$proj" --target "$wt" --build-dir "build_@t" > "$ROOT/$logrel" 2>&1
        rc=$?
        if [ "$rc" = 0 ] && save_artifacts "$proj" "$wt" "$ex"; then
            sync_state "$ex" "$wt" PASS "$logrel"; echo "  [PASS] $ex $wt"
        elif [ "$rc" = 124 ]; then
            sync_state "$ex" "$wt" TIMEOUT "$logrel"; echo "  [TIMEOUT] $ex $wt (>${EH_BUILD_TIMEOUT:-1800}s)"
        else
            sync_state "$ex" "$wt" FAIL "$logrel"; echo "  [FAIL] $ex $wt"
        fi
    done < "$ROOT/worklist.tsv"
}

echo "=== [3/4] plan — fetch each example, read its targets FROM THE FETCHED artifact ==="
for ex in "${EXAMPLES[@]}"; do
    role="$(role_of "$ex")"
    # resume: example already fully PASS in state -> reuse recorded targets, skip fetch
    if [ "$REBUILD_ALWAYS" != 1 ]; then
        counts="$(awk -F'\t' -v e="$ex" '$1==e{n++} $1==e&&$3=="PASS"{p++} END{print (n+0)":"(p+0)}' "$STATE")"
        if [ "${counts%%:*}" -gt 0 ] && [ "${counts%%:*}" = "${counts##*:}" ]; then
            awk -F'\t' -v e="$ex" -v r="$role" '$1==e{print $1"\t"$2"\t"r}' "$STATE" >> "$ROOT/worklist.tsv"
            echo "  [plan] $ex — ${counts%%:*} PASS (skip fetch)"; continue
        fi
    fi
    d="$RUN/proj/${ex//\//__}"; fok=0
    for attempt in 1 2 3; do
        rm -rf "$d"; mkdir -p "$d"
        ( cd "$d" && timeout "${EH_FETCH_TIMEOUT:-180}" idf.py create-project-from-example \
              "${NS}/${NAME}=${VER}:${ex}" >/dev/null 2>&1 ) && { fok=1; break; }
        sleep $((attempt*3))
    done
    if [ "$fok" != 1 ]; then
        printf '%s\t-\t%s\n' "$ex" "$role" >> "$ROOT/worklist.tsv"; sync_state "$ex" "-" FETCH_FAIL ""
        echo "  [FETCH_FAIL] $ex (after 3 attempts)"; continue
    fi
    proj="$(dirname "$(dirname "$(find "$d" -maxdepth 3 -path '*/main/idf_component.yml' | head -1)")")"
    if [ -n "$FORCE_VER" ]; then
        force_esp_hosted_ver "$proj/main/idf_component.yml" "$FORCE_VER"
        rm -f "$proj/dependencies.lock"; rm -rf "$proj/managed_components"
    fi
    tg="$(targets_from_fetched "$proj" "$role")"
    [ "$LEAN" = 1 ] && tg="$(lean_pick "$role" $tg)"
    for t in $tg; do printf '%s\t%s\t%s\n' "$ex" "$t" "$role" >> "$ROOT/worklist.tsv"; done
    echo "  [plan] $ex -> $tg$( [ "$LEAN" = 1 ] && echo ' (lean)')"
done
render

echo "=== [3/4] build — ${JOBS} concurrent × ${PER_JOB} cores (budget ${EH_MAX_CPUS}/${NPROC}); resume skips PASS ==="
for ex in "${EXAMPLES[@]}"; do
    run_build "$ex" &
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 1; done
done
wait

echo "=== [4/4] done — $HTML ==="
render
echo "  ---- status counts ----"
awk -F'\t' '{c[$3]++} END{for(k in c) printf "    %-12s %d\n", k, c[k]}' "$STATE"
echo "  ---- problems (example -> target: status) ----"
TAB="$(printf '\t')"
awk -F'\t' '$3!="PASS"' "$STATE" | sort -t"$TAB" -k1,1 -k2,2 | awk -F'\t' '
    {if($1!=prev){print "    "$1; prev=$1} printf "        %-10s %s\n",$2,$3}'
if grep -qE "$TAB(FAIL|FETCH_FAIL|NO_PROJECT|TIMEOUT)$TAB" "$STATE"; then
    echo "  >>> FAILURES above (see per-cell logs / progress.html)"; exit 1
else
    echo "  >>> all green"; exit 0
fi
