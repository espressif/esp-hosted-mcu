#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Check that ESP-Hosted covers every IDF Wi-Fi struct, struct field and API.

For each IDF ref, each Wi-Fi capable target and two config variants (only the
SOC caps set, and every CONFIG_* the headers test set to 1), the script
preprocesses the public Wi-Fi and supplicant headers and lists every struct,
struct field and function. The union over targets and variants is compared
with wifi_coverage_manifest.yml and with the functions hosted defines:

  structs  mapped field by field (carried / skip / gap per field, each carried
           field naming its rpc_v2.proto slot), or marked as a whole
  APIs     supported when hosted defines the function, or the esp_*_remote_*
           hook that the esp_wifi_remote forwarder calls; a stub when that
           definition only returns ESP_ERR_NOT_SUPPORTED; otherwise the
           manifest must classify it (local / unsupported / gap / skip)

A manifest entry that the checked IDF does not have is fine: hosted may cover
more than that IDF version offers.

Exit codes: 0 = pass, 1 = coverage or manifest drift, 2 = setup, parse or
schema error (never a pass).

Usage:
  check_wifi_coverage.py --refs-file idf_refs.yml [--live] [--html report.html]
  check_wifi_coverage.py --idf $IDF_PATH
  check_wifi_coverage.py --idf-repo ~/esp-idf --ref origin/master --ref v6.0.3
  check_wifi_coverage.py --idf $IDF_PATH --dump wifi_sta_config_t
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

try:
    import yaml
    from pycparser import c_ast, c_parser
except ImportError as e:
    sys.exit(f'error: {e}. Run: pip install pycparser pyyaml')

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
DEFAULT_MANIFEST = HERE / 'wifi_coverage_manifest.yml'
DEFAULT_REFS = HERE / 'idf_refs.yml'
PROTO = REPO_ROOT / 'common/serializers/eh_proto_v2/src/proto/rpc_v2.proto'
HOST_BITS = REPO_ROOT / 'common/eh_common/include/eh_rpc_bitmasks.h'
CP_BITS = REPO_ROOT / 'coprocessor/features/eh_cp_feat_rpc_ext_v2/include/eh_cp_feat_rpc_ext_v2_bitmasks.h'
HOST_BIT_PREFIX = 'EH_HOST_'
# Hosted sources that may define Wi-Fi APIs; the bundled esp_wifi_remote copy is
# upstream code (its weak defaults are exactly what "not supported" means).
HOSTED_SRC_DIRS = ('host', 'port')
HOSTED_SRC_EXCLUDE = ('port/idf_components/esp_wifi_remote/esp_wifi_remote', 'third_party')

WIFI_INC = 'components/esp_wifi/include'
SUPP_INC = 'components/wpa_supplicant/esp_supplicant/include'
REMOTE_DIR = 'components/esp_wifi/remote'
SOC_DIR = 'components/soc'
VERSION_H = 'components/esp_common/include/esp_idf_version.h'
# Display order of the API headers; every public header in the Wi-Fi and
# supplicant include directories is checked, listed here or not.
API_HEADERS = ('esp_wifi.h', 'esp_wifi_he.h', 'esp_wifi_default.h', 'esp_wifi_netif.h',
               'esp_wifi_ap_get_sta_list.h', 'esp_now.h', 'esp_smartconfig.h', 'smartconfig_ack.h',
               'esp_mesh.h', 'esp_mesh_internal.h', 'esp_eap_client.h', 'esp_wpa2.h', 'esp_dpp.h', 'esp_wps.h',
               'esp_wnm.h', 'esp_rrm.h', 'esp_mbo.h', 'esp_supplicant_utils.h', 'esp_wpa.h')
# Headers that wifi-remote forwards (esp_X -> esp_X_remote hook) when the IDF
# tree has no in-tree forwarder file to read.
FORWARDED_HEADERS = {'esp_wifi.h': ('esp_wifi_', 'esp_wifi_remote_'),
                     'esp_eap_client.h': ('esp_eap_client_', 'esp_eap_client_remote_')}
# Struct every IDF ref has; its absence means the parse is broken.
SENTINELS = ('wifi_init_config_t', 'wifi_sta_config_t', 'wifi_ap_config_t')
SENTINEL_APIS = ('esp_wifi_init', 'esp_wifi_set_config')
RESERVED_RE = re.compile(r'^(he_)?reserved\d*$')
CONFIG_RE = re.compile(r'^\s*#\s*(?:if|ifdef|ifndef|elif)\b(.*)$', re.M)

EXIT_DRIFT = 1
EXIT_SETUP = 2


class SetupError(Exception):
    pass


# Minimal stand-ins for the headers the Wi-Fi headers pull in from other
# components. Only types used in prototypes are needed.
STUBS = {
    'prelude.h': '''
#define __attribute__(x)
#define __asm__(x)
#define __volatile__(...)
#define __inline__ inline
#define __inline inline
#define __extension__
#define __typeof__(x) void*
#define bool _Bool
''',
    'stdint.h': '''#pragma once
typedef signed char int8_t; typedef unsigned char uint8_t;
typedef short int16_t; typedef unsigned short uint16_t;
typedef int int32_t; typedef unsigned int uint32_t;
typedef long long int64_t; typedef unsigned long long uint64_t;
typedef int intptr_t; typedef unsigned int uintptr_t;
#define UINT8_MAX 255
#define UINT16_MAX 65535
#define UINT32_MAX 4294967295U
#define INT8_MAX 127
#define INT8_MIN (-128)
''',
    'stdbool.h': '#pragma once\n#define true 1\n#define false 0\n',
    'stddef.h': '#pragma once\ntypedef unsigned int size_t;\n#define NULL ((void*)0)\n',
    'stdarg.h': '#pragma once\ntypedef void *va_list;\n',
    'esp_err.h': '#pragma once\n#include <stdint.h>\ntypedef int esp_err_t;\n'
                 '#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_WIFI_BASE 0x3000\n',
    'esp_event_base.h': '#pragma once\ntypedef const char *esp_event_base_t;\n'
                        '#define ESP_EVENT_DECLARE_BASE(id) extern esp_event_base_t const id\n',
    'esp_event.h': '#pragma once\n#include "esp_event_base.h"\n',
    'esp_interface.h': '#pragma once\ntypedef enum { ESP_IF_WIFI_STA = 0, ESP_IF_WIFI_AP, '
                       'ESP_IF_WIFI_NAN, ESP_IF_ETH, ESP_IF_MAX } esp_interface_t;\n',
    'esp_netif.h': '#pragma once\n#include "esp_netif_types.h"\n',
    'esp_netif_types.h': '#pragma once\n#include <stdint.h>\n'
                         'typedef struct esp_netif_obj esp_netif_t;\n'
                         'typedef struct esp_netif_inherent_config esp_netif_inherent_config_t;\n'
                         'typedef struct esp_netif_config esp_netif_config_t;\n'
                         'typedef void *esp_netif_iodriver_handle;\n'
                         'typedef struct { uint32_t addr; } esp_ip4_addr_t;\n'
                         'typedef struct { uint32_t addr[4]; uint8_t zone; } esp_ip6_addr_t;\n'
                         '#include "esp_err.h"\n#include <stddef.h>\n'
                         'typedef struct { uint8_t mac[6]; esp_ip4_addr_t ip; } esp_netif_pair_mac_ip_t;\n'
                         'typedef esp_err_t (*esp_netif_receive_t)(void *, void *, size_t, void *);\n',
    'esp_compiler.h': '#pragma once\n#define likely(x) (x)\n#define unlikely(x) (x)\n',
    'esp_private/wifi.h': '#pragma once\n',
    'lwip/ip_addr.h': '#pragma once\n#include <stdint.h>\n'
                      'typedef struct { uint32_t addr; } ip4_addr_t;\n'
                      'typedef struct { uint32_t addr[4]; } ip6_addr_t;\n'
                      'typedef struct { ip4_addr_t u_addr; uint8_t type; } ip_addr_t;\n',
}


# ───────────────────────────── IDF sources ─────────────────────────────

class IdfTree:
    """One IDF ref, read from a working tree or from a git object."""

    def __init__(self, name, root=None, repo=None, rev=None):
        self.name, self.root, self.repo, self.rev = name, root, repo, rev

    def _git(self, *args):
        r = subprocess.run(['git', '-C', str(self.repo), *args], capture_output=True, text=True)
        if r.returncode:
            raise SetupError(f'{self.name}: git {" ".join(args)}: {r.stderr.strip()}')
        return r.stdout

    def read(self, rel):
        if self.root:
            p = Path(self.root) / rel
            if not p.is_file():
                raise SetupError(f'{self.name}: missing {p}')
            return p.read_text(errors='replace')
        return self._git('show', f'{self.rev}:{rel}')

    def list_dir(self, rel):
        if self.root:
            p = Path(self.root) / rel
            if not p.is_dir():
                raise SetupError(f'{self.name}: missing {p}')
            return sorted(x.name for x in p.iterdir())
        out = self._git('ls-tree', '--name-only', f'{self.rev}:{rel}')
        return sorted(out.split())

    def exists(self, rel):
        if self.root:
            return (Path(self.root) / rel).exists()
        r = subprocess.run(['git', '-C', str(self.repo), 'cat-file', '-e', f'{self.rev}:{rel}'],
                           capture_output=True)
        return r.returncode == 0

    def version(self):
        text = self.read(VERSION_H)
        parts = []
        for k in ('MAJOR', 'MINOR', 'PATCH'):
            m = re.search(rf'#define\s+ESP_IDF_VERSION_{k}\s+(\d+)', text)
            if not m:
                raise SetupError(f'{self.name}: no ESP_IDF_VERSION_{k} in {VERSION_H}')
            parts.append(int(m.group(1)))
        return tuple(parts)

    def export_headers(self, dst):
        """Copy the Wi-Fi and supplicant public headers into dst; return ({name: text})."""
        texts = {}
        for rel, sub, required in ((WIFI_INC, '', True), (f'{WIFI_INC}/local', 'local', False),
                                   (SUPP_INC, 'supp', False)):
            if not required and not self.exists(rel):
                continue
            out = Path(dst) / sub
            out.mkdir(parents=True, exist_ok=True)
            for name in self.list_dir(rel):
                if name.endswith('.h'):
                    text = self.read(f'{rel}/{name}')
                    (out / name).write_text(text)
                    if name in texts:
                        raise SetupError(f'{self.name}: header {name} exists twice in the include dirs')
                    texts[name] = text
        return texts

    def forwarders(self):
        """Names the in-tree esp_wifi_remote forwards, or None when the tree has none."""
        names = set()
        for f in ('esp_wifi_with_remote.c', 'esp_eap_client_with_remote.c'):
            rel = f'{REMOTE_DIR}/{f}'
            if self.exists(rel):
                names |= set(definitions(self.read(rel)))
        return names or None

    def wifi_targets(self):
        """Map target -> {CONFIG_*: value} for every target with SOC_WIFI_SUPPORTED."""
        res = {}
        for t in self.list_dir(SOC_DIR):
            rel = f'{SOC_DIR}/{t}/include/soc/Kconfig.soc_caps.in'
            if not self.exists(rel):
                continue
            caps = parse_soc_caps(self.read(rel))
            if caps.get('CONFIG_SOC_WIFI_SUPPORTED') == '1':
                caps[f'CONFIG_IDF_TARGET_{t.upper()}'] = '1'
                caps['CONFIG_IDF_TARGET'] = f'"{t}"'
                res[t] = caps
        if not res:
            raise SetupError(f'{self.name}: no Wi-Fi capable target under {SOC_DIR}')
        return res


def parse_soc_caps(text):
    cfg, name = {}, None
    for line in text.splitlines():
        s = line.strip()
        m = re.match(r'config\s+(\w+)$', s)
        if m:
            name = m.group(1)
            continue
        m = re.match(r'default\s+(.+)$', s)
        if m and name:
            v = m.group(1).strip()
            if v == 'y':
                cfg['CONFIG_' + name] = '1'
            elif v != 'n':
                cfg['CONFIG_' + name] = v
            name = None
    return cfg


def feature_configs(header_texts):
    """CONFIG_* names the headers test, except SOC caps and target names (those come per target)."""
    names = set()
    for text in header_texts:
        for cond in CONFIG_RE.findall(text):
            names.update(re.findall(r'\bCONFIG_\w+', cond))
    return sorted(n for n in names if not n.startswith(('CONFIG_SOC_', 'CONFIG_IDF_TARGET')))


# ───────────────────────────── C parsing ─────────────────────────────

def find_cpp():
    cands = [os.environ.get('CC'), 'cc', 'gcc', 'clang', 'riscv32-esp-elf-gcc', 'xtensa-esp32-elf-gcc']
    for c in cands:
        if c and shutil.which(c.split()[0]):
            return c.split()
    raise SetupError('no C preprocessor found (set CC)')


def preprocess(inc_dir, stub_dir, cfg, cpp, headers):
    (Path(stub_dir) / 'sdkconfig.h').write_text(
        ''.join(f'#define {k} {v}\n' for k, v in sorted(cfg.items())))
    inc = Path(inc_dir)
    top = Path(stub_dir) / 'all_wifi_headers.c'
    top.write_text(''.join(f'#include "{h}"\n' for h in sorted(headers)))
    cmd = cpp + ['-E', '-w', '-nostdinc', '-undef', '-x', 'c',
                 '-include', str(Path(stub_dir) / 'prelude.h'),
                 '-I', str(inc), '-I', str(inc / 'local'), '-I', str(inc / 'supp'), '-I', str(stub_dir),
                 str(top)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise SetupError(f'preprocess failed: {r.stderr.strip()[:2000]}')
    # gcc/clang line markers carry flags pycparser does not take; keep only file and line.
    return re.sub(r'^# (\d+) ("[^"]*").*$', r'#line \1 \2', r.stdout, flags=re.M)


class TypeTable:
    """Typedefs, struct tags and function declarations from one translation unit."""

    def __init__(self, text, headers):
        self.headers = headers
        try:
            ast = c_parser.CParser().parse(text)
        except Exception as e:  # pycparser raises its own ParseError types
            raise SetupError(f'parse failed: {e}')
        self.typedefs, self.tags, self.origin, self.funcs = {}, {}, {}, {}
        for ext in ast.ext:
            header = Path(ext.coord.file).name if ext.coord and ext.coord.file else ''
            if isinstance(ext, c_ast.Typedef):
                self.typedefs[ext.name] = ext.type
                self.origin[ext.name] = header
                self._tag(ext.type)
            elif isinstance(ext, c_ast.Decl):
                self._tag(ext.type)
                if isinstance(ext.type, c_ast.FuncDecl) and header in headers:
                    self.funcs[ext.name] = header

    def _tag(self, node):
        t = node.type if isinstance(node, c_ast.TypeDecl) else node
        if isinstance(t, (c_ast.Struct, c_ast.Union)) and t.name and t.decls is not None:
            self.tags[t.name] = t

    def composite(self, node, seen=None):
        """Resolve a type node to (Struct|Union with body, typedef name) or (None, None)."""
        seen = seen or set()
        if isinstance(node, c_ast.TypeDecl):
            node = node.type
        if isinstance(node, (c_ast.Struct, c_ast.Union)):
            if node.decls is not None:
                return node, None
            return self.tags.get(node.name), None
        if isinstance(node, c_ast.IdentifierType):
            name = ' '.join(node.names)
            if name in self.typedefs and name not in seen:
                seen.add(name)
                body, _ = self.composite(self.typedefs[name], seen)
                return body, name
        return None, None

    def struct(self, name):
        if name not in self.typedefs:
            return None
        body, _ = self.composite(self.typedefs[name])
        return body

    def structs(self):
        """{typedef name: header} for every struct/union typedef from the checked headers."""
        return {n: self.origin[n] for n in self.typedefs
                if self.origin.get(n) in self.headers and self.struct(n) is not None}


def leaf_type(node):
    """Strip arrays; pointers and function types stay leaves."""
    while isinstance(node, c_ast.ArrayDecl):
        node = node.type
    return node


def flatten(table, body, entry_fields, manifest_structs, prefix=''):
    """Yield dotted field paths. Nested manifest structs and skipped subtrees stay one path."""
    for d in body.decls or []:
        t = leaf_type(d.type)
        sub, tname = (None, None) if isinstance(t, (c_ast.PtrDecl, c_ast.FuncDecl)) \
            else table.composite(t)
        if not d.name:
            if sub is not None:  # anonymous struct/union: members belong to the parent
                yield from flatten(table, sub, entry_fields, manifest_structs, prefix)
            continue  # unnamed padding bitfield
        if RESERVED_RE.match(d.name):
            continue
        path = prefix + d.name
        if sub is None or tname in manifest_structs:
            yield path
            continue
        status = entry_fields.get(path)
        if isinstance(status, dict) and ('skip' in status or 'gap' in status):
            yield path
            continue
        yield from flatten(table, sub, entry_fields, manifest_structs, path + '.')


# ───────────────────────────── hosted sources ─────────────────────────────

# Return type on the same line or on the line before; a call never ends in '{'.
DEF_RE = re.compile(r'^(?:[A-Za-z_][\w \t\*]*?[\s\*])?(esp_\w+)\s*\(([^;{}]*?)\)\s*\{', re.M)
STUB_RE = re.compile(r'\s*(UNSUPPORTED_RET\s*\(\s*\)|return\s+ESP_ERR_NOT_SUPPORTED)\s*;\s*')


def strip_c(text):
    """Drop comments and #if 0 blocks; keep line count."""
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    out, skip = [], 0
    for line in text.split('\n'):
        s = line.strip()
        if skip:
            if re.match(r'#\s*if', s):
                skip += 1
            elif re.match(r'#\s*endif', s):
                skip -= 1
            elif skip == 1 and re.match(r'#\s*(else|elif)', s):
                skip = 0
            out.append('')
            continue
        if re.match(r'#\s*if\s+0\b', s):
            skip = 1
            out.append('')
            continue
        out.append(line)
    return '\n'.join(out)


def definitions(text):
    """{function name: 'impl' | 'stub'} for esp_* functions defined in C source text."""
    text = strip_c(text)
    res = {}
    for m in DEF_RE.finditer(text):
        i, depth = m.end(), 1
        while depth and i < len(text):
            depth += {'{': 1, '}': -1}.get(text[i], 0)
            i += 1
        body = re.sub(r'\(\s*void\s*\)\s*\w+\s*;', '', text[m.end():i - 1])
        kind = 'stub' if STUB_RE.fullmatch(body) else 'impl'
        # A name defined twice under different #if branches counts as implemented if any branch is.
        if res.get(m.group(1)) != 'impl':
            res[m.group(1)] = kind
    return res


def hosted_definitions(root=REPO_ROOT):
    res = {}
    for d in HOSTED_SRC_DIRS:
        for f in sorted((Path(root) / d).rglob('*.c')):
            rel = f.relative_to(root).as_posix()
            if any(rel.startswith(x) or f'/{x}/' in f'/{rel}' for x in HOSTED_SRC_EXCLUDE):
                continue
            for name, kind in definitions(f.read_text(errors='replace')).items():
                if res.get(name) != 'impl':
                    res[name] = kind
    return res


# ───────────────────────────── manifest ─────────────────────────────

WHOLE_KINDS = ('unsupported', 'skip', 'gap')
API_KINDS = ('local', 'unsupported', 'gap', 'skip')


class _UniqueKeyLoader(yaml.SafeLoader):
    def construct_mapping(self, node, deep=False):
        keys = set()
        for k, _ in node.value:
            key = self.construct_object(k, deep=deep)
            if key in keys:
                raise yaml.constructor.ConstructorError(
                    None, None, f'duplicate key {key}', k.start_mark)
            keys.add(key)
        return super().construct_mapping(node, deep)


def whole_kind(entry, kinds=WHOLE_KINDS):
    return next((k for k in kinds if k in entry), None)


def _check_kind_entry(where, entry, kinds, extra_keys, errs):
    found = [k for k in kinds if k in entry]
    if len(found) != 1:
        errs.append(f'{where}: needs exactly one of {"/".join(kinds)} with a reason')
        return
    if not str(entry[found[0]]).strip():
        errs.append(f'{where}: {found[0]} needs a reason')
    extra = set(entry) - set(found) - set(extra_keys)
    if extra:
        errs.append(f'{where}: unknown keys {sorted(extra)}')


def _check_field(where, s, f, v, errs):
    if v == 'carried':
        if not s.get('proto'):
            errs.append(f'{where}: "carried" needs struct-level proto')
        elif '.' in f:
            errs.append(f'{where}: dotted path needs an explicit proto')
        return
    if not isinstance(v, dict):
        errs.append(f'{where}: expected "carried" or a mapping, got {v!r}')
        return
    kinds = [k for k in ('proto', 'skip', 'gap') if k in v]
    if len(kinds) != 1:
        errs.append(f'{where}: needs exactly one of proto/skip/gap')
    extra = set(v) - {'proto', 'skip', 'gap', 'bit', 'note'}
    if extra:
        errs.append(f'{where}: unknown keys {sorted(extra)}')
    if 'bit' in v and 'proto' not in v:
        errs.append(f'{where}: bit needs proto')
    for k in ('skip', 'gap'):
        if k in v and not str(v[k]).strip():
            errs.append(f'{where}: {k} needs a reason')


def load_manifest(path):
    """Return {'headers': {...}, 'structs': {...}, 'apis': {...}}."""
    try:
        data = yaml.load(Path(path).read_text(), Loader=_UniqueKeyLoader)
    except (OSError, yaml.YAMLError) as e:
        raise SetupError(f'manifest {path}: {e}')
    if not isinstance(data, dict) or not isinstance(data.get('structs'), dict):
        raise SetupError(f'manifest {path}: top level needs a "structs" mapping')
    unknown = set(data) - {'headers', 'structs', 'apis'}
    errs = [f'unknown top-level keys {sorted(unknown)}'] if unknown else []
    headers, apis = data.get('headers') or {}, data.get('apis') or {}
    for h, e in headers.items():
        if not re.fullmatch(r'\w+\.h', str(h)):
            errs.append(f'headers.{h}: expected a header file name')
        if not isinstance(e, dict):
            errs.append(f'headers.{h}: expected a mapping')
            continue
        _check_kind_entry(f'headers.{h}', e, API_KINDS, ('note',), errs)
    for a, e in apis.items():
        if not isinstance(e, dict):
            errs.append(f'apis.{a}: expected a mapping')
            continue
        _check_kind_entry(f'apis.{a}', e, API_KINDS, ('note',), errs)
    owners = {}
    for sname, s in data['structs'].items():
        if not isinstance(s, dict):
            errs.append(f'{sname}: expected a mapping')
            continue
        for n in [sname] + list(s.get('aliases') or []):
            if n in owners and owners[n] != sname:
                errs.append(f'{sname}: name {n} is also claimed by {owners[n]}')
            owners[n] = sname
        if 'since' in s and not re.fullmatch(r'\d+\.\d+(\.\d+)?', str(s['since'])):
            errs.append(f'{sname}: since must look like 5.4 or 5.4.1')
        kind = whole_kind(s)
        if kind:
            extra = set(s) - {kind, 'since', 'aliases', 'note'}
            if extra or sum(k in s for k in WHOLE_KINDS) != 1:
                errs.append(f'{sname}: a whole-struct {kind} entry takes only since/aliases/note, '
                            f'not {sorted(extra) or "a second status"}')
            if not str(s[kind]).strip():
                errs.append(f'{sname}: {kind} needs a reason')
            continue
        if not isinstance(s.get('fields'), dict):
            errs.append(f'{sname}: needs a "fields" mapping, or unsupported/skip/gap with a reason')
            continue
        bad = set(s) - {'proto', 'fields', 'since', 'aliases', 'note'}
        if bad:
            errs.append(f'{sname}: unknown keys {sorted(bad)}')
        for f, v in s['fields'].items():
            _check_field(f'{sname}.{f}', s, f, v, errs)
    if errs:
        raise SetupError('manifest schema errors:\n  ' + '\n  '.join(errs))
    return {'headers': headers, 'structs': data['structs'], 'apis': apis}


def proto_slot(sentry, fname, fval):
    if fval == 'carried':
        return f'{sentry["proto"]}.{fname}', None
    return fval.get('proto'), fval.get('bit')


def proto_fields(text):
    text = strip_c(text)
    res = set()
    for m in re.finditer(r'\bmessage\s+(\w+)\s*\{', text):
        depth, i = 1, m.end()
        while depth and i < len(text):
            depth += {'{': 1, '}': -1}.get(text[i], 0)
            i += 1
        body = text[m.end():i - 1]
        for f in re.finditer(r'(?:repeated\s+|optional\s+)?[\w.]+\s+(\w+)\s*=\s*\d+\s*;', body):
            res.add(f'{m.group(1)}.{f.group(1)}')
    return res


def check_wire_claims(structs, proto_text, host_bits_text, cp_bits_text):
    pf = proto_fields(proto_text)
    host_ids = set(re.findall(r'\b\w+\b', strip_c(host_bits_text)))
    cp_ids = set(re.findall(r'\b\w+\b', strip_c(cp_bits_text)))
    errs = []
    for sname, s in structs.items():
        for fname, fval in (s.get('fields') or {}).items():
            slot, bit = proto_slot(s, fname, fval)
            if slot is None:
                continue
            if slot not in pf:
                errs.append(f'{sname}.{fname}: proto field {slot} not in rpc_v2.proto')
            if bit:
                if bit not in cp_ids:
                    errs.append(f'{sname}.{fname}: bit {bit} not in CP bitmask header')
                if HOST_BIT_PREFIX + bit not in host_ids:
                    errs.append(f'{sname}.{fname}: bit {HOST_BIT_PREFIX}{bit} not in host bitmask header')
    return errs


# ───────────────────────────── scan ─────────────────────────────

def ver_tuple(s):
    p = [int(x) for x in str(s).split('.')]
    return tuple(p + [0] * (3 - len(p)))


def scan_ref(tree, manifest, cpp):
    """Parse one ref for every Wi-Fi target and both config variants.

    Returns {'version', 'targets': {target: {'structs': {name: header}, 'present': {entry: name},
    'fields': {entry: paths}, 'apis': {name: header}}}, 'forwarders': set or None}.
    """
    structs = manifest['structs']
    version = tree.version()
    targets = tree.wifi_targets()
    res = {}
    with tempfile.TemporaryDirectory() as td:
        inc, stub = Path(td) / 'inc', Path(td) / 'stub'
        for n, body in STUBS.items():
            (stub / n).parent.mkdir(parents=True, exist_ok=True)
            (stub / n).write_text(body)
        texts = tree.export_headers(inc)
        headers = set(texts)
        features = {k: '1' for k in feature_configs(texts.values())}
        for t, caps in sorted(targets.items()):
            agg = {'structs': {}, 'present': {}, 'fields': defaultdict(set), 'apis': {}}
            for variant in (caps, {**features, **caps}):
                try:
                    text = preprocess(inc, stub, variant, cpp, headers)
                    table = TypeTable(text, headers)
                except SetupError as e:
                    raise SetupError(f'{tree.name} [{t}]: {e}')
                seen = {Path(f).name for f in re.findall(r'^#line \d+ "([^"]*)"', text, re.M)}
                if headers - seen:
                    raise SetupError(f'{tree.name} [{t}]: headers not parsed: {sorted(headers - seen)}')
                missing = [s for s in SENTINELS if table.struct(s) is None]
                missing += [a for a in SENTINEL_APIS if a not in table.funcs]
                if missing:
                    raise SetupError(f'{tree.name} [{t}]: sentinel structs/APIs not found: {missing}')
                agg['structs'].update(table.structs())
                agg['apis'].update(table.funcs)
                for sname, s in structs.items():
                    for cand in [sname] + list(s.get('aliases') or []):
                        body = table.struct(cand)
                        if body is None:
                            continue
                        agg['present'][sname] = cand
                        if not whole_kind(s):
                            agg['fields'][sname] |= set(flatten(table, body, s['fields'], set(structs)))
                        break
            agg['fields'] = dict(agg['fields'])
            res[t] = agg
    return {'version': version, 'targets': res, 'forwarders': tree.forwarders()}


# ───────────────────────────── analysis ─────────────────────────────

def field_status(fval):
    if fval is None:
        return 'missing', ''
    if fval == 'carried':
        return 'carried', ''
    for k in ('skip', 'gap'):
        if k in fval:
            return k, str(fval[k])
    return 'carried', str(fval.get('note', ''))


def wire_slot(sentry, fname, fval):
    if fval is None or (isinstance(fval, dict) and 'proto' not in fval):
        return ''
    slot, bit = proto_slot(sentry, fname, fval)
    return f'{slot} [{bit}]' if bit else slot


def ref_label(name):
    """'origin/release/v5.5' -> 'v5.5'; paths and other refs keep their last part."""
    return re.sub(r'^.*?(release/)', '', name).rstrip('/').split('/')[-1] or name


def short_targets(ts):
    return ','.join(t[5:] if t.startswith('esp32') and len(t) > 5 else t for t in ts)


def _new_struct(name, kind, reason, header, refs):
    return {'name': name, 'kind': kind, 'reason': reason, 'header': header, 'proto': '',
            'presence': {r: [] for r in refs}, 'fields': {}, 'no_struct': []}


def build_struct_model(manifest, scans, refs):
    structs = manifest['structs']
    classified = set(structs)
    for s in structs.values():
        classified.update(s.get('aliases') or [])
    res = {}
    for sname, s in structs.items():
        kind = whole_kind(s) or 'fields'
        st = _new_struct(sname, kind, str(s.get(kind, '')) if kind != 'fields' else str(s.get('note', '')),
                         '', refs)
        st['proto'] = s.get('proto', '')
        for ref, scan in scans:
            st['presence'][ref] = sorted(t for t, d in scan['targets'].items() if sname in d['present'])
            for d in scan['targets'].values():
                if sname in d['present']:
                    st['header'] = d['structs'].get(d['present'][sname], st['header'])
        if kind == 'fields':
            paths = list(s['fields'])
            for ref, scan in scans:
                for d in scan['targets'].values():
                    paths += sorted(d['fields'].get(sname, ()))
            for p in dict.fromkeys(paths):
                fval = s['fields'].get(p)
                status, reason = field_status(fval)
                st['fields'][p] = {
                    'status': status, 'reason': reason, 'slot': wire_slot(s, p, fval),
                    'presence': {ref: sorted(t for t, d in scan['targets'].items()
                                             if p in d['fields'].get(sname, ())) for ref, scan in scans}}
            for ref, scan in scans:
                since = s.get('since')
                if not st['presence'][ref] and not (since and scan['version'] < ver_tuple(since)):
                    st['no_struct'].append(ref)
        res[sname] = st
    headers = manifest['headers']
    for ref, scan in scans:
        for t, d in scan['targets'].items():
            for n, header in d['structs'].items():
                if n in classified:
                    continue
                hentry = headers.get(header)
                if n not in res:
                    if hentry:
                        kind = 'skip' if whole_kind(hentry, API_KINDS) == 'local' else whole_kind(hentry, API_KINDS)
                        res[n] = _new_struct(n, kind, str(hentry[whole_kind(hentry, API_KINDS)]), header, refs)
                        res[n]['by_header'] = True
                    else:
                        res[n] = _new_struct(n, 'new', 'not in the manifest', header, refs)
                res[n]['presence'][ref].append(t)
    for st in res.values():
        st['presence'] = {r: sorted(set(ts)) for r, ts in st['presence'].items()}
    return res


def api_status(name, header, scan, defs, manifest):
    """(status, reason, source) of one API in one ref; source is code / api / header / none."""
    hosted = defs.get(name)
    if hosted is None and header in FORWARDED_HEADERS:
        fwd = scan['forwarders']
        if fwd is None or name in fwd:
            for idf_prefix, remote_prefix in FORWARDED_HEADERS.values():
                if name.startswith(idf_prefix):
                    hosted = defs.get(remote_prefix + name[len(idf_prefix):])
                    break
    entry = manifest['apis'].get(name)
    if hosted == 'impl':
        if entry:
            kind = whole_kind(entry, API_KINDS)
            if kind != 'local':
                return 'stale', f'hosted implements it, but the manifest says {kind}: {entry[kind]}', 'api'
        return 'supported', '', 'code'
    if hosted == 'stub':
        return 'stub', 'hosted returns ESP_ERR_NOT_SUPPORTED', 'code'
    source = 'api' if entry else 'header'
    entry = entry or manifest['headers'].get(header)
    if entry:
        kind = whole_kind(entry, API_KINDS)
        return kind, str(entry[kind]), source
    return 'missing', '', 'none'


def build_api_model(manifest, scans, defs, refs):
    res = {}
    for ref, scan in scans:
        for t, d in scan['targets'].items():
            for name, header in d['apis'].items():
                api = res.setdefault(name, {'name': name, 'header': header, 'status': {}, 'reason': {},
                                            'source': {}, 'presence': {r: [] for r in refs}})
                api['presence'][ref].append(t)
        for api in res.values():
            if api['presence'][ref]:
                api['status'][ref], api['reason'][ref], api['source'][ref] = api_status(
                    api['name'], api['header'], scan, defs, manifest)
    for api in res.values():
        api['presence'] = {r: sorted(set(ts)) for r, ts in api['presence'].items()}
    return res


def build_model(manifest, scans, wire_errs, defs):
    """Everything both reports need; no printing here."""
    refs = [{'name': r, 'label': ref_label(r), 'version': '.'.join(map(str, s['version'])),
             'ver': s['version'], 'targets': sorted(s['targets'])} for r, s in scans]
    names = [r['name'] for r in refs]
    apis = build_api_model(manifest, scans, defs, names)
    unused = sorted(a for a in manifest['apis'] if a not in apis)
    return {'refs': refs, 'structs': build_struct_model(manifest, scans, names), 'apis': apis,
            'wire_errs': wire_errs, 'unused_api_entries': unused, 'api_entries': set(manifest['apis'])}


def problems(model):
    """Everything that fails the check."""
    miss, new, gone = [], [], []
    for st in model['structs'].values():
        if st['kind'] == 'new':
            new.append(st)
        for ref in st['no_struct']:
            gone.append((st, ref))
        for p, f in st['fields'].items():
            if f['status'] == 'missing' and any(f['presence'].values()):
                miss.append((st, p, f))
    api_bad = [a for a in model['apis'].values() if {'missing', 'stale'} & set(a['status'].values())]
    return miss, new, gone, api_bad


def problem_count(model):
    miss, new, gone, api_bad = problems(model)
    return len(miss) + len(new) + sum(1 for st in model['structs'].values() if st['no_struct']) \
        + len(api_bad) + len(model['wire_errs'])


def struct_ref_status(st, ref):
    """Struct status in one ref: missing / gap / carried, from the fields that ref has."""
    seen = [f['status'] for f in st['fields'].values() if f['presence'].get(ref)]
    for s in ('missing', 'gap'):
        if s in seen:
            return s
    return 'carried'


def struct_summary(st):
    """One word for a struct row: missing / gap / carried / skip / unsupported / new / absent."""
    if st['kind'] != 'fields':
        return st['kind']
    seen = [f for f in st['fields'].values() if any(f['presence'].values())]
    if st['no_struct']:
        return 'missing'
    for s in ('missing', 'gap'):
        if any(f['status'] == s for f in seen):
            return s
    return 'carried' if seen else 'absent'


API_RANK = ('missing', 'stale', 'gap', 'stub', 'unsupported', 'skip', 'local', 'supported')


def api_summary(api):
    vals = set(api['status'].values())
    return next(s for s in API_RANK if s in vals)


# ───────────────────────────── CLI report ─────────────────────────────

COLORS = {'red': '31', 'yellow': '33', 'green': '32', 'dim': '2'}


class Painter:
    def __init__(self, on):
        self.on = on

    def __call__(self, text, color):
        return f'\033[{COLORS[color]}m{text}\033[0m' if self.on else text


def where(presence, model, only=None):
    """'v5.5, master [all targets]; v6.1 [c5]' for the refs an element is in (optionally only some refs)."""
    groups = {}
    for r in model['refs']:
        ts = presence.get(r['name']) or []
        if not ts or (only is not None and r['name'] not in only):
            continue
        spec = 'all targets' if ts == r['targets'] else short_targets(ts)
        groups.setdefault(spec, []).append(r['label'])
    if len(groups) == 1 and len(next(iter(groups.values()))) == len(model['refs']) > 1:
        return f'all refs [{next(iter(groups))}]'
    return '; '.join(f'{", ".join(refs)} [{spec}]' for spec, refs in groups.items())


def header_order(model):
    """Headers that have structs or APIs: failing ones first, then API_HEADERS order, then by name."""
    bad = set()
    miss, new, gone, api_bad = problems(model)
    bad |= {st['header'] for st, _, _ in miss} | {st['header'] for st in new}
    bad |= {st['header'] for st, _ in gone} | {a['header'] for a in api_bad}
    names = {st['header'] for st in model['structs'].values()} | {a['header'] for a in model['apis'].values()}
    return sorted(names, key=lambda h: (h not in bad, API_HEADERS.index(h) if h in API_HEADERS else 99, h))


def file_rows(model):
    """{header: [(tag, color, subject, where, reason)]}, problems first in each header."""
    rows = defaultdict(list)
    vers = {r['name']: r['version'] for r in model['refs']}
    miss, new, gone, api_bad = problems(model)
    for st, p, f in miss:
        rows[st['header']].append(('MISSING', 'red', f'{st["name"]}.{p}', where(f['presence'], model), ''))
    for st in new:
        rows[st['header']].append(('NEW STRUCT', 'red', st['name'], where(st['presence'], model),
                                   'classify it in the manifest'))
    for st in model['structs'].values():
        if st['no_struct']:
            refs = ', '.join(f'{ref_label(r)} ({vers[r]})' for r in st['no_struct'])
            rows[st['header']].append(('NO STRUCT', 'red', st['name'], refs, 'renamed? add aliases or since'))
    for api in api_bad:
        for status, tag in (('missing', 'MISSING API'), ('stale', 'STALE API')):
            refs = [r for r, v in api['status'].items() if v == status]
            if refs:
                reason = api['reason'][refs[0]] or 'hosted does not define it'
                rows[api['header']].append((tag, 'red', f'{api["name"]}()', where(api['presence'], model, refs),
                                            reason))
    for api in model['apis'].values():
        for status, tag in (('gap', 'GAP'), ('stub', 'STUB')):
            refs = [r for r, v in api['status'].items() if v == status]
            if refs and not (status == 'gap' and api['source'][refs[0]] == 'header'):
                rows[api['header']].append((tag, 'yellow', f'{api["name"]}()', where(api['presence'], model, refs),
                                            api['reason'][refs[0]]))
    for st in model['structs'].values():
        if st['kind'] == 'gap' and not st.get('by_header'):
            rows[st['header']].append(('GAP', 'yellow', st['name'], where(st['presence'], model), st['reason']))
        for p, f in st['fields'].items():
            if f['status'] == 'gap':
                rows[st['header']].append(('GAP', 'yellow', f'{st["name"]}.{p}', where(f['presence'], model),
                                           f['reason']))
    for st in model['structs'].values():
        for p, f in st['fields'].items():
            if f['status'] == 'skip':
                rows[st['header']].append(('SKIP', 'dim', f'{st["name"]}.{p}', '', f['reason']))
    return rows


def file_groups(model, verbose):
    """{header: [(tag, reason, text)]} for elements not carried, grouped by reason within a header."""
    groups = defaultdict(lambda: defaultdict(lambda: [[], [], False]))  # header -> (kind, reason) -> [apis, structs, whole]
    for st in model['structs'].values():
        if st['kind'] in ('unsupported', 'skip') or (st['kind'] == 'gap' and st.get('by_header')):
            g = groups[st['header']][(st['kind'], st['reason'])]
            g[1].append(st['name'])
            g[2] = g[2] or bool(st.get('by_header'))
    for api in model['apis'].values():
        s = api_summary(api)
        if s in ('unsupported', 'skip', 'local') or s == 'gap':
            ref = next(r for r, v in api['status'].items() if v == s)
            if s == 'gap' and api['source'][ref] != 'header':
                continue
            g = groups[api['header']][(s, api['reason'][ref])]
            g[0].append(api['name'])
            g[2] = g[2] or api['source'][ref] == 'header'
    tags = {'unsupported': 'NOT CARRIED', 'skip': 'SKIP', 'local': 'LOCAL', 'gap': 'GAP'}
    out = defaultdict(list)
    for header, gs in groups.items():
        for (kind, reason), (apis, sts, whole) in sorted(gs.items()):
            counts = ', '.join(f'{n} {w}' for n, w in ((len(apis), 'APIs'), (len(sts), 'structs')) if n)
            if whole and not verbose:  # the whole header: names are in the HTML and -v
                text = f'whole header: {counts}'
            else:
                text = '; '.join(p for p in (', '.join(f'{a}()' for a in sorted(apis)), ', '.join(sorted(sts))) if p)
            out[header].append((tags[kind], reason, text))
    return out


def report(model, verbose=False, color=False):
    paint = Painter(color)
    rows, groups = file_rows(model), file_groups(model, verbose)
    for e in model['wire_errs']:
        print(f'{paint("WIRE", "red")}  {e}')
    for header in header_order(model):
        hrows, hgroups = rows.get(header, []), groups.get(header, [])
        if not hrows and not hgroups:
            continue
        print(header or '(not in any checked IDF)')
        w_tag = max([len(r[0]) for r in hrows] + [len(g[0]) for g in hgroups] + [4])
        w_subj = max([len(r[2]) for r in hrows if r[3] or r[4]] + [10])
        for tag, col, subj, wh, reason in hrows:
            line = f'  {paint(tag.ljust(w_tag), col)}  {subj.ljust(w_subj)}'
            if wh:
                line += f'  {wh}'
            if reason:
                line += f'  -- {reason}'
            print(line.rstrip())
        for tag, reason, text in hgroups:
            print(f'  {paint(tag.ljust(w_tag), "dim")}  {reason}: {text}')
    if verbose:
        for st in model['structs'].values():
            for p, f in st['fields'].items():
                if not any(f['presence'].values()):
                    print(f'info: {st["name"]}.{p} is in no checked IDF (hosted covers more)')
        for a in model['unused_api_entries']:
            print(f'info: apis.{a} is in no checked IDF (hosted covers more)')

    sc, ac = defaultdict(int), defaultdict(int)
    for st in model['structs'].values():
        sc[struct_summary(st)] += 1
    for api in model['apis'].values():
        ac[api_summary(api)] += 1
    nfail = problem_count(model)
    refs = ', '.join(f'{r["label"]}={r["version"]}' for r in model['refs'])
    s_parts = ', '.join(f'{sc[k]} {k}' for k in ('carried', 'gap', 'missing', 'unsupported', 'skip', 'new',
                                                 'absent') if sc[k])
    a_parts = ', '.join(f'{ac[k]} {k}' for k in ('supported', 'local', 'stub', 'gap', 'missing', 'stale',
                                                 'unsupported', 'skip') if ac[k])
    verdict = paint('FAIL', 'red') if nfail else paint('PASS', 'green')
    print(f'{verdict}: {len(model["apis"])} APIs ({a_parts}); {len(model["structs"])} structs ({s_parts}); '
          f'{nfail} problem(s); IDF: {refs}')
    return EXIT_DRIFT if nfail else 0


# ───────────────────────────── HTML report ─────────────────────────────

HTML_STATUS = {
    'carried': ('ok', 'carried'), 'supported': ('ok', 'supported'), 'local': ('ok', 'local'),
    'gap': ('gap', 'gap'), 'stub': ('gap', 'stub'), 'skip': ('skip', 'skip'),
    'missing': ('miss', 'MISSING'), 'stale': ('miss', 'STALE'), 'new': ('miss', 'NEW'),
    'unsupported': ('uns', 'not carried'), 'absent': ('na', 'absent'),
}

HTML_CSS = """
:root{--bg:#fff;--fg:#1d1f23;--mut:#6b7078;--line:#e3e5e8;--head:#f5f6f8;--sub:#fafbfc;
--ok:#d8f3dc;--okf:#1b5e20;--gap:#fff1c2;--gapf:#7a5300;--skip:#eceef1;--skipf:#555b63;
--miss:#ffd9d6;--missf:#9b1c13;--uns:#e8e4f7;--unsf:#4b3a8c;--naf:#a0a4ab}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#15171a;--fg:#e6e7e9;--mut:#9aa0a8;
--line:#2a2e33;--head:#1d2024;--sub:#191b1e;--ok:#16351d;--okf:#8fd19e;--gap:#3a2f0b;--gapf:#f2c94c;--skip:#24272c;
--skipf:#b4b9c0;--miss:#4a1714;--missf:#ff9b92;--uns:#2a2445;--unsf:#c3b5ff;--naf:#5d636b}}
:root[data-theme=dark]{--bg:#15171a;--fg:#e6e7e9;--mut:#9aa0a8;--line:#2a2e33;--head:#1d2024;--sub:#191b1e;
--ok:#16351d;--okf:#8fd19e;--gap:#3a2f0b;--gapf:#f2c94c;--skip:#24272c;--skipf:#b4b9c0;--miss:#4a1714;
--missf:#ff9b92;--uns:#2a2445;--unsf:#c3b5ff;--naf:#5d636b}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);
font:14px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:1280px;margin:0 auto;padding:20px 16px 40px}h1{font-size:20px;margin:0 0 4px}
.sub{color:var(--mut);margin:0 0 14px}
.chips{display:flex;flex-wrap:wrap;gap:6px;margin:6px 0}
.chip{padding:3px 9px;border-radius:999px;font-weight:600;font-size:12.5px}
.verdict{font-size:13px;padding:3px 10px;border-radius:6px;font-weight:700;margin-left:8px;vertical-align:2px}
.wrap{overflow-x:auto;border:1px solid var(--line);border-radius:8px}
table{border-collapse:collapse;width:100%;min-width:760px}
th,td{padding:5px 8px;border-bottom:1px solid var(--line);text-align:left;vertical-align:top}
thead th{background:var(--head);position:sticky;top:0;font-size:12px;color:var(--mut);font-weight:600}
td.c{text-align:center;white-space:nowrap;font-size:12px}
tr.sh th{background:var(--head);cursor:pointer;font-weight:600;font-size:14.5px;padding-top:9px}
tr.sh th::before{content:"\\25BE ";color:var(--mut)}tbody.closed tr.sh th::before{content:"\\25B8 "}
tbody.closed tr:not(.sh){display:none}
td.nm{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12.5px;padding-left:22px}
tr.st td.nm{font-weight:700;cursor:pointer}tr.st td.nm::before{content:"\\25B8 ";color:var(--mut)}
tr.st.open td.nm::before{content:"\\25BE "}tr.st.whole td.nm{cursor:default}tr.st.whole td.nm::before{content:""}
tr.fld{display:none;background:var(--sub)}tr.fld.show{display:table-row}tr.fld td.nm{padding-left:44px;font-weight:400}
tr.kind td{font-size:11.5px;color:var(--mut);text-transform:uppercase;letter-spacing:.04em;padding:8px 8px 3px 22px}
.note{color:var(--mut);font-size:12.5px}.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px}
.ok{background:var(--ok);color:var(--okf)}.gap{background:var(--gap);color:var(--gapf)}
.skip{background:var(--skip);color:var(--skipf)}.miss{background:var(--miss);color:var(--missf);font-weight:700}
.uns{background:var(--uns);color:var(--unsf)}.na{color:var(--naf)}
.legend{display:flex;flex-wrap:wrap;gap:6px 14px;font-size:12.5px;color:var(--mut);margin:8px 0 4px}
.legend span b{display:inline-block;width:12px;height:12px;border-radius:3px;margin-right:5px;vertical-align:-1px}
.bar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:10px 0}
input[type=search]{flex:1;min-width:200px;max-width:380px;padding:6px 10px;border:1px solid var(--line);
border-radius:6px;background:var(--bg);color:var(--fg)}
button{padding:5px 10px;border:1px solid var(--line);border-radius:6px;background:var(--head);color:var(--fg);cursor:pointer}
"""

HTML_JS = """
function q(s,r){return Array.prototype.slice.call((r||document).querySelectorAll(s));}
q('tbody.grp').forEach(function(tb){
  if(tb.dataset.clean==='1') tb.classList.add('closed');
  tb.querySelector('tr.sh').addEventListener('click',function(){tb.classList.toggle('closed');});
});
function setStruct(tr,open){tr.classList.toggle('open',open);
  q('tr.fld[data-of="'+tr.dataset.id+'"]').forEach(function(f){f.classList.toggle('show',open);});}
q('tr.st:not(.whole)').forEach(function(tr){
  if(tr.dataset.clean==='0') setStruct(tr,true);
  tr.querySelector('td.nm').addEventListener('click',function(){setStruct(tr,!tr.classList.contains('open'));});
});
function all(open){q('tbody.grp').forEach(function(tb){tb.classList.toggle('closed',!open);});
  q('tr.st:not(.whole)').forEach(function(tr){setStruct(tr,open);});}
document.getElementById('xa').onclick=function(){all(true);};
document.getElementById('ca').onclick=function(){all(false);};
var box=document.getElementById('q');
box.addEventListener('input',function(){
  var v=box.value.trim().toLowerCase();
  q('tbody.grp').forEach(function(tb){
    var rows=q('tr:not(.sh)',tb), any=false;
    rows.forEach(function(r){r.style.display='';});
    if(!v){tb.style.display='';return;}
    var head=tb.querySelector('tr.sh').textContent.toLowerCase().indexOf(v)>=0;
    rows.forEach(function(r){
      var hit=head||r.textContent.toLowerCase().indexOf(v)>=0;
      r.style.display=hit&&!r.classList.contains('kind')?'table-row':'none';
      if(hit&&r.dataset.of){var p=tb.querySelector('tr.st[data-id="'+r.dataset.of+'"]');if(p)p.style.display='table-row';}
      any=any||hit;
    });
    tb.style.display=any?'':'none'; if(any) tb.classList.remove('closed');
  });
});
"""


def _esc(s):
    return (str(s).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
            .replace('"', '&quot;'))


def _cell(model, ref, ts, cls, label, title=''):
    r = next(x for x in model['refs'] if x['name'] == ref)
    if not ts:
        return '<td class="c na" title="this IDF version does not have it">&mdash;</td>'
    part = '' if ts == r['targets'] else f'<br><span class="note">{_esc(short_targets(ts))}</span>'
    tip = _esc(title or ', '.join(ts))
    return f'<td class="c {cls}" title="{tip}">{label}{part}</td>'


def _chips(counts, order, unit=''):
    out = []
    for k in order:
        if counts[k]:
            cls, label = HTML_STATUS[k]
            out.append(f'<span class="chip {cls}">{counts[k]} {unit}{label}</span>')
    return '<div class="chips">' + ''.join(out) + '</div>'


STRUCT_ORDER = ('missing', 'new', 'gap', 'carried', 'unsupported', 'skip', 'absent')


def _api_rows(model, apis):
    out = []
    for a in sorted(apis, key=lambda a: (API_RANK.index(api_summary(a)), a['name'])):
        cells = ''
        for r in model['refs']:
            st = a['status'].get(r['name'])
            cls, label = HTML_STATUS[st] if st else ('', '')
            cells += _cell(model, r['name'], a['presence'][r['name']], cls, label, a['reason'].get(r['name'], ''))
        reasons = sorted({v for v in a['reason'].values() if v})
        out.append(f'<tr class="api"><td class="nm">{_esc(a["name"])}()</td>'
                   f'<td class="note">{_esc("; ".join(reasons))}</td>{cells}</tr>')
    return out


def _struct_rows(model, structs, ids):
    out = []
    for st in sorted(structs, key=lambda s: (STRUCT_ORDER.index(struct_summary(s)), s['name'])):
        summ = struct_summary(st)
        sid = f's{next(ids)}'
        if st['kind'] != 'fields':
            cls, label = HTML_STATUS[st['kind']]
            cells = ''.join(_cell(model, r['name'], st['presence'][r['name']], cls, label) for r in model['refs'])
            out.append(f'<tr class="st whole"><td class="nm">{_esc(st["name"])}</td>'
                       f'<td class="note">{_esc(st["reason"])}</td>{cells}</tr>')
            continue
        cells = ''
        for r in model['refs']:
            if r['name'] in st['no_struct']:
                cells += '<td class="c miss">NO STRUCT</td>'
                continue
            rcls, rlabel = HTML_STATUS[struct_ref_status(st, r['name'])]
            cells += _cell(model, r['name'], st['presence'][r['name']], rcls, rlabel)
        clean = '1' if summ in ('carried', 'absent') else '0'
        out.append(f'<tr class="st" data-id="{sid}" data-clean="{clean}"><td class="nm">{_esc(st["name"])} '
                   f'<span class="note">({len(st["fields"])} fields)</span></td>'
                   f'<td class="mono note">{_esc(st["proto"])}</td>{cells}</tr>')
        frank = {'missing': 0, 'gap': 1}
        for p, f in sorted(st['fields'].items(), key=lambda kv: frank.get(kv[1]['status'], 2)):
            fcls, flabel = HTML_STATUS[f['status']]
            fcells = ''.join(_cell(model, r['name'], f['presence'][r['name']], fcls, flabel) for r in model['refs'])
            slot = _esc(f['slot']) or f'<span class="{fcls}">{_esc(f["reason"]) or flabel}</span>'
            out.append(f'<tr class="fld" data-of="{sid}" title="{_esc(f["reason"])}"><td class="nm">.{_esc(p)}</td>'
                       f'<td class="mono note">{slot}</td>{fcells}</tr>')
    return out


def render_html(model):
    import itertools
    refs = model['refs']
    nfail = problem_count(model)
    sc, ac = defaultdict(int), defaultdict(int)
    for st in model['structs'].values():
        sc[struct_summary(st)] += 1
    for api in model['apis'].values():
        ac[api_summary(api)] += 1
    out = ['<!doctype html><html lang="en"><head><meta charset="utf-8">',
           '<meta name="viewport" content="width=device-width,initial-scale=1">',
           '<title>Wi-Fi Coverage</title><style>', HTML_CSS, '</style></head><body><main>']
    verdict = ('miss', 'FAIL') if nfail else ('ok', 'PASS')
    out.append(f'<h1>ESP-Hosted Wi-Fi coverage<span class="verdict {verdict[0]}">{verdict[1]}</span></h1>')
    out.append('<p class="sub">IDF Wi-Fi APIs and structs vs what ESP-Hosted covers, by header file. IDF: '
               + ', '.join(f'<b>{_esc(r["name"])}</b> {r["version"]} ({len(r["targets"])} targets: '
                           f'{_esc(short_targets(r["targets"]))})' for r in refs) + '</p>')
    out.append(f'<div class="note">{len(model["apis"])} APIs</div>'
               + _chips(ac, ('missing', 'stale', 'supported', 'local', 'stub', 'gap', 'unsupported', 'skip')))
    out.append(f'<div class="note">{len(model["structs"])} structs</div>'
               + _chips(sc, ('missing', 'new', 'carried', 'gap', 'unsupported', 'skip', 'absent')))
    out.append('<div class="legend">' + ''.join(f'<span><b class="{c}"></b>{t}</span>' for c, t in (
        ('ok', 'supported / local / carried: works on the host'),
        ('gap', 'gap: should work, does not yet; stub: returns ESP_ERR_NOT_SUPPORTED'),
        ('skip', 'skip: never needed, by design'),
        ('miss', 'MISSING / NEW / STALE: fails the check'),
        ('uns', 'not carried: hosted does not offer it'),
        ('na', '&mdash; this IDF version does not have it'))) + '</div>')
    if model['wire_errs']:
        out.append('<ul>' + ''.join(f'<li class="miss">WIRE: {_esc(e)}</li>' for e in model['wire_errs']) + '</ul>')
    out.append('<div class="bar"><input type="search" id="q" placeholder="Filter by header, API, struct or field">'
               '<button id="xa">Expand all</button><button id="ca">Collapse all</button></div>')
    hdr = ''.join(f'<th class="c" title="{_esc(r["name"])}">{_esc(r["label"])}<br>'
                  f'<span class="note">{r["version"]}</span></th>' for r in refs)
    out.append(f'<div class="wrap"><table><thead><tr><th>Header / API / struct / field</th>'
               f'<th>Note / wire slot</th>{hdr}</tr></thead>')
    apis_by, structs_by = defaultdict(list), defaultdict(list)
    for a in model['apis'].values():
        apis_by[a['header']].append(a)
    for st in model['structs'].values():
        structs_by[st['header']].append(st)
    ids = itertools.count()
    for header in header_order(model):
        apis, structs = apis_by.get(header, []), structs_by.get(header, [])
        a_c, s_c = defaultdict(int), defaultdict(int)
        for a in apis:
            a_c[api_summary(a)] += 1
        for st in structs:
            s_c[struct_summary(st)] += 1
        problem = any(a_c[k] for k in ('missing', 'stale', 'gap', 'stub')) or \
            any(s_c[k] for k in ('missing', 'new', 'gap'))
        summary = []
        if apis:
            summary.append(f'{len(apis)} APIs: ' + ', '.join(f'{a_c[k]} {HTML_STATUS[k][1]}' for k in API_RANK
                                                              if a_c[k]))
        if structs:
            summary.append(f'{len(structs)} structs: ' + ', '.join(f'{s_c[k]} {HTML_STATUS[k][1]}'
                                                                   for k in STRUCT_ORDER if s_c[k]))
        out.append(f'<tbody class="grp" data-clean="{0 if problem else 1}"><tr class="sh">'
                   f'<th colspan="{2 + len(refs)}">{_esc(header or "(not in any checked IDF)")} '
                   f'<span class="note">{_esc(" · ".join(summary))}</span></th></tr>')
        if apis:
            out.append(f'<tr class="kind"><td colspan="{2 + len(refs)}">APIs</td></tr>')
            out += _api_rows(model, apis)
        if structs:
            out.append(f'<tr class="kind"><td colspan="{2 + len(refs)}">Structs</td></tr>')
            out += _struct_rows(model, structs, ids)
        out.append('</tbody>')
    out.append('</table></div>')
    out.append('<p class="note">Click a header to open it, and a struct to show its fields. Cells name the targets '
               'when an element exists on some targets only. Reserved fields and unnamed padding are not listed. '
               'Generated by tools/wifi_coverage/check_wifi_coverage.py.</p>')
    out.append(f'</main><script>{HTML_JS}</script></body></html>')
    return '\n'.join(out)


# ───────────────────────────── CLI ─────────────────────────────

def load_refs(path, live):
    try:
        data = yaml.safe_load(Path(path).read_text())
    except (OSError, yaml.YAMLError) as e:
        raise SetupError(f'refs file {path}: {e}')
    if not isinstance(data, dict) or not data.get('url') or not data.get('refs'):
        raise SetupError(f'{path}: needs url and a refs list')
    refs = []
    for r in data['refs']:
        if not isinstance(r, dict) or 'branch' not in r or (not live and 'sha' not in r):
            raise SetupError(f'{path}: each ref needs branch and sha')
        refs.append((r['branch'], r['branch'] if live else r['sha']))
    return data['url'], refs


def _have_commits(repo, revs):
    return all(subprocess.run(['git', '-C', str(repo), 'cat-file', '-e', f'{r}^{{commit}}'],
                              capture_output=True).returncode == 0 for r in revs)


def fetch_refs(repo, url, revs, live):
    """Fetch the revisions (blobs on demand) into the cache repo; return the git revs to read.

    Pinned SHAs already in the cache are not fetched again, so repeat runs work offline.
    """
    if not (Path(repo) / '.git').exists() and not (Path(repo) / 'HEAD').exists():
        Path(repo).mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', 'init', '-q', str(repo)], check=True)
    if not live and _have_commits(repo, revs):
        return revs
    want = [f'+refs/heads/{r}:refs/remotes/wsc/{r}' for r in revs] if live else revs
    cmd = ['git', '-C', str(repo), 'fetch', '-q', '--depth=1', '--filter=blob:none', url] + want
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise SetupError(f'fetch failed: {r.stderr.strip()}')
    return [f'refs/remotes/wsc/{r}' for r in revs] if live else revs


def dump(tree, manifest, name, cpp):
    """Print the fields of a struct, or the header of an API, as one ref has them."""
    one = {'headers': {}, 'apis': {}, 'structs': {name: {'fields': {}}}}
    scan = scan_ref(tree, one, cpp)
    paths, apis = set(), {}
    for d in scan['targets'].values():
        paths |= d['fields'].get(name, set())
        apis.update(d['apis'])
    if name in apis:
        print(f'{name}() in {apis[name]}')
    for p in sorted(paths):
        print(p)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--idf', action='append', default=[], help='IDF working tree (repeatable)')
    ap.add_argument('--idf-repo', help='IDF git repo to read --ref revisions from')
    ap.add_argument('--ref', action='append', default=[], help='git revision in --idf-repo')
    ap.add_argument('--refs-file', help=f'pinned refs to fetch and check (e.g. {DEFAULT_REFS.name})')
    ap.add_argument('--live', action='store_true', help='with --refs-file: use branch heads, not SHAs')
    ap.add_argument('--url', help='with --refs-file: fetch from this IDF mirror instead of its url')
    ap.add_argument('--cache-dir', default=str(Path.home() / '.cache' / 'esp_hosted' / 'wifi_coverage_idf'),
                    help='with --refs-file: fetch cache (default: %(default)s)')
    ap.add_argument('--manifest', default=str(DEFAULT_MANIFEST))
    ap.add_argument('--hosted-root', default=str(REPO_ROOT), help='ESP-Hosted tree whose sources are scanned')
    ap.add_argument('--proto', default=str(PROTO))
    ap.add_argument('--host-bits', default=str(HOST_BITS))
    ap.add_argument('--cp-bits', default=str(CP_BITS))
    ap.add_argument('--html', metavar='FILE', help='also write the full coverage table as HTML')
    ap.add_argument('--color', choices=('auto', 'always', 'never'), default='auto')
    ap.add_argument('--dump', metavar='NAME', help='print the fields of struct NAME (or the header of API NAME)')
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args(argv)
    try:
        manifest = load_manifest(a.manifest)
        cpp = find_cpp()
        trees = [IdfTree(p, root=p) for p in a.idf]
        if a.refs_file:
            url, refs = load_refs(a.refs_file, a.live)
            url = a.url or url
            revs = fetch_refs(a.cache_dir, url, [r for _, r in refs], a.live)
            trees += [IdfTree(name, repo=a.cache_dir, rev=rev) for (name, _), rev in zip(refs, revs)]
        if a.ref:
            if not a.idf_repo:
                raise SetupError('--ref needs --idf-repo')
            trees += [IdfTree(r, repo=a.idf_repo, rev=r) for r in a.ref]
        if not trees:
            raise SetupError('nothing to check: give --idf, --ref or --refs-file')
        if a.dump:
            dump(trees[0], manifest, a.dump, cpp)
            return 0
        try:
            texts = [Path(p).read_text() for p in (a.proto, a.host_bits, a.cp_bits)]
        except OSError as e:
            raise SetupError(str(e))
        wire_errs = check_wire_claims(manifest['structs'], *texts)
        defs = hosted_definitions(Path(a.hosted_root))
        if not defs:
            raise SetupError(f'no esp_* function definitions found under {a.hosted_root}')
        scans = [(t.name, scan_ref(t, manifest, cpp)) for t in trees]
        model = build_model(manifest, scans, wire_errs, defs)
        color = a.color == 'always' or (a.color == 'auto' and sys.stdout.isatty()
                                         and not os.environ.get('NO_COLOR'))
        rc = report(model, a.verbose, color)
        if a.html:
            Path(a.html).write_text(render_html(model))
            print(f'HTML report: {a.html}')
        return rc
    except SetupError as e:
        print(f'ERROR: {e}', file=sys.stderr)
        return EXIT_SETUP


if __name__ == '__main__':
    sys.exit(main())
