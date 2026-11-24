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
    def setUp(self):
        spec = importlib.util.spec_from_file_location('pins', ROOT / 'tools/check_profile_pins.py')
        self.gate = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.gate)

    def test_pins_cover_every_pinned_build_file(self):
        pins = self.gate.production_pins()
        self.assertEqual(len(pins), 8)  # two .dic, two .aff, two control .aff, model, word list
        listed = [x for x in (ROOT / 'packaging/arch/profile-files.txt').read_text().splitlines()
                  if x.startswith('build/')]
        self.assertTrue(set(pins) <= set(listed))
        self.assertEqual(self.gate.control_header(), load_tool().CONTROL_AFF_HEADER)

    def test_pre_sys256_profile_refused(self):
        # The experiment profile: right paths, bare RSKN/RSKP, unmarked control .aff.
        import tempfile
        tool = load_tool()
        pins = self.gate.production_pins()
        with tempfile.TemporaryDirectory() as tmp:
            for rel in pins:
                path = Path(tmp) / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                old = {'.rskn': b'RSKN', '.rskp': b'RSKP'}.get(path.suffix, b'SET UTF-8\n')
                path.write_bytes(old)
            errors = self.gate.check(tmp)
        self.assertEqual(sum('no embedded notices' in e for e in errors), 2)
        self.assertEqual(sum('control .aff not marked' in e for e in errors), 2)
        self.assertEqual(sum('does not match its pin' in e for e in errors), 8)
        del tool


if __name__ == '__main__':
    unittest.main()
