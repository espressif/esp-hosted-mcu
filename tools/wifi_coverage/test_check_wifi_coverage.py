#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Tests for check_wifi_coverage.py on synthetic IDF and hosted trees.

Run:  python3 tools/wifi_coverage/test_check_wifi_coverage.py
"""
import contextlib
import io
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_wifi_coverage as wsc  # noqa: E402

PROTO = '''
message wifi_init_config { int32 a = 1; int32 magic = 2; }
message wifi_sta_config { bytes ssid = 1; uint32 bitmask = 2; wifi_scan_threshold threshold = 3; }
message wifi_ap_config { bytes ssid = 1; }
message wifi_scan_threshold { int32 rssi = 1; }
message wifi_country { bytes cc = 1; int32 wifi_5g_channel_mask = 2; }
message wifi_twt_config { bool post_wakeup_event = 1; }
'''
CP_BITS = 'typedef enum { WIFI_STA_CONFIG_1_rm_enabled = 0, WIFI_ONLY_CP_BIT = 1 } x_t;\n'
HOST_BITS = 'typedef enum { EH_HOST_WIFI_STA_CONFIG_1_rm_enabled = 0, EH_HOST_WIFI_ONLY_HOST_BIT = 1 } y_t;\n'

BASE_MANIFEST = {
    'wifi_init_config_t': {'proto': 'wifi_init_config', 'fields': {
        'a': 'carried', 'magic': 'carried', 'osi_funcs': {'skip': 'pointer'}}},
    'wifi_sta_config_t': {'proto': 'wifi_sta_config', 'fields': {
        'ssid': 'carried', 'threshold': 'carried',
        'rm_enabled': {'proto': 'wifi_sta_config.bitmask', 'bit': 'WIFI_STA_CONFIG_1_rm_enabled'}}},
    'wifi_ap_config_t': {'proto': 'wifi_ap_config', 'fields': {'ssid': 'carried'}},
    'wifi_scan_threshold_t': {'proto': 'wifi_scan_threshold', 'fields': {'rssi': 'carried'}},
    'wifi_twt_config_t': {'unsupported': 'TWT not offered'},
}

# Struct bodies; each is placed in esp_wifi_types_generic.h unless noted.
BASE_STRUCTS = {
    'wifi_init_config_t': 'int a; void *osi_funcs; int magic;',
    'wifi_scan_threshold_t': 'int8_t rssi;',
    'wifi_sta_config_t': textwrap.dedent('''\
        uint8_t ssid[32];
        wifi_scan_threshold_t threshold;
        uint32_t rm_enabled:1;
        uint32_t reserved1:31;'''),
    'wifi_ap_config_t': 'uint8_t ssid[32];',
}


BASE_GLUE = textwrap.dedent('''\
    esp_err_t esp_wifi_remote_init(const wifi_init_config_t *config)
    {
        return eh_host_wifi_init(config);
    }

    esp_err_t esp_wifi_remote_set_config(wifi_interface_t ifx, wifi_config_t *conf) { return 0; }
    ''')


def make_idf(root, structs=None, version=(6, 1, 0), targets=None, extra_generic='',
             esp_wifi_extra='', headers=None):
    """Write a minimal IDF tree. targets: {name: [caps]}; caps are SOC_* names set to y."""
    structs = dict(BASE_STRUCTS if structs is None else structs)
    targets = targets or {'esp32': [], 'esp32c5': ['SOC_WIFI_HE_SUPPORT', 'SOC_WIFI_SUPPORT_5G']}
    root = Path(root)
    inc = root / wsc.WIFI_INC
    (inc / 'local').mkdir(parents=True, exist_ok=True)
    init = structs.pop('wifi_init_config_t', None)
    body = ''.join(f'typedef struct {{\n{b}\n}} {n};\n' for n, b in structs.items())
    (inc / 'esp_wifi_types_generic.h').write_text(
        '#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n#include "sdkconfig.h"\n'
        + body + extra_generic)
    (inc / 'local' / 'esp_wifi_types_native.h').write_text(
        '#pragma once\n#include "sdkconfig.h"\n'
        '#if CONFIG_SOC_WIFI_HE_SUPPORT\n'
        'typedef struct { bool post_wakeup_event; } wifi_twt_config_t;\n#endif\n')
    (inc / 'esp_wifi_types.h').write_text(
        '#pragma once\n#include "esp_wifi_types_generic.h"\n#include "esp_wifi_types_native.h"\n')
    init_def = f'typedef struct {{\n{init}\n}} wifi_init_config_t;\n' if init else ''
    (inc / 'esp_wifi.h').write_text(
        '#pragma once\n#include "esp_err.h"\n#include "esp_event.h"\n#include "esp_wifi_types.h"\n'
        + init_def + 'esp_err_t esp_wifi_init(const wifi_init_config_t *config);\n'
        'esp_err_t esp_wifi_set_config(int interface, void *conf);\n' + esp_wifi_extra)
    for rel, text in (headers or {}).items():
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(text)
    for t, caps in targets.items():
        d = root / wsc.SOC_DIR / t / 'include' / 'soc'
        d.mkdir(parents=True, exist_ok=True)
        text = 'config SOC_WIFI_SUPPORTED\n    bool\n    default y\n\n'
        text += ''.join(f'config {c}\n    bool\n    default y\n\n' for c in caps)
        (d / 'Kconfig.soc_caps.in').write_text(text)
    vh = root / wsc.VERSION_H
    vh.parent.mkdir(parents=True, exist_ok=True)
    vh.write_text(''.join(f'#define ESP_IDF_VERSION_{k}   {v}\n'
                          for k, v in zip(('MAJOR', 'MINOR', 'PATCH'), version)))
    return root


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        (self.tmp / 'rpc.proto').write_text(PROTO)
        (self.tmp / 'cp.h').write_text(CP_BITS)
        (self.tmp / 'host.h').write_text(HOST_BITS)
        self.glue(BASE_GLUE)

    def glue(self, text):
        """Replace the hosted tree's only source file."""
        p = self.tmp / 'hosted' / 'host' / 'glue.c'
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)

    def manifest(self, m=None, apis=None, headers=None):
        import yaml
        p = self.tmp / f'm{id(m)}_{id(apis)}_{id(headers)}.yml'
        p.write_text(yaml.safe_dump({'structs': m or BASE_MANIFEST, 'apis': apis or {},
                                     'headers': headers or {}}))
        return p

    def run_check(self, args, manifest=None):
        out, err = io.StringIO(), io.StringIO()
        argv = ['--manifest', str(manifest or self.manifest()),
                '--proto', str(self.tmp / 'rpc.proto'),
                '--host-bits', str(self.tmp / 'host.h'), '--cp-bits', str(self.tmp / 'cp.h'),
                '--hosted-root', str(self.tmp / 'hosted')] + args
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = wsc.main(argv)
        return rc, out.getvalue() + err.getvalue()

    def idf(self, name='idf', **kw):
        return str(make_idf(self.tmp / name, **kw))

    @staticmethod
    def with_fields(**extra):
        s = dict(BASE_STRUCTS)
        for k, v in extra.items():
            s[k] = s.get(k, '') + '\n' + v if v is not None else None
        return {k: v for k, v in s.items() if v is not None}


class Pass(Base):
    def test_baseline(self):
        rc, out = self.run_check(['--idf', self.idf()])
        self.assertEqual(rc, 0, out)
        self.assertIn('PASS', out)

    def test_manifest_has_more_than_idf(self):
        m = {k: dict(v, fields=dict(v['fields'])) if 'fields' in v else v for k, v in BASE_MANIFEST.items()}
        m['wifi_ap_config_t']['fields']['future_field'] = {'skip': 'only in a newer IDF'}
        m['wifi_init_config_t']['fields']['old_field'] = {'skip': 'removed in this IDF'}
        rc, out = self.run_check(['--idf', self.idf(), '-v'], self.manifest(m))
        self.assertEqual(rc, 0, out)
        self.assertIn('wifi_ap_config_t.future_field is in no checked IDF', out)

    def test_reserved_and_padding_ignored(self):
        s = dict(BASE_STRUCTS)
        s['wifi_sta_config_t'] = textwrap.dedent('''\
            uint8_t ssid[32];
            wifi_scan_threshold_t threshold;
            uint32_t rm_enabled:1;
            uint32_t reserved:20;
            uint32_t he_reserved:7;
            uint32_t reserved2:3;
            uint32_t :1;''')
        rc, out = self.run_check(['--idf', self.idf(structs=s)])
        self.assertEqual(rc, 0, out)

    def test_bitfield_retype_is_same_field(self):
        s = dict(BASE_STRUCTS)
        s['wifi_init_config_t'] = 'int a:4; void *osi_funcs; unsigned magic:1;'
        rc, out = self.run_check(['--idf', self.idf(structs=s)])
        self.assertEqual(rc, 0, out)

    def test_struct_absent_on_some_targets(self):
        m = dict(BASE_MANIFEST, wifi_twt_config_t={
            'proto': 'wifi_twt_config', 'fields': {'post_wakeup_event': 'carried'}})
        rc, out = self.run_check(['--idf', self.idf()], self.manifest(m))
        self.assertEqual(rc, 0, out)

    def test_struct_absent_before_since(self):
        m = dict(BASE_MANIFEST, wifi_new_t={'proto': 'wifi_ap_config', 'since': '6.2',
                                            'fields': {'ssid': 'carried'}})
        rc, out = self.run_check(['--idf', self.idf(version=(6, 1, 9))], self.manifest(m))
        self.assertEqual(rc, 0, out)

    def test_struct_alias_old_name(self):
        s = dict(BASE_STRUCTS)
        s['wifi_old_ap_t'] = s.pop('wifi_ap_config_t')
        s['wifi_ap_config_t'] = 'uint8_t ssid[32];'
        m = dict(BASE_MANIFEST, wifi_renamed_t={'proto': 'wifi_ap_config', 'aliases': ['wifi_old_ap_t'],
                                                'fields': {'ssid': 'carried'}})
        rc, out = self.run_check(['--idf', self.idf(structs=s)], self.manifest(m))
        self.assertEqual(rc, 0, out)

    def test_typedef_alias_resolves(self):
        s = dict(BASE_STRUCTS)
        s['wifi_real_ap_t'] = s.pop('wifi_ap_config_t')
        extra = 'typedef wifi_real_ap_t wifi_ap_config_t;\n'
        m = dict(BASE_MANIFEST, wifi_real_ap_t={'skip': 'same body as wifi_ap_config_t'})
        rc, out = self.run_check(['--idf', self.idf(structs=s, extra_generic=extra)], self.manifest(m))
        self.assertEqual(rc, 0, out)

    def test_anonymous_members_flatten(self):
        s = dict(BASE_STRUCTS, wifi_ap_config_t='union { uint8_t ssid[32]; uint8_t raw[32]; };')
        m = {k: dict(v, fields=dict(v['fields'])) if 'fields' in v else v for k, v in BASE_MANIFEST.items()}
        m['wifi_ap_config_t']['fields']['raw'] = {'skip': 'alias of ssid'}
        rc, out = self.run_check(['--idf', self.idf(structs=s)], self.manifest(m))
        self.assertEqual(rc, 0, out)

    def test_skip_stops_nested_expansion(self):
        s = dict(BASE_STRUCTS, wifi_init_config_t=(
            'int a; void *osi_funcs; struct { int (*f1)(void); int (*f2)(void); } crypto; int magic;'))
        m = {k: dict(v, fields=dict(v['fields'])) if 'fields' in v else v for k, v in BASE_MANIFEST.items()}
        m['wifi_init_config_t']['fields']['crypto'] = {'skip': 'function table'}
        rc, out = self.run_check(['--idf', self.idf(structs=s)], self.manifest(m))
        self.assertEqual(rc, 0, out)

    def test_gap_reported_not_failed(self):
        s = self.with_fields(wifi_ap_config_t='uint8_t max_scan_ap_num;')
        m = {k: dict(v, fields=dict(v['fields'])) if 'fields' in v else v for k, v in BASE_MANIFEST.items()}
        m['wifi_ap_config_t']['fields']['max_scan_ap_num'] = {'gap': 'follow-up'}
        rc, out = self.run_check(['--idf', self.idf(structs=s)], self.manifest(m))
        self.assertEqual(rc, 0, out)
        self.assertRegex(out, r'GAP\s+wifi_ap_config_t\.max_scan_ap_num\s+idf \[all targets\]  -- follow-up')


class Fail(Base):
    def test_new_field(self):
        s = self.with_fields(wifi_ap_config_t='uint8_t new_knob;')
        rc, out = self.run_check(['--idf', self.idf(structs=s)])
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'MISSING\s+wifi_ap_config_t\.new_knob\s+idf \[all targets\]\n')

    def test_new_field_in_inline_nested_struct(self):
        s = dict(BASE_STRUCTS, wifi_ap_config_t='uint8_t ssid[32];\nstruct { int x; int y; } inner;')
        m = {k: dict(v, fields=dict(v['fields'])) if 'fields' in v else v for k, v in BASE_MANIFEST.items()}
        m['wifi_ap_config_t']['fields']['inner.x'] = {'proto': 'wifi_ap_config.ssid'}
        rc, out = self.run_check(['--idf', self.idf(structs=s)], self.manifest(m))
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'MISSING\s+wifi_ap_config_t\.inner\.y')
        self.assertNotIn('wifi_ap_config_t.inner.x ', out)

    def test_new_struct(self):
        s = dict(BASE_STRUCTS, wifi_brand_new_t='int knob;')
        rc, out = self.run_check(['--idf', self.idf(structs=s)])
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'NEW STRUCT\s+wifi_brand_new_t\s+idf \[all targets\]')

    def test_new_field_in_nested_manifest_struct(self):
        s = self.with_fields(wifi_scan_threshold_t='uint8_t rssi_5g_adjustment;')
        rc, out = self.run_check(['--idf', self.idf(structs=s)])
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'MISSING\s+wifi_scan_threshold_t\.rssi_5g_adjustment')

    def test_target_only_field(self):
        s = self.with_fields(wifi_ap_config_t='#if CONFIG_SOC_WIFI_SUPPORT_5G\nuint32_t mask_5g;\n#endif')
        rc, out = self.run_check(['--idf', self.idf(structs=s)])
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'MISSING\s+wifi_ap_config_t\.mask_5g\s+idf \[c5\]')

    def test_new_field_in_anonymous_member(self):
        s = dict(BASE_STRUCTS, wifi_ap_config_t='uint8_t ssid[32];\nstruct { uint8_t hidden_knob; };')
        rc, out = self.run_check(['--idf', self.idf(structs=s)])
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'MISSING\s+wifi_ap_config_t\.hidden_knob')

    def test_struct_absent_everywhere(self):
        m = dict(BASE_MANIFEST, wifi_gone_t={'proto': 'wifi_ap_config', 'fields': {'ssid': 'carried'}})
        rc, out = self.run_check(['--idf', self.idf()], self.manifest(m))
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'NO STRUCT\s+wifi_gone_t\s+idf \(6\.1\.0\)')

    def test_struct_absent_at_since(self):
        m = dict(BASE_MANIFEST, wifi_new_t={'proto': 'wifi_ap_config', 'since': '6.1',
                                            'fields': {'ssid': 'carried'}})
        rc, out = self.run_check(['--idf', self.idf(version=(6, 1, 0))], self.manifest(m))
        self.assertEqual(rc, 1, out)

    def test_proto_field_missing(self):
        m = {k: dict(v, fields=dict(v['fields'])) if 'fields' in v else v for k, v in BASE_MANIFEST.items()}
        m['wifi_ap_config_t']['fields']['ssid'] = {'proto': 'wifi_ap_config.no_such'}
        rc, out = self.run_check(['--idf', self.idf()], self.manifest(m))
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'WIRE\s+wifi_ap_config_t\.ssid: proto field wifi_ap_config\.no_such')

    def test_carried_default_needs_same_name_in_proto(self):
        s = self.with_fields(wifi_ap_config_t='uint8_t channel;')
        m = {k: dict(v, fields=dict(v['fields'])) if 'fields' in v else v for k, v in BASE_MANIFEST.items()}
        m['wifi_ap_config_t']['fields']['channel'] = 'carried'
        rc, out = self.run_check(['--idf', self.idf(structs=s)], self.manifest(m))
        self.assertEqual(rc, 1, out)
        self.assertIn('proto field wifi_ap_config.channel not in rpc_v2.proto', out)

    def test_bit_one_sided(self):
        for bit, side in (('WIFI_ONLY_CP_BIT', 'host'), ('WIFI_ONLY_HOST_BIT', 'CP')):
            m = {k: dict(v, fields=dict(v['fields'])) if 'fields' in v else v for k, v in BASE_MANIFEST.items()}
            m['wifi_sta_config_t']['fields']['rm_enabled'] = {'proto': 'wifi_sta_config.bitmask', 'bit': bit}
            rc, out = self.run_check(['--idf', self.idf()], self.manifest(m))
            self.assertEqual(rc, 1, out)
            self.assertIn(f'not in {side} bitmask header', out)

    def test_bit_only_in_comment_does_not_count(self):
        (self.tmp / 'host.h').write_text('/* EH_HOST_WIFI_STA_CONFIG_1_rm_enabled */\n')
        rc, out = self.run_check(['--idf', self.idf()])
        self.assertEqual(rc, 1, out)

    def test_multi_ref_reports_each_ref(self):
        old = self.idf('old', version=(5, 5, 0))
        new = self.idf('new', structs=self.with_fields(wifi_ap_config_t='uint8_t new_knob;'))
        rc, out = self.run_check(['--idf', old, '--idf', new])
        self.assertEqual(rc, 1, out)
        line = [x for x in out.splitlines() if 'new_knob' in x][0]
        self.assertTrue(line.endswith('new [all targets]'), line)


class Errors(Base):
    def test_unparseable_header(self):
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra='typedef struct { int x } broken_t;\n')])
        self.assertEqual(rc, 2, out)

    def test_missing_include_is_error(self):
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra='#include "nope.h"\n')])
        self.assertEqual(rc, 2, out)

    def test_sentinel_missing(self):
        s = dict(BASE_STRUCTS)
        del s['wifi_ap_config_t']
        m = {k: v for k, v in BASE_MANIFEST.items() if k != 'wifi_ap_config_t'}
        rc, out = self.run_check(['--idf', self.idf(structs=s)], self.manifest(m))
        self.assertEqual(rc, 2, out)
        self.assertIn('sentinel', out)

    def test_no_wifi_targets(self):
        root = make_idf(self.tmp / 'idf', targets={'esp32h2': []})
        caps = root / wsc.SOC_DIR / 'esp32h2/include/soc/Kconfig.soc_caps.in'
        caps.write_text('config SOC_WIFI_SUPPORTED\n    bool\n    default n\n')
        rc, out = self.run_check(['--idf', str(root)])
        self.assertEqual(rc, 2, out)

    def test_missing_idf_dir(self):
        rc, out = self.run_check(['--idf', str(self.tmp / 'nope')])
        self.assertEqual(rc, 2, out)

    def test_schema_errors(self):
        bad = [
            {'wifi_ap_config_t': {'proto': 'p', 'fields': {'ssid': 'carryed'}}},
            {'wifi_ap_config_t': {'proto': 'p', 'fields': {'ssid': {'proto': 'p.x', 'skip': 'y'}}}},
            {'wifi_ap_config_t': {'proto': 'p', 'fields': {'ssid': {'bit': 'X'}}}},
            {'wifi_ap_config_t': {'proto': 'p', 'fields': {'ssid': {'skip': ''}}}},
            {'wifi_ap_config_t': {'proto': 'p', 'fields': {'a.b': 'carried'}}},
            {'wifi_ap_config_t': {'fields': {'ssid': 'carried'}}},
            {'wifi_ap_config_t': {'proto': 'p', 'fieldz': {}}},
            {'wifi_ap_config_t': {'proto': 'p', 'since': 'v6', 'fields': {}}},
            {'wifi_ap_config_t': {'proto': 'p', 'fields': {'ssid': {'proto': 'p.s', 'bitt': 'X'}}}},
        ]
        for m in bad:
            with self.subTest(m=m):
                rc, out = self.run_check(['--idf', self.idf()], self.manifest(m))
                self.assertEqual(rc, 2, out)
                self.assertIn('manifest', out)

    def test_duplicate_key(self):
        p = self.tmp / 'dup.yml'
        p.write_text('structs:\n  wifi_ap_config_t:\n    proto: wifi_ap_config\n'
                     '    fields:\n      ssid: carried\n      ssid: {skip: x}\n')
        rc, out = self.run_check(['--idf', self.idf()], p)
        self.assertEqual(rc, 2, out)
        self.assertIn('duplicate key ssid', out)


class WholeStructs(Base):
    def test_unsupported_and_skip_are_listed(self):
        s = dict(BASE_STRUCTS, wifi_csi_cfg_t='int lltf_en;', wifi_csi_info_t='int len;')
        m = dict(BASE_MANIFEST, wifi_csi_cfg_t={'unsupported': 'CSI not offered'},
                 wifi_csi_info_t={'unsupported': 'CSI not offered'})
        rc, out = self.run_check(['--idf', self.idf(structs=s)], self.manifest(m))
        self.assertEqual(rc, 0, out)
        self.assertIn('NOT CARRIED  CSI not offered: wifi_csi_cfg_t, wifi_csi_info_t', out)
        self.assertRegex(out, r'SKIP\s+wifi_init_config_t\.osi_funcs\s+-- pointer')

    def test_whole_gap_listed_with_refs(self):
        s = dict(BASE_STRUCTS, wifi_evt_t='int old_mode;')
        m = dict(BASE_MANIFEST, wifi_evt_t={'gap': 'payload dropped'})
        rc, out = self.run_check(['--idf', self.idf(structs=s)], self.manifest(m))
        self.assertEqual(rc, 0, out)
        self.assertRegex(out, r'GAP\s+wifi_evt_t\s+idf \[all targets\]  -- payload dropped')

    def test_whole_entry_absent_is_fine(self):
        m = dict(BASE_MANIFEST, wifi_gone_t={'unsupported': 'removed from IDF'})
        rc, out = self.run_check(['--idf', self.idf()], self.manifest(m))
        self.assertEqual(rc, 0, out)
        self.assertNotIn('NO STRUCT', out)

    def test_whole_entry_fields_not_checked(self):
        s = dict(BASE_STRUCTS, wifi_csi_cfg_t='int lltf_en; int brand_new;')
        m = dict(BASE_MANIFEST, wifi_csi_cfg_t={'unsupported': 'CSI not offered'})
        rc, out = self.run_check(['--idf', self.idf(structs=s)], self.manifest(m))
        self.assertEqual(rc, 0, out)

    def test_whole_struct_nested_is_one_entry(self):
        s = {'wifi_inner_t': 'int x;', **BASE_STRUCTS}
        s['wifi_ap_config_t'] = 'uint8_t ssid[32];\nwifi_inner_t inner;'
        m = dict(BASE_MANIFEST, wifi_inner_t={'unsupported': 'x'})
        rc, out = self.run_check(['--idf', self.idf(structs=s)], self.manifest(m))
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'MISSING\s+wifi_ap_config_t\.inner\s')
        self.assertNotIn('inner.x', out)

    def test_schema_whole_entry(self):
        for m in ({'x_t': {'unsupported': 'r', 'fields': {}}}, {'x_t': {'unsupported': ''}},
                  {'x_t': {'unsupported': 'r', 'skip': 'r'}},
                  {'a_t': {'skip': 'r', 'aliases': ['b_t']}, 'b_t': {'skip': 'r'}}):
            with self.subTest(m=m):
                rc, out = self.run_check(['--idf', self.idf()], self.manifest(dict(BASE_MANIFEST, **m)))
                self.assertEqual(rc, 2, out)


class Output(Base):
    def test_html_report(self):
        s = self.with_fields(wifi_ap_config_t='uint8_t new_knob;')
        s['wifi_csi_cfg_t'] = 'int a;'
        m = dict(BASE_MANIFEST, wifi_csi_cfg_t={'unsupported': 'CSI not offered'})
        html = self.tmp / 'r.html'
        rc, out = self.run_check(['--idf', self.idf(structs=s), '--html', str(html)], self.manifest(m))
        self.assertEqual(rc, 1, out)
        text = html.read_text()
        self.assertIn('verdict miss">FAIL', text)
        self.assertIn('<td class="nm">.new_knob</td>', text)
        self.assertIn('class="c miss"', text)
        self.assertIn('CSI not offered', text)
        self.assertIn('wifi_csi_cfg_t', text)
        self.assertIn('<td class="c ok"', text)
        grp = text.split('<th colspan="3">esp_wifi_types_generic.h')[1].split('</tbody>')[0]
        self.assertIn('wifi_ap_config_t', grp)        # file -> struct
        self.assertIn('.new_knob</td>', grp)          # struct -> field
        self.assertNotIn('wifi_init_config_t', grp)   # defined in esp_wifi.h, not here

    def test_html_partial_targets_named(self):
        s = self.with_fields(wifi_ap_config_t='#if CONFIG_SOC_WIFI_SUPPORT_5G\nuint32_t mask_5g;\n#endif')
        m = {k: dict(v, fields=dict(v['fields'])) if 'fields' in v else v for k, v in BASE_MANIFEST.items()}
        m['wifi_ap_config_t']['fields']['mask_5g'] = {'proto': 'wifi_ap_config.ssid'}
        html = self.tmp / 'r.html'
        rc, out = self.run_check(['--idf', self.idf(structs=s), '--html', str(html)], self.manifest(m))
        self.assertEqual(rc, 0, out)
        self.assertIn('<span class="note">c5</span>', html.read_text())

    def test_color(self):
        s = self.with_fields(wifi_ap_config_t='uint8_t new_knob;')
        rc, out = self.run_check(['--idf', self.idf(structs=s), '--color', 'always'])
        self.assertIn('\033[31mMISSING', out)
        rc, out = self.run_check(['--idf', self.idf('idf2', structs=s), '--color', 'never'])
        self.assertNotIn('\033[', out)

    def test_per_ref_struct_status(self):
        old = self.idf('old', version=(5, 5, 0))
        new = self.idf('new', structs=self.with_fields(wifi_ap_config_t='uint8_t later;'))
        m = {k: dict(v, fields=dict(v['fields'])) if 'fields' in v else v for k, v in BASE_MANIFEST.items()}
        m['wifi_ap_config_t']['fields']['later'] = {'gap': 'todo'}
        html = self.tmp / 'r.html'
        rc, out = self.run_check(['--idf', old, '--idf', new, '--html', str(html)], self.manifest(m))
        self.assertEqual(rc, 0, out)
        row = [x for x in html.read_text().splitlines() if '<td class="nm">wifi_ap_config_t ' in x][0]
        self.assertLess(row.index('c ok'), row.index('c gap'), row)  # old ref carried, new ref gap


class Apis(Base):
    def test_supported_via_remote_hook(self):
        self.glue(BASE_GLUE + 'esp_err_t esp_wifi_remote_foo(int x)\n{\n    return eh(x);\n}\n')
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra='esp_err_t esp_wifi_foo(int x);\n')])
        self.assertEqual(rc, 0, out)
        self.assertRegex(out, r'3 APIs \(3 supported\)')

    def test_missing_api(self):
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra='esp_err_t esp_wifi_bar(void);\n')])
        self.assertEqual(rc, 1, out)
        self.assertTrue(out.startswith('esp_wifi.h\n  MISSING API'), out)  # failing file first
        self.assertRegex(out, r'MISSING API\s+esp_wifi_bar\(\)\s+idf \[all targets\]  -- '
                              r'hosted does not define it')

    def test_direct_definition_in_non_forwarded_header(self):
        he = {f'{wsc.WIFI_INC}/esp_wifi_he.h': '#pragma once\n#include "esp_err.h"\nesp_err_t esp_wifi_sta_x(int a);\n'}
        self.glue(BASE_GLUE + 'esp_err_t esp_wifi_sta_x(int a) { return eh_x(a); }\n')
        rc, out = self.run_check(['--idf', self.idf(headers=he)])
        self.assertEqual(rc, 0, out)

    def test_remote_hook_does_not_count_for_non_forwarded_header(self):
        he = {f'{wsc.WIFI_INC}/esp_wifi_he.h': '#pragma once\n#include "esp_err.h"\nesp_err_t esp_wifi_sta_x(int a);\n'}
        self.glue(BASE_GLUE + 'esp_err_t esp_wifi_remote_sta_x(int a) { return eh_x(a); }\n')
        rc, out = self.run_check(['--idf', self.idf(headers=he)])
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'MISSING API\s+esp_wifi_sta_x\(\)')

    def test_in_tree_forwarder_list_is_used(self):
        fwd = {f'{wsc.REMOTE_DIR}/esp_wifi_with_remote.c':
               'esp_err_t esp_wifi_init(const wifi_init_config_t *c)\n{\n    return esp_wifi_remote_init(c);\n}\n'
               'esp_err_t esp_wifi_set_config(int i, void *c) { return esp_wifi_remote_set_config(i, c); }\n'}
        self.glue(BASE_GLUE + 'esp_err_t esp_wifi_remote_foo(int x) { return eh(x); }\n')
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra='esp_err_t esp_wifi_foo(int x);\n',
                                                    headers=fwd)])
        self.assertEqual(rc, 1, out)  # IDF has no esp_wifi_foo forwarder, so the hook is unreachable
        self.assertRegex(out, r'MISSING API\s+esp_wifi_foo\(\)')

    def test_stub_is_reported_not_failed(self):
        self.glue(BASE_GLUE + textwrap.dedent('''\
            esp_err_t esp_wifi_remote_foo(int x)
            {
                (void)x;
                return ESP_ERR_NOT_SUPPORTED;
            }
            esp_err_t esp_wifi_remote_baz(void) { UNSUPPORTED_RET(); }
            '''))
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra='esp_err_t esp_wifi_foo(int x);\n'
                                                                   'esp_err_t esp_wifi_baz(void);\n')])
        self.assertEqual(rc, 0, out)
        self.assertRegex(out, r'STUB\s+esp_wifi_foo\(\)\s+idf \[all targets\]  -- hosted returns ESP_ERR_NOT_SUPPORTED')
        self.assertRegex(out, r'STUB\s+esp_wifi_baz\(\)')

    def test_dead_code_and_calls_are_not_definitions(self):
        self.glue(BASE_GLUE + textwrap.dedent('''\
            #if 0
            esp_err_t esp_wifi_remote_a(void) { return eh_a(); }
            #endif
            /* esp_err_t esp_wifi_remote_b(void) { return 0; } */
            // esp_err_t esp_wifi_remote_c(void) { return 0; }
            static void caller(void)
            {
                if (esp_wifi_remote_d()) {
                    return;
                }
            }
            '''))
        decls = ''.join(f'esp_err_t esp_wifi_{n}(void);\n' for n in 'abcd')
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra=decls)])
        self.assertEqual(rc, 1, out)
        for n in 'abcd':
            self.assertRegex(out, rf'MISSING API\s+esp_wifi_{n}\(\)')

    def test_return_type_on_previous_line(self):
        self.glue(BASE_GLUE + 'static esp_err_t\nesp_wifi_remote_a(void)\n{\n    return eh_a();\n}\n')
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra='esp_err_t esp_wifi_a(void);\n')])
        self.assertEqual(rc, 0, out)

    def test_any_public_header_is_checked(self):
        hdrs = {f'{wsc.WIFI_INC}/esp_wifi_brand_new.h': '#pragma once\n#include "esp_err.h"\n'
                'typedef struct { int k; } wifi_brand_new_cfg_t;\nesp_err_t esp_wifi_brand_new(void);\n',
                f'{wsc.SUPP_INC}/esp_supp_new.h': '#pragma once\n#include "esp_err.h"\nesp_err_t esp_supp_new(void);\n'}
        rc, out = self.run_check(['--idf', self.idf(headers=hdrs)])
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'NEW STRUCT\s+wifi_brand_new_cfg_t\s+idf \[all targets\]  -- classify it')
        self.assertRegex(out, r'MISSING API\s+esp_wifi_brand_new\(\)')
        self.assertRegex(out, r'MISSING API\s+esp_supp_new\(\)\s+idf \[all targets\]  -- hosted does not define it')

    def test_header_missing_from_parse_is_error(self):
        real = wsc.preprocess

        def drop_one(inc, stub, cfg, cpp, headers):
            return real(inc, stub, cfg, cpp, headers - {'esp_wifi_types.h'})
        wsc.preprocess = drop_one
        try:
            rc, out = self.run_check(['--idf', self.idf(headers={f'{wsc.WIFI_INC}/esp_extra.h': '#pragma once\n'})])
        finally:
            wsc.preprocess = real
        self.assertEqual(rc, 0, out)  # esp_wifi_types.h still arrives through esp_wifi.h
        wsc.preprocess = lambda inc, stub, cfg, cpp, headers: real(inc, stub, cfg, cpp, headers - {'esp_extra.h'})
        try:
            rc, out = self.run_check(['--idf', self.idf('idf2', headers={f'{wsc.WIFI_INC}/esp_extra.h': '#pragma once\n'})])
        finally:
            wsc.preprocess = real
        self.assertEqual(rc, 2, out)
        self.assertIn("headers not parsed: ['esp_extra.h']", out)

    def test_duplicate_header_name_is_error(self):
        hdrs = {f'{wsc.SUPP_INC}/esp_wifi.h': '#pragma once\n'}
        rc, out = self.run_check(['--idf', self.idf(headers=hdrs)])
        self.assertEqual(rc, 2, out)

    def test_if0_else_branch_counts(self):
        self.glue(BASE_GLUE + '#if 0\nint x;\n#else\nesp_err_t esp_wifi_remote_a(void) { return eh_a(); }\n#endif\n')
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra='esp_err_t esp_wifi_a(void);\n')])
        self.assertEqual(rc, 0, out)

    def test_classified_api_passes_and_prints(self):
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra='esp_err_t esp_wifi_csi(void);\n')],
                                 self.manifest(apis={'esp_wifi_csi': {'unsupported': 'CSI not offered'}}))
        self.assertEqual(rc, 0, out)
        self.assertIn('NOT CARRIED  CSI not offered: esp_wifi_csi()', out)

    def test_api_gap_prints(self):
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra='esp_err_t esp_wifi_okc(void);\n')],
                                 self.manifest(apis={'esp_wifi_okc': {'gap': 'glue missing'}}))
        self.assertEqual(rc, 0, out)
        self.assertRegex(out, r'GAP\s+esp_wifi_okc\(\)\s+idf \[all targets\]  -- glue missing')

    def test_stale_entry_fails(self):
        self.glue(BASE_GLUE + 'esp_err_t esp_wifi_remote_csi(void) { return eh_csi(); }\n')
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra='esp_err_t esp_wifi_csi(void);\n')],
                                 self.manifest(apis={'esp_wifi_csi': {'unsupported': 'CSI not offered'}}))
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'STALE API\s+esp_wifi_csi\(\).*manifest says unsupported')

    def test_local_entry_on_defined_api_is_fine(self):
        self.glue(BASE_GLUE + 'esp_err_t esp_wifi_remote_csi(void) { return eh_csi(); }\n')
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra='esp_err_t esp_wifi_csi(void);\n')],
                                 self.manifest(apis={'esp_wifi_csi': {'local': 'runs on host'}}))
        self.assertEqual(rc, 0, out)

    def test_header_entry_covers_apis_and_structs(self):
        now = {f'{wsc.WIFI_INC}/esp_now.h': '#pragma once\n#include "esp_err.h"\n'
               'typedef struct { int a; } esp_now_peer_t;\nesp_err_t esp_now_init(void);\n'}
        rc, out = self.run_check(['--idf', self.idf(headers=now)],
                                 self.manifest(headers={'esp_now.h': {'unsupported': 'no ESP-NOW'}}))
        self.assertEqual(rc, 0, out)
        self.assertIn('esp_now.h\n  NOT CARRIED  no ESP-NOW: whole header: 1 APIs, 1 structs', out)
        rc, out = self.run_check(['--idf', self.idf('idf_v', headers=now), '-v'],
                                 self.manifest(headers={'esp_now.h': {'unsupported': 'no ESP-NOW'}}))
        self.assertIn('NOT CARRIED  no ESP-NOW: esp_now_init(); esp_now_peer_t', out)
        rc, out = self.run_check(['--idf', self.idf('idf2', headers=now)])
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'NEW STRUCT\s+esp_now_peer_t')
        self.assertRegex(out, r'MISSING API\s+esp_now_init\(\)')

    def test_config_gated_api_and_field_found(self):
        extra = '#if CONFIG_ESP_WIFI_FTM_ENABLE\nesp_err_t esp_wifi_ftm(void);\n#endif\n'
        s = self.with_fields(wifi_ap_config_t='#ifdef CONFIG_ESP_WIFI_GCMP\nuint8_t gcmp;\n#endif')
        rc, out = self.run_check(['--idf', self.idf(structs=s, esp_wifi_extra=extra)])
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'MISSING API\s+esp_wifi_ftm\(\)\s+idf \[all targets\]')
        self.assertRegex(out, r'MISSING\s+wifi_ap_config_t\.gcmp\s+idf \[all targets\]')

    def test_negated_config_branch_also_found(self):
        extra = '#if !CONFIG_ESP_WIFI_X\nesp_err_t esp_wifi_only_without_x(void);\n#endif\n'
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra=extra)])
        self.assertRegex(out, r'MISSING API\s+esp_wifi_only_without_x\(\)')

    def test_target_only_api(self):
        extra = '#if CONFIG_SOC_WIFI_HE_SUPPORT\nesp_err_t esp_wifi_he_only(void);\n#endif\n'
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra=extra)])
        self.assertRegex(out, r'MISSING API\s+esp_wifi_he_only\(\)\s+idf \[c5\]')

    def test_api_in_newer_ref_only(self):
        old = self.idf('old', version=(5, 5, 0))
        new = self.idf('new', esp_wifi_extra='esp_err_t esp_wifi_new(void);\n')
        rc, out = self.run_check(['--idf', old, '--idf', new])
        self.assertEqual(rc, 1, out)
        line = [x for x in out.splitlines() if 'esp_wifi_new' in x][0]
        self.assertIn('new [all targets]', line)
        self.assertNotIn('old', line.split('--')[0].replace('esp_wifi_new', ''))

    def test_unused_api_entry_is_info(self):
        rc, out = self.run_check(['--idf', self.idf(), '-v'],
                                 self.manifest(apis={'esp_wifi_future': {'gap': 'later'}}))
        self.assertEqual(rc, 0, out)
        self.assertIn('info: apis.esp_wifi_future is in no checked IDF', out)

    def test_sentinel_api_missing(self):
        root = make_idf(self.tmp / 'idf')
        h = root / wsc.WIFI_INC / 'esp_wifi.h'
        h.write_text(h.read_text().replace('esp_err_t esp_wifi_set_config(int interface, void *conf);\n', ''))
        rc, out = self.run_check(['--idf', str(root)])
        self.assertEqual(rc, 2, out)

    def test_no_hosted_definitions(self):
        self.glue('int x;\n')
        rc, out = self.run_check(['--idf', self.idf()])
        self.assertEqual(rc, 2, out)

    def test_schema_api_entries(self):
        for kw in ({'apis': {'esp_wifi_a': {'unknown': 'x'}}}, {'apis': {'esp_wifi_a': {'gap': ''}}},
                   {'apis': {'esp_wifi_a': {'gap': 'x', 'skip': 'y'}}}, {'headers': {'nope': {'skip': 'x'}}},
                   {'apis': {'esp_wifi_a': 'carried'}}):
            with self.subTest(kw=kw):
                rc, out = self.run_check(['--idf', self.idf()], self.manifest(**kw))
                self.assertEqual(rc, 2, out)

    def test_html_api_section(self):
        html = self.tmp / 'r.html'
        rc, out = self.run_check(['--idf', self.idf(esp_wifi_extra='esp_err_t esp_wifi_bar(void);\n'),
                                  '--html', str(html)])
        text = html.read_text()
        self.assertIn('<td class="nm">esp_wifi_bar()</td>', text)
        self.assertIn('MISSING', text)


class Git(Base):
    def git(self, repo, *args):
        subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)

    def make_repo(self):
        repo = self.tmp / 'repo'
        make_idf(repo, version=(5, 5, 0))
        self.git(repo, 'init', '-q', '-b', 'release/v5.5')
        self.git(repo, 'config', 'user.email', 't@t')
        self.git(repo, 'config', 'user.name', 't')
        self.git(repo, 'config', 'uploadpack.allowFilter', 'true')
        self.git(repo, 'add', '-A')
        self.git(repo, 'commit', '-q', '-m', 'old')
        self.git(repo, 'checkout', '-q', '-b', 'master')
        make_idf(repo, structs=self.with_fields(wifi_ap_config_t='uint8_t new_knob;'), version=(6, 2, 0))
        self.git(repo, 'commit', '-q', '-am', 'new')
        sha = lambda r: subprocess.run(['git', '-C', str(repo), 'rev-parse', r], capture_output=True,
                                       text=True, check=True).stdout.strip()
        return repo, sha('release/v5.5'), sha('master')

    def test_git_refs(self):
        repo, old, new = self.make_repo()
        rc, out = self.run_check(['--idf-repo', str(repo), '--ref', 'release/v5.5'])
        self.assertEqual(rc, 0, out)
        rc, out = self.run_check(['--idf-repo', str(repo), '--ref', 'release/v5.5', '--ref', 'master'])
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r'MISSING\s+wifi_ap_config_t\.new_knob\s+master \[all targets\]')

    def test_bad_ref(self):
        repo, _, _ = self.make_repo()
        rc, out = self.run_check(['--idf-repo', str(repo), '--ref', 'nope'])
        self.assertEqual(rc, 2, out)

    def refs_file(self, url, pairs):
        p = self.tmp / 'refs.yml'
        import yaml
        p.write_text(yaml.safe_dump({'url': url, 'refs': [{'branch': b, 'sha': s} for b, s in pairs]}))
        return p

    def test_refs_file_pinned_and_live(self):
        repo, old, new = self.make_repo()
        url = f'file://{repo}'
        pinned = self.refs_file(url, [('release/v5.5', old)])
        rc, out = self.run_check(['--refs-file', str(pinned), '--cache-dir', str(self.tmp / 'c1')])
        self.assertEqual(rc, 0, out)
        both = self.refs_file(url, [('release/v5.5', old), ('master', old)])
        rc, out = self.run_check(['--refs-file', str(both), '--cache-dir', str(self.tmp / 'c2')])
        self.assertEqual(rc, 0, out)  # master pinned to the old SHA: IDF moving on must not fail
        rc, out = self.run_check(['--refs-file', str(both), '--live', '--cache-dir', str(self.tmp / 'c3')])
        self.assertEqual(rc, 1, out)  # live master head has the new field
        self.assertRegex(out, r'MISSING\s+wifi_ap_config_t\.new_knob\s+master')

    def test_refs_file_needs_sha_unless_live(self):
        p = self.tmp / 'r.yml'
        p.write_text('url: x\nrefs:\n  - branch: master\n')
        rc, out = self.run_check(['--refs-file', str(p)])
        self.assertEqual(rc, 2, out)

    def test_pinned_cache_works_offline(self):
        repo, old, _ = self.make_repo()
        cache = self.tmp / 'c5'
        rc, out = self.run_check(['--refs-file', str(self.refs_file(f'file://{repo}', [('release/v5.5', old)])),
                                  '--cache-dir', str(cache)])
        self.assertEqual(rc, 0, out)
        gone = self.refs_file(f'file://{self.tmp}/no_repo', [('release/v5.5', old)])
        rc, out = self.run_check(['--refs-file', str(gone), '--cache-dir', str(cache)])
        self.assertEqual(rc, 0, out)

    def test_fetch_failure(self):
        p = self.refs_file(f'file://{self.tmp}/no_repo', [('master', '0' * 40)])
        rc, out = self.run_check(['--refs-file', str(p), '--cache-dir', str(self.tmp / 'c4')])
        self.assertEqual(rc, 2, out)


class RealTree(unittest.TestCase):
    """The checked-in manifest and the real hosted sources agree with each other."""

    def test_schema_and_wire(self):
        m = wsc.load_manifest(wsc.DEFAULT_MANIFEST)
        errs = wsc.check_wire_claims(m['structs'], wsc.PROTO.read_text(), wsc.HOST_BITS.read_text(),
                                     wsc.CP_BITS.read_text())
        self.assertEqual(errs, [])

    def test_hosted_definitions_found(self):
        defs = wsc.hosted_definitions()
        for name, kind in (('esp_wifi_remote_init', 'impl'), ('esp_wifi_remote_set_config', 'impl'),
                           ('esp_wifi_sta_itwt_setup', 'impl'), ('esp_wifi_sta_btwt_setup', 'stub')):
            self.assertEqual(defs.get(name), kind, name)


if __name__ == '__main__':
    unittest.main(verbosity=1)
