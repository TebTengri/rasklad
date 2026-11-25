# SPDX-License-Identifier: MIT
"""SYS-272: tools/update_dictionaries.py, offline.

`pin` runs on a copy of the pinned files with a synthetic work directory (the
current release profile with changed dictionary bytes): every old pin of a
changed file is gone and every new one is in place. No network, no training.
"""
import hashlib
import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
PROFILE = ROOT / 'build/release-profile'
NEW = 'fedcba9876543210fedcba9876543210fedcba98'
PINNED = ('data/sources.json', 'data/evaluation-002-sources.json', 'data/morphology-003-sources.json',
          'data/product-profile-sources.json', 'data/experiment-002-protocol.json',
          'data/notices/en_US.README.txt', 'data/notices/ru_RU.README.txt', 'data/notices/Tatoeba-CC0-REVIEW.md',
          'src/profileverify.cpp', 'tools/make_release_profile.py', 'tests/app/app_tests.cpp',
          'README.md', 'README.en.md', 'packaging/arch/PROFILE.md', 'packaging/arch/DATA-NOTICE.txt')


def load(name, rel):
    spec = importlib.util.spec_from_file_location(name, ROOT / rel)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha(data):
    return hashlib.sha256(data).hexdigest()


@unittest.skipUnless((PROFILE / 'build').is_dir(), 'needs the release profile in build/release-profile')
class Pin(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        self.root = self.tmp / 'repo'
        for rel in PINNED:
            (self.root / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / rel, self.root / rel)
        (self.root / 'docs/results').mkdir(parents=True)
        self.tool = load('update_dictionaries', 'tools/update_dictionaries.py')
        self.work = self.tmp / 'work'
        self.tool.work_dir = lambda commit: self.work
        release = load('release_profile', 'tools/make_release_profile.py')
        # The "new" source profile: the pinned files with one byte more in each
        # dictionary and .aff; the release profile built from it as the tool does.
        source = {}
        for rel in release.SOURCE_PINS:
            data = (PROFILE / rel).read_bytes()
            if '/control/' in rel:
                data = data[len(release.CONTROL_AFF_HEADER):]
            elif rel.endswith(('.rskn', '.rskp')):
                data = data[8 + int.from_bytes(data[4:8], 'little'):]
            if rel.endswith(('.dic', '.aff')):
                data += b'\n'
            source[rel] = data
        built = release.build_profile(source, NEW)
        for sub, files in (('profile', source), ('release', built)):
            for rel, data in files.items():
                (self.work / sub / rel).parent.mkdir(parents=True, exist_ok=True)
                (self.work / sub / rel).write_bytes(data)
        self.source = source
        stage = self.work / 'profile/data'
        (stage / 'notices').mkdir(parents=True)
        for name in ('en_US.README.txt', 'ru_RU.README.txt'):
            shutil.copyfile(ROOT / 'data/notices' / name, stage / 'notices' / name)
        sources = json.loads((ROOT / 'data/sources.json').read_text())
        sources['commit'] = NEW
        (stage / 'sources.json').write_text(json.dumps(sources, indent=2) + '\n')
        shutil.copyfile(ROOT / 'data/evaluation-002-sources.json', stage / 'evaluation-002-sources.json')
        (self.work / 'source-pins.json').write_text(json.dumps(
            {rel: [sha(d), len(d)] for rel, d in source.items()}))
        (self.work / 'report.json').write_text('{"methods": {}}\n')

    def test_every_changed_pin_moves(self):
        old_commit = json.loads((ROOT / 'data/sources.json').read_text())['commit']
        old_dic = sha((PROFILE / 'build/experiment-data/en.dic').read_bytes())
        old_model = sha((PROFILE / 'build/experiment-002-artifacts/detector-model.rskn').read_bytes())
        self.tool.pin_into(NEW, self.root)
        new_dic = sha(self.source['build/experiment-data/en.dic'])
        new_model = sha((self.work / 'release/build/experiment-002-artifacts/detector-model.rskn').read_bytes())
        for rel in ('src/profileverify.cpp', 'data/product-profile-sources.json', 'data/morphology-003-sources.json'):
            text = (self.root / rel).read_text()
            self.assertNotIn(old_dic, text, rel)
            self.assertIn(new_dic, text, rel)
            self.assertNotIn(old_commit, text, rel)
        cpp = (self.root / 'src/profileverify.cpp').read_text()
        self.assertNotIn(old_model, cpp)
        self.assertIn(new_model, cpp)
        tests = (self.root / 'tests/app/app_tests.cpp').read_text()
        self.assertIn(new_model, tests)
        self.assertNotIn(old_model, tests)
        # The addon pins the manifests' own bytes: they must be the files as written.
        for rel in ('data/product-profile-sources.json', 'data/sources.json'):
            self.assertIn(sha((self.root / rel).read_bytes()), cpp, rel)
        tool = (self.root / 'tools/make_release_profile.py').read_text()
        self.assertIn("DICTIONARY_COMMIT = '%s'" % NEW, tool)
        self.assertIn(new_dic, tool)
        for rel in ('README.md', 'packaging/arch/DATA-NOTICE.txt'):
            text = (self.root / rel).read_text()
            self.assertNotIn(old_commit[:12], text, rel)
            self.assertIn(NEW[:12], text, rel)
        self.assertTrue((self.root / 'docs/results' / ('dictionary-update-%s.json' % NEW[:12])).is_file())

    def test_refuses_the_same_commit(self):
        old_commit = json.loads((ROOT / 'data/sources.json').read_text())['commit']
        with self.assertRaises(SystemExit):
            self.tool.pin_into(old_commit, self.root)


class Steps(unittest.TestCase):
    def setUp(self):
        self.tool = load('update_dictionaries', 'tools/update_dictionaries.py')

    def test_fetch_needs_the_network_flag(self):
        with self.assertRaises(SystemExit) as raised:
            self.tool.fetch(NEW, network=False)
        self.assertIn('--network', str(raised.exception))

    def test_plan_names_https_sources_only(self):
        for url, _ in self.tool.plan(NEW):
            self.assertTrue(url.startswith('https://'), url)

    def test_redirect_only_to_https_on_the_same_host(self):
        import urllib.request
        handler = self.tool.SameHostHttps()
        req = urllib.request.Request('https://downloads.tatoeba.org/a')
        for bad in ('http://downloads.tatoeba.org/a', 'https://example.org/a'):
            with self.assertRaises(SystemExit):
                handler.redirect_request(req, None, 302, 'Found', {}, bad)
        self.assertIsNotNone(handler.redirect_request(req, None, 302, 'Found', {}, 'https://downloads.tatoeba.org/b'))

    def test_plan_includes_the_pinned_dictionaries(self):
        names = [name for _, name in self.tool.plan(NEW)]
        self.assertIn('old-en.dic', names)
        self.assertIn('old-ru.dic', names)

    def test_compare_flags_a_drop(self):
        self.assertTrue(self.tool.worse(0.0004, 0.0010, True, 0.0005))
        self.assertFalse(self.tool.worse(0.0004, 0.0008, True, 0.0005))
        self.assertTrue(self.tool.worse(0.65, 0.63, False, 0.01))
        self.assertFalse(self.tool.worse(0.65, 0.70, False, 0.01))
        self.assertTrue(self.tool.worse(0.0, 0.0001, True, 0.0))  # ambiguous words never change
        part = {'valid': {'count': 10, 'falseChangeRate': 0.0},
                'wrong': {'count': 10, 'recoveryRate': 0.9, 'wrongConversionRate': 0.0}}
        unit = dict(part, byIntendedLanguage={'en': part, 'ru': part})
        report = {'loaded': True, 'methods': {'production': {'occurrences': unit, 'uniqueTypes': unit}}}
        rates = self.tool.rates(report)
        self.assertEqual(len(rates), len(self.tool.SCOPES) * len(self.tool.HEADLINE) * 2)

    def test_evaluate_stops_on_a_profile_the_addon_rejects(self):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp)
        fake = tmp / 'product_eval'
        fake.write_text('#!/bin/sh\necho \'{"loaded":false,"error":"hash mismatch"}\'\nexit 3\n')
        fake.chmod(0o755)
        with self.assertRaises(SystemExit) as raised:
            self.tool.evaluate(fake, tmp, tmp, 'new')
        self.assertIn('reject', str(raised.exception))


class Gates(unittest.TestCase):
    """pin writes nothing without a passing compare on the same HEAD and inputs."""

    def setUp(self):
        self.tool = load('update_dictionaries', 'tools/update_dictionaries.py')
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        self.tool.work_dir = lambda commit: self.tmp
        self.tool.git_head = lambda: 'a' * 40
        self.tool.pin_into = lambda commit, root: self.fail('pin_into must not run')

    def write(self, **record):
        (self.tmp / 'compare.json').write_text(json.dumps(dict({'commit': NEW, 'head': 'a' * 40}, **record)))

    def refused(self, needle):
        with self.assertRaises(SystemExit) as raised:
            self.tool.pin(NEW)
        self.assertIn(needle, str(raised.exception))

    def test_without_compare(self):
        self.refused('run compare first')

    def test_after_a_failed_compare(self):
        self.write(passed=False)
        self.refused('did not pass')

    def test_after_head_moved(self):
        self.write(passed=True, head='b' * 40)
        self.refused('HEAD moved')

    def test_after_the_work_files_changed(self):
        self.tool.inputs = lambda work: {'release/x': '1'}
        self.write(passed=True, inputs={'release/x': '0'})
        self.refused('changed since compare')


class WorkDir(unittest.TestCase):
    def setUp(self):
        self.tool = load('update_dictionaries', 'tools/update_dictionaries.py')
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        self.tool.ROOT = self.tmp / 'repo'
        (self.tool.ROOT / 'build').mkdir(parents=True)
        (self.tmp / 'elsewhere').mkdir()

    def test_a_symlinked_component_is_refused(self):
        (self.tool.ROOT / 'build/dict-update').symlink_to(self.tmp / 'elsewhere')
        with self.assertRaises(SystemExit):
            self.tool.work_dir(NEW)
        (self.tool.ROOT / 'build/dict-update').unlink()
        (self.tool.ROOT / 'build/dict-update').mkdir()
        (self.tool.ROOT / 'build/dict-update' / NEW[:12]).symlink_to(self.tmp / 'elsewhere')
        with self.assertRaises(SystemExit):
            self.tool.work_dir(NEW)

    def test_a_plain_path_is_accepted(self):
        self.assertEqual(self.tool.work_dir(NEW), self.tool.ROOT / 'build/dict-update' / NEW[:12])

if __name__ == '__main__':
    unittest.main()
