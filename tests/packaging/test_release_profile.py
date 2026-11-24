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
        self.assertIn(b'Alexander I. Lebedev', notice)
        self.assertIn(b'Kevin Atkinson', notice)
        self.assertIn(b'Princeton University', notice)
        self.assertEqual(model[8 + n:], b'RSKNmodel')
        meta = json.loads(out['build/experiment-002-artifacts/artifact-metadata.json'])
        self.assertEqual(meta['artifacts']['model']['sha256'], hashlib.sha256(model).hexdigest())
        self.assertEqual(meta['artifacts']['model']['bytes'], len(model))
        self.assertTrue(meta['artifacts']['exactFullLexicons']['format'].startswith('RSKX v1'))


if __name__ == '__main__':
    unittest.main()
