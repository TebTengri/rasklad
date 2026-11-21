"""SYS-226: the packaged profile list equals the pinned lab profile list."""
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class ProfileFiles(unittest.TestCase):
    def test_list_matches_lab_profile(self):
        spec = importlib.util.spec_from_file_location('chrome_lab', ROOT / 'tools/chromium_ime_lab.py')
        lab = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(lab)
        lines = (ROOT / 'packaging/arch/profile-files.txt').read_text().splitlines()
        listed = [x for x in lines if x and not x.startswith('#')]
        self.assertEqual(listed, lab.PROFILE_PATHS)
        for rel in listed:
            self.assertTrue(rel.startswith(('data/', 'build/')))
            self.assertNotIn('..', rel.split('/'))

    def test_pkgbuild_has_both_sources(self):
        text = (ROOT / 'packaging/arch/PKGBUILD').read_text()
        self.assertIn("'@SHA256@' '@PROFILE_SHA256@'", text)
        self.assertIn('hunspell', text)


if __name__ == '__main__':
    unittest.main()
