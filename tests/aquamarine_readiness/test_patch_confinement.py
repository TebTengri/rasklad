# SPDX-License-Identifier: MIT
"""Local regression tests for tools/aquamarine_readiness_patch.py path confinement.

Fixtures are synthetic and live only in a fresh temporary directory; no upstream
checkout, host file or build/ content is touched.
"""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('aq_patch', ROOT / 'tools' / 'aquamarine_readiness_patch.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)

# Synthetic source: each exact anchor exactly once, separated by filler lines.
VALID = '// fixture start\n' + '\n// filler\n'.join(old for old, _ in patcher.EDITS) + '\n// fixture end\n'


def snapshot(path):
    info = os.stat(path, follow_symlinks=False)
    return path.read_bytes(), info.st_ino, info.st_nlink


class PatchConfinement(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix='rasklad-aq-patch-')
        self.tmp = Path(self._tmp.name)
        self.build = self.tmp / 'build'
        self.outside = self.tmp / 'outside'
        self.build.mkdir()
        self.outside.mkdir()

    def tearDown(self):
        self._tmp.cleanup()

    def make_checkout(self, root, text=VALID):
        backend = root / 'src' / 'backend'
        backend.mkdir(parents=True)
        target = backend / 'Wayland.cpp'
        target.write_text(text, encoding='utf-8')
        return target

    def assert_no_temp_left(self, directory):
        self.assertEqual([p.name for p in directory.iterdir() if p.name.startswith('.')], [])

    def test_valid_fixture_patches_by_replacing_inode(self):
        target = self.make_checkout(self.build / 'aq')
        before_ino = os.stat(target).st_ino
        digest = patcher.patch_checkout(self.build, self.build / 'aq', check=False)
        text = target.read_text(encoding='utf-8')
        self.assertIn(patcher.MARK, text)
        self.assertNotEqual(os.stat(target).st_ino, before_ino)
        self.assertEqual(len(digest), 64)
        self.assert_no_temp_left(target.parent)
        with self.assertRaisesRegex(patcher.Refused, 'already patched'):
            patcher.patch_checkout(self.build, self.build / 'aq', check=False)

    def test_check_mode_writes_nothing(self):
        target = self.make_checkout(self.build / 'aq')
        before = snapshot(target)
        patcher.patch_checkout(self.build, self.build / 'aq', check=True)
        self.assertEqual(snapshot(target), before)
        self.assert_no_temp_left(target.parent)

    def test_symlinked_ancestor_cannot_touch_outside(self):
        protected = self.make_checkout(self.outside / 'aq')
        before = snapshot(protected)
        (self.build / 'aq').mkdir()
        os.symlink(self.outside / 'aq' / 'src', self.build / 'aq' / 'src')
        for check in (True, False):
            with self.assertRaisesRegex(patcher.Refused, 'unsafe path component'):
                patcher.patch_checkout(self.build, self.build / 'aq', check=check)
        self.assertEqual(snapshot(protected), before)
        self.assert_no_temp_left(protected.parent)

    def test_symlinked_checkout_root_refused(self):
        protected = self.make_checkout(self.outside / 'aq')
        before = snapshot(protected)
        os.symlink(self.outside / 'aq', self.build / 'aq')
        with self.assertRaisesRegex(patcher.Refused, 'unsafe path component'):
            patcher.patch_checkout(self.build, self.build / 'aq', check=False)
        self.assertEqual(snapshot(protected), before)

    def test_target_hardlink_cannot_alter_protected_original(self):
        protected = self.outside / 'Wayland.cpp'
        protected.write_text(VALID, encoding='utf-8')
        backend = self.build / 'aq' / 'src' / 'backend'
        backend.mkdir(parents=True)
        os.link(protected, backend / 'Wayland.cpp')
        before = snapshot(protected)
        with self.assertRaisesRegex(patcher.Refused, 'links'):
            patcher.patch_checkout(self.build, self.build / 'aq', check=False)
        self.assertEqual(snapshot(protected), before)
        self.assertEqual((backend / 'Wayland.cpp').read_text(encoding='utf-8'), VALID)
        self.assert_no_temp_left(backend)

    def test_target_symlink_refused(self):
        protected = self.outside / 'Wayland.cpp'
        protected.write_text(VALID, encoding='utf-8')
        backend = self.build / 'aq' / 'src' / 'backend'
        backend.mkdir(parents=True)
        os.symlink(protected, backend / 'Wayland.cpp')
        before = snapshot(protected)
        with self.assertRaisesRegex(patcher.Refused, 'missing or symlink'):
            patcher.patch_checkout(self.build, self.build / 'aq', check=False)
        self.assertEqual(snapshot(protected), before)

    def test_anchor_mismatch_leaves_original_untouched(self):
        broken = VALID.replace('wl_display_roundtrip(waylandState.display);', 'wl_display_roundtrip(display);')
        target = self.make_checkout(self.build / 'aq', broken)
        before = snapshot(target)
        with self.assertRaisesRegex(patcher.Refused, 'anchor count 0'):
            patcher.patch_checkout(self.build, self.build / 'aq', check=False)
        self.assertEqual(snapshot(target), before)
        self.assert_no_temp_left(target.parent)

    def test_checkout_outside_build_refused(self):
        protected = self.make_checkout(self.outside / 'aq')
        before = snapshot(protected)
        for path in (self.outside / 'aq', self.build, self.build / '..' / 'outside' / 'aq'):
            with self.assertRaisesRegex(patcher.Refused, 'under build'):
                patcher.patch_checkout(self.build, path, check=False)
        self.assertEqual(snapshot(protected), before)


if __name__ == '__main__':
    unittest.main()
