# SPDX-License-Identifier: MIT
"""SYS-256: tools/make_release_profile.py and the addon agree on the marks."""
import ast
import hashlib
import importlib.util
import json
import re
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_tool():
    spec = importlib.util.spec_from_file_location('release_profile', ROOT / 'tools/make_release_profile.py')
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    return tool


class ReleaseProfile(unittest.TestCase):
    def test_control_header_equals_addon_constant(self):
        header = (ROOT / 'src/profileverify.h').read_text()
        block = re.search(r'kControlAffHeader\[\] =\s*((?:"(?:[^"\\]|\\.)*"\s*)+);', header).group(1)
        text = ''.join(ast.literal_eval(part) for part in re.findall(r'"(?:[^"\\]|\\.)*"', block))
        self.assertEqual(load_tool().CONTROL_AFF_HEADER, text.encode())

    def test_container_and_metadata(self):
        tool = load_tool()
        data = {rel: b'' for rel in tool.SOURCE_PINS}
        data['build/experiment-002-artifacts/en_US.NOTICE.txt'] = b'EN NOTICE Kevin Atkinson\n'
        data['build/experiment-002-artifacts/ru_RU.NOTICE.txt'] = b'RU NOTICE Lebedev\n'
        data['build/experiment-003-data/control/en.aff'] = b'SET UTF-8\n'
        data['build/experiment-003-data/control/ru.aff'] = b'SET UTF-8\n'
        data['build/experiment-002-artifacts/detector-model.rskn'] = b'RSKNmodel'
        data['build/experiment-002-artifacts/exact-full-lexicons.rskp'] = b'RSKPstem'
        data['build/experiment-002-artifacts/artifact-metadata.json'] = json.dumps(
            {'artifacts': {'model': {}, 'exactFullLexicons': {}}}).encode()
        out = tool.build_profile(data)
        self.assertEqual(out['build/experiment-003-data/control/ru.aff'], tool.CONTROL_AFF_HEADER + b'SET UTF-8\n')
        model = out['build/experiment-002-artifacts/detector-model.rskn']
        self.assertEqual(model[:4], b'RSKX')
        (n,) = struct.unpack('<I', model[4:8])
        notice = model[8:8 + n]
        # Exactly the verified source notices, not the checkout's copies.
        self.assertIn(b'\nEN NOTICE Kevin Atkinson\n', notice)
        self.assertTrue(notice.endswith(b'\nRU NOTICE Lebedev\n'))
        self.assertEqual(model[8 + n:], b'RSKNmodel')
        stem = out['build/experiment-002-artifacts/exact-full-lexicons.rskp']
        (m,) = struct.unpack('<I', stem[4:8])
        self.assertIn(b'EN NOTICE', stem[8:8 + m])
        self.assertIn(b'RU NOTICE', stem[8:8 + m])
        self.assertEqual(stem[8 + m:], b'RSKPstem')
        meta = json.loads(out['build/experiment-002-artifacts/artifact-metadata.json'])
        self.assertEqual(meta['artifacts']['model']['sha256'], hashlib.sha256(model).hexdigest())
        self.assertEqual(meta['artifacts']['model']['bytes'], len(model))
        self.assertTrue(meta['artifacts']['exactFullLexicons']['format'].startswith('RSKX v1'))


class PackagingGate(unittest.TestCase):
    def test_pkgbuild_check_runs_the_addon_verifier(self):
        text = (ROOT / 'packaging/arch/PKGBUILD').read_text()
        check = text[text.index('check() {'):text.index('package() {')]
        self.assertIn('./build/profile-gate/profile_gate "${srcdir}/profile"', check)
        self.assertIn('qmake6 ../../tests/profile_gate/profile_gate.pro', text)
        gate = (ROOT / 'tests/profile_gate/profile_gate.cpp').read_text()
        self.assertIn('verifyProfileFiles(', gate)
        self.assertIn('productionPins()', gate)


if __name__ == '__main__':
    unittest.main()
