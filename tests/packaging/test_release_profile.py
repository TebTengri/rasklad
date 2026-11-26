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

    def test_release_statements_equal_addon_constants(self):
        """SYS-256: the addon compares the scope and MODIFICATIONS.txt word for word."""
        header = (ROOT / 'src/profileverify.h').read_text()
        def constant(name):
            block = re.search(name + r'\[\] =\s*((?:"(?:[^"\\]|\\.)*"\s*)+);', header).group(1)
            return ''.join(ast.literal_eval(part) for part in re.findall(r'"(?:[^"\\]|\\.)*"', block))
        tool = load_tool()
        self.assertEqual(tool.REVIEW, constant('kDistributionReview'))
        self.assertEqual(tool.RELEASE_SCOPE, constant('kReleaseScope'))
        self.assertEqual(tool.RELEASE_MODIFICATIONS, constant('kReleaseModifications').encode())
        product = json.loads((ROOT / 'data/product-profile-sources.json').read_text())
        self.assertIs(product['release_approved'], True)
        self.assertEqual(product['distribution_review'], tool.REVIEW)
        self.assertTrue((ROOT / tool.REVIEW).is_file())
        self.assertIn(tool.REVIEW, (ROOT / 'packaging/arch/profile-files.txt').read_text().split())

    def test_update_profile_is_not_approved(self):
        """A dictionary update keeps release_approved=false until a new review."""
        tool = load_tool()
        data = {rel: b'' for rel in tool.SOURCE_PINS}
        data['build/experiment-002-artifacts/artifact-metadata.json'] = json.dumps(
            {'artifacts': {'model': {}, 'exactFullLexicons': {}}, 'scope': 'local; release_approved=false'}).encode()
        data['build/experiment-002-artifacts/MODIFICATIONS.txt'] = b'release_approved=false\n'
        # Even a source that claims the approval comes out unapproved.
        data['build/experiment-002-artifacts/MODIFICATIONS.txt'] = tool.RELEASE_MODIFICATIONS
        data['build/experiment-002-artifacts/artifact-metadata.json'] = json.dumps(
            {'artifacts': {'model': {}, 'exactFullLexicons': {}}, 'scope': tool.RELEASE_SCOPE}).encode()
        out = tool.build_profile(data, approved=False)
        self.assertEqual(out['build/experiment-002-artifacts/MODIFICATIONS.txt'], tool.UNREVIEWED_MODIFICATIONS)
        self.assertIn(b'release_approved=false', tool.UNREVIEWED_MODIFICATIONS)
        self.assertNotIn(b'release_approved=true', tool.UNREVIEWED_MODIFICATIONS)
        meta = json.loads(out['build/experiment-002-artifacts/artifact-metadata.json'])
        self.assertEqual(meta['scope'], tool.UNREVIEWED_SCOPE)
        approved = tool.build_profile(data)
        self.assertEqual(approved['build/experiment-002-artifacts/MODIFICATIONS.txt'], tool.RELEASE_MODIFICATIONS)

    def test_approval_follows_the_product_passport(self):
        """After update_dictionaries pin the passport says false: no approval is written."""
        import tempfile
        tool = load_tool()
        self.assertTrue(tool.passport_approved())
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / 'data').mkdir()
            passport = json.loads((ROOT / 'data/product-profile-sources.json').read_text())
            tool.ROOT = Path(tmp)
            for value, review in ((False, None), (True, None), (True, 'data/notices/OTHER.md'), ('true', tool.REVIEW)):
                p = dict(passport, release_approved=value)
                p.pop('distribution_review')
                if review:
                    p['distribution_review'] = review
                (Path(tmp) / 'data/product-profile-sources.json').write_text(json.dumps(p))
                self.assertFalse(tool.passport_approved(), (value, review))
            # A duplicate member: Python would keep the last (approving) value.
            p = dict(passport)
            text = json.dumps(p)
            text = '{"release_approved": false, ' + text[1:]
            (Path(tmp) / 'data/product-profile-sources.json').write_text(text)
            self.assertFalse(tool.passport_approved())
            (Path(tmp) / 'data/product-profile-sources.json').write_text(json.dumps(p))
            self.assertTrue(tool.passport_approved())

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
        wrapper = (ROOT / 'tools/build_arch_package.sh').read_text()
        self.assertIn('makepkg --cleanbuild --check', wrapper)


if __name__ == '__main__':
    unittest.main()
