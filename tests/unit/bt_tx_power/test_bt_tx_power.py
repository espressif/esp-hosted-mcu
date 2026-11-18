# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""BLE TX power symbol map vs every chip's IDF esp_power_level_t. Host C compiler, no target.

For each IDF header family the CP map (eh_cp_bt_tx_power_map.h) is compiled against that
header's enum and must give: every IDF name -> its IDF value; every other symbol ->
unsupported; every native value -> its canonical (non-deprecated) name. Older IDF release
tags must define the same names.
"""
import os
import re
import shutil
import subprocess

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
# $EH_UNIT_IDF_PATH, else pinned .deps/esp-idf, else $IDF_PATH (CI image).
IDF = os.environ.get('EH_UNIT_IDF_PATH') or os.path.join(REPO, '.deps', 'esp-idf')
if not os.path.isdir(IDF) and os.environ.get('IDF_PATH'):
    IDF = os.environ['IDF_PATH']
INC = [os.path.join(REPO, 'coprocessor', 'features', 'eh_cp_feat_bt', 'include'),
       os.path.join(REPO, 'coprocessor', 'features', 'eh_cp_feat_bt', 'src')]
HDR = 'components/bt/include/{}/include/esp_bt.h'

FAMILIES = ['esp32', 'esp32c2', 'esp32c3', 'esp32c5', 'esp32c6', 'esp32h2', 'esp32h4', 'esp32s31']
# BLE on H4 and S31 is buildable from 6.1 only.
SINCE_6_1 = {'esp32h4', 'esp32s31'}
REFS = ['v5.5', 'v5.5.1', 'v5.5.2', 'v5.5.3', 'v5.5.4', 'v5.5.5',
        'v6.0', 'v6.0.1', 'v6.0.2', 'v6.1', 'v6.1.1']

ENUM_RE = re.compile(r'typedef\s+enum(?:\s+\w+)?\s*\{([^{}]*)\}\s*esp_power_level_t\s*;')
ENTRY_RE = re.compile(r'(ESP_PWR_LVL_\w+)\s*=\s*(\w+)\s*,?\s*(?:/\*!<(.*?)\*/)?', re.S)


def _enum_block(text):
    m = ENUM_RE.search(text)
    assert m, 'esp_power_level_t enum not found'
    return m.group(0)


def _parse(text):
    """{name: (value, alias)} without INVALID. alias = deprecated / backward-compat / refers to a name."""
    out = {}
    for name, val, cmt in ENTRY_RE.findall(_enum_block(text)):
        if name == 'ESP_PWR_LVL_INVALID':
            continue
        ref = val.startswith('ESP_PWR_LVL_')
        v = out[val[len('ESP_PWR_LVL_'):]][0] if ref else int(val, 0)
        alias = ref or bool(re.search(r'deprecated|backward', cmt or '', re.I))
        out[name[len('ESP_PWR_LVL_'):]] = (v, alias)
    return out


def _sym(name):
    return (100 if name[0] == 'N' else 200) + int(name[1:])


def _bt_api_text(family):
    """esp_bt.h of the family, plus common/btdm_le.h when it includes it (H4/S31 from IDF 6.1.1)."""
    with open(os.path.join(IDF, HDR.format(family))) as f:
        text = f.read()
    if 'btdm_le.h' in text:
        with open(os.path.join(IDF, 'components/bt/include/common/btdm_le.h')) as f:
            text += f.read()
    return text


def _git_show(ref, path):
    r = subprocess.run(['git', '-C', IDF, 'show', f'{ref}:{path}'], capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def _missing(why):
    """Skip locally; fail in CI, where a skip would pass the job with nothing tested."""
    if os.environ.get('CI'):
        pytest.fail(why)
    pytest.skip(why)


def _need_idf():
    if not os.path.isfile(os.path.join(IDF, HDR.format('esp32c6'))):
        _missing('no ESP-IDF (.deps/esp-idf or $IDF_PATH)')


def _need_cc():
    cc = shutil.which('cc') or shutil.which('gcc')
    if not cc:
        _missing('no host C compiler')
    return cc


def _idf_version():
    with open(os.path.join(IDF, 'tools', 'cmake', 'version.cmake')) as f:
        v = dict(re.findall(r'set\(IDF_VERSION_(MAJOR|MINOR) (\d+)\)', f.read()))
    return int(v['MAJOR']), int(v['MINOR'])


@pytest.mark.parametrize('family', FAMILIES)
def test_bt_tx_power_map(tmp_path, family):
    _need_idf()
    if family in SINCE_6_1 and _idf_version() < (6, 1):
        pytest.skip(f'{family}: BLE buildable from IDF 6.1')
    cc = _need_cc()
    with open(os.path.join(IDF, HDR.format(family))) as f:
        text = f.read()
    idf = _parse(text)
    canon = {n: v for n, (v, a) in idf.items() if not a}

    work = os.path.join(str(tmp_path), family)
    os.makedirs(work, exist_ok=True)
    with open(os.path.join(work, 'idf_pwr.h'), 'w') as f:
        f.write(_enum_block(text) + '\n')
    exe = os.path.join(work, 'dump')
    cmd = [cc, '-std=c99', '-Wall', '-Wextra', '-Werror', f'-DEH_CP_BT_PWR_FAMILY_{family.upper()}=1', '-I', work]
    for d in INC:
        cmd += ['-I', d]
    subprocess.run(cmd + [os.path.join(HERE, 'test_bt_tx_power.c'), '-o', exe], check=True)
    lines = subprocess.run([exe], capture_output=True, text=True, check=True).stdout.splitlines()

    syms, rev, unsupported = {}, {}, []
    for ln in lines:
        p = ln.split()
        if p[0] == 'S':
            syms[p[1]] = (int(p[2]), int(p[3]), int(p[4]))
        elif p[0] == 'R':
            rev[int(p[1])] = int(p[2])
        elif p[0] == 'U':
            unsupported.append(int(p[1]))

    # Symbol encoding: N<x> = 100 + x, P<x> = 200 + x, all distinct.
    for name, (sym, _, _) in syms.items():
        assert sym == _sym(name), f'{name}: symbol {sym}'
    assert len({s for s, _, _ in syms.values()}) == len(syms)

    # Every IDF name has a symbol (host enum is complete).
    missing = sorted(set(idf) - set(syms))
    assert not missing, f'{family}: IDF names without a symbol: {missing}'

    # Set: IDF name -> its IDF value; any other symbol -> unsupported.
    for name, (_, ok, native) in syms.items():
        if name in idf:
            assert ok == 1 and native == idf[name][0], f'{family} {name}: ok={ok} native={native}'
        else:
            assert ok == 0, f'{family} {name}: accepted but not in IDF header'
    assert unsupported == [0, 0], 'INVALID / 0 must not map'

    # Get: native -> canonical name; anything else -> INVALID.
    by_val = {}
    for n, v in canon.items():
        assert v not in by_val, f'{family}: two canonical names for {v}: {by_val[v]}, {n}'
        by_val[v] = n
    for native, sym in rev.items():
        want = _sym(by_val[native]) if native in by_val else 255
        assert sym == want, f'{family}: native {native} -> {sym}, want {want}'


@pytest.mark.parametrize('family', FAMILIES)
def test_bt_tx_power_names_stable(family):
    """Each supported IDF release defines the same names and aliases (values may differ)."""
    _need_idf()
    if family in SINCE_6_1 and _idf_version() < (6, 1):
        pytest.skip(f'{family}: BLE buildable from IDF 6.1')
    with open(os.path.join(IDF, HDR.format(family))) as f:
        base = {n: a for n, (_, a) in _parse(f.read()).items()}
    checked = 0
    for ref in REFS:
        if family in SINCE_6_1 and not ref.startswith('v6.1'):
            continue
        text = _git_show(ref, HDR.format(family))
        if text is None:
            continue
        got = {n: a for n, (_, a) in _parse(text).items()}
        assert got == base, f'{family} @ {ref}: names/aliases differ from pinned IDF'
        checked += 1
    if not checked:
        pytest.skip('no IDF release tags available')


CP_CFG_BT = os.path.join(REPO, 'coprocessor', 'eh_cp_core', 'include', 'eh_cp_cfg_bt.h')


def _cp_target_families():
    """{IDF target: header dir} as eh_cp_cfg_bt.h selects EH_CP_BT_PWR_FAMILY_<dir>."""
    out, targets = {}, []
    with open(CP_CFG_BT) as f:
        for ln in f:
            if re.match(r'\s*#\s*(el)?if\b', ln):
                targets = re.findall(r'CONFIG_IDF_TARGET_(\w+)', ln)
                continue
            m = re.search(r'define\s+EH_CP_BT_PWR_FAMILY_(\w+)\s+1', ln)
            if m:
                for tgt in targets:
                    out[tgt] = m.group(1).lower()
    return out


def test_bt_tx_power_families_complete():
    """bt/include/ has no header directory beyond FAMILIES, eh_cp_cfg_bt.h maps every
    target to one of them, and each target that reuses another chip's dir matches IDF's CMake."""
    _need_idf()
    tf = _cp_target_families()
    assert set(tf.values()) == set(FAMILIES), f'eh_cp_cfg_bt.h families {sorted(set(tf.values()))}'
    alias = {tgt: d for tgt, d in tf.items() if tgt.lower() != d}
    assert alias, 'eh_cp_cfg_bt.h: no target alias parsed'
    refs = [None] + REFS
    checked = 0
    for ref in refs:
        if ref is None:
            dirs = set(os.listdir(os.path.join(IDF, 'components/bt/include')))
            cmake = open(os.path.join(IDF, 'components/bt/CMakeLists.txt')).read()
        else:
            r = subprocess.run(['git', '-C', IDF, 'ls-tree', '--name-only', f'{ref}:components/bt/include/'],
                               capture_output=True, text=True)
            if r.returncode:
                continue
            dirs = set(r.stdout.split())
            cmake = _git_show(ref, 'components/bt/CMakeLists.txt') or ''
        extra = dirs - set(FAMILIES) - {'common'}
        assert not extra, f'{ref or "pinned"}: new bt/include dirs {sorted(extra)}: add a family'
        for tgt, d in alias.items():
            m = re.search(rf'CONFIG_IDF_TARGET_{tgt}\)\s*\n\s*set\(\w+\s+"(\w+)"\)', cmake)
            if m:
                assert m.group(1) == d, f'{ref or "pinned"}: {tgt} uses {m.group(1)}, want {d}'
        checked += 1
    assert checked


# Host CP-target choice (host/Kconfig.ext) -> IDF header dir: the CP's own mapping.
# ESP32S2 has no BLE.
HOST_TARGETS = _cp_target_families()
HOST_INC = [os.path.join(REPO, 'host', 'compat', 'bt', 'include'),
            os.path.join(REPO, 'host', 'features', 'eh_host_feat_bt', 'include')]


def test_bt_tx_power_host_targets_complete():
    """Every BLE-capable ESP_HOSTED_CP_TARGET choice has a host table entry."""
    with open(os.path.join(REPO, 'host', 'Kconfig.ext')) as f:
        choices = set(re.findall(r'^\s*config ESP_HOSTED_CP_TARGET_(ESP32\w*)\s*$', f.read(), re.M))
    assert choices - {'ESP32S2'} == set(HOST_TARGETS)


@pytest.mark.parametrize('target', sorted(HOST_TARGETS))
def test_bt_tx_power_host_table(tmp_path, target):
    """Compat table = the CP chip's IDF esp_power_level_t; conversions both ways."""
    _need_idf()
    family = HOST_TARGETS[target]
    if family in SINCE_6_1 and _idf_version() < (6, 1):
        pytest.skip(f'{family}: BLE buildable from IDF 6.1')
    cc = _need_cc()
    with open(os.path.join(IDF, HDR.format(family))) as f:
        text = f.read()
    idf = _parse(text)
    canon = {n: v for n, (v, a) in idf.items() if not a}
    has_invalid = 'ESP_PWR_LVL_INVALID' in _enum_block(text)
    err = 255 if has_invalid else -1

    # The table reads EH_HOST_CP_TARGET_* from the master config; a stub stands in for it.
    with open(os.path.join(str(tmp_path), 'eh_host_port_master_config.h'), 'w') as f:
        f.write(f'#define EH_HOST_CP_TARGET_{target} 1\n')
    exe = os.path.join(str(tmp_path), 'dump')
    cmd = [cc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(tmp_path)]
    for d in HOST_INC:
        cmd += ['-I', d]
    subprocess.run(cmd + [os.path.join(HERE, 'test_bt_tx_power_host.c'), '-o', exe], check=True)
    lines = subprocess.run([exe], capture_output=True, text=True, check=True).stdout.splitlines()

    flags, rows, from_eh, to_eh = None, {}, {}, {}
    for ln in lines:
        p = ln.split()
        if p[0] == 'F':
            flags = tuple(int(x) for x in p[1:])
        elif p[0] in 'LA':
            assert p[1] not in rows, f'{target}: {p[1]} listed twice'
            rows[p[1]] = (int(p[2]), p[0] == 'A')
        elif p[0] == 'S':
            from_eh[p[1]] = int(p[2])
        elif p[0] == 'R':
            to_eh[int(p[1])] = int(p[2])

    # Rows: same names, values and alias marks as the IDF header.
    assert rows == idf, f'{target}: table differs from IDF {family}'
    enhanced = 'esp_ble_tx_power_set_enhanced' in _bt_api_text(family)
    assert flags == (int(has_invalid), int(enhanced), int(family in SINCE_6_1)), f'{target}: flags {flags}'

    # Name -> native: every IDF name (aliases too); any other name or INVALID -> IDF error value.
    for name, native in from_eh.items():
        want = idf[name][0] if name in idf else err
        assert native == want, f'{target}: {name} -> {native}, want {want}'

    # Native -> name: canonical name; anything else -> INVALID (255).
    by_val = {v: n for n, v in canon.items()}
    for native, sym in to_eh.items():
        want = _sym(by_val[native]) if native in by_val else 255
        assert sym == want, f'{target}: native {native} -> {sym}, want {want}'


def _enum_values(text, name):
    """{entry: value} of `typedef enum [tag] {...} name;`, implicit values counted, #if lines ignored."""
    m = re.search(r'typedef\s+enum(?:\s+\w+)?\s*\{([^{}]*)\}\s*' + name + r'\s*;', text)
    if not m:
        return None
    body = re.sub(r'/\*.*?\*/|//[^\n]*', '', m.group(1), flags=re.S)
    out, nxt = {}, 0
    for item in body.split(','):
        item = '\n'.join(l for l in item.splitlines() if not l.strip().startswith('#')).strip()
        if not item:
            continue
        k, _, v = item.partition('=')
        nxt = int(v.strip(), 0) if v.strip() else nxt
        out[k.strip()] = nxt
        nxt += 1
    return out


@pytest.mark.parametrize('family', FAMILIES)
def test_bt_tx_power_types(family):
    """Power-type enums: hosted names and values = IDF on every chip (MAX is per chip)."""
    _need_idf()
    if family in SINCE_6_1 and _idf_version() < (6, 1):
        pytest.skip(f'{family}: BLE buildable from IDF 6.1')
    text = _bt_api_text(family)
    with open(os.path.join(HOST_INC[1], 'eh_host_bt.h')) as f:
        eh = f.read()
    with open(os.path.join(HOST_INC[0], 'esp_bt.h')) as f:
        compat = f.read()
    for idf_t, eh_t in [('esp_ble_power_type_t', 'eh_ble_power_type_t'),
                        ('esp_ble_enhanced_power_type_t', 'eh_ble_enhanced_power_type_t')]:
        got = _enum_values(text, idf_t)
        if got is None:
            assert idf_t == 'esp_ble_enhanced_power_type_t' and family == 'esp32'
            continue
        mine = {k.replace('EH_', 'ESP_', 1): v for k, v in _enum_values(eh, eh_t).items()}
        comp = _enum_values(compat, idf_t)
        for k, v in got.items():
            if k.endswith('_MAX'):
                continue
            assert mine.get(k) == v, f'{family} {k}: eh_host_bt.h {mine.get(k)}, IDF {v}'
            assert comp.get(k) == v, f'{family} {k}: compat esp_bt.h {comp.get(k)}, IDF {v}'


DOC = os.path.join(REPO, 'docs', 'features', 'bluetooth.md')
DOC_BEGIN, DOC_END = '<!-- bt-tx-power-levels:begin -->', '<!-- bt-tx-power-levels:end -->'
# Doc columns: chips that share one IDF header share a column.
DOC_COLS = [('ESP32', 'esp32'), ('C2', 'esp32c2'), ('C3, S3', 'esp32c3'), ('C5', 'esp32c5'),
            ('C6, C61', 'esp32c6'), ('H2, H21', 'esp32h2'), ('H4, S31', 'esp32s31')]


def _doc_table():
    """The level table of docs/features/bluetooth.md, rebuilt from the IDF headers."""
    texts = {d: open(os.path.join(IDF, HDR.format(d))).read() for _, d in DOC_COLS}
    data = {d: _parse(x) for d, x in texts.items()}
    names = sorted({n for v in data.values() for n in v},
                   key=lambda n: (-1 if n[0] == 'N' else 1) * int(n[1:]))
    rows = ['| Level | Nominal dBm | ' + ' | '.join(c for c, _ in DOC_COLS) + ' |',
            '|---|---|' + '---|' * len(DOC_COLS)]
    for n in names:
        dbm = '0' if n == 'N0' else ('-' if n[0] == 'N' else '+') + n[1:]
        cells = [(f'{data[d][n][0]}*' if data[d][n][1] else str(data[d][n][0])) if n in data[d] else '—'
                 for _, d in DOC_COLS]
        rows.append(f'| `{n}` | {dbm} | ' + ' | '.join(cells) + ' |')
    rows.append('| `INVALID` | — | ' + ' | '.join(
        '255' if 'ESP_PWR_LVL_INVALID' in _enum_block(texts[d]) else '—' for _, d in DOC_COLS) + ' |')
    return '\n'.join(rows)


def test_bt_tx_power_doc_table():
    """docs/features/bluetooth.md shows the IDF values of every chip."""
    _need_idf()
    if _idf_version() < (6, 1):
        pytest.skip('H4/S31 column needs IDF 6.1')
    with open(DOC) as f:
        text = f.read()
    got = text.split(DOC_BEGIN, 1)[1].split(DOC_END, 1)[0].strip()
    assert got == _doc_table(), (
        'docs/features/bluetooth.md level table differs from IDF; regenerate it with\n'
        "python3 -c \"import sys; sys.path.insert(0, 'tests/unit/bt_tx_power'); "
        "import test_bt_tx_power as t; print(t._doc_table())\"")
