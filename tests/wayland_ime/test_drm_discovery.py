# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('discovery', ROOT/'tools/drm_discovery_lab.py')
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)


class SyntheticDiscovery(unittest.TestCase):
    def test_default_is_inert(self):
        with patch('sys.argv', ['discovery']), patch.object(lab, 'run') as run:
            self.assertEqual(lab.main(), 0)
            run.assert_not_called()

    def test_no_real_gpu_or_host_sysfs_sources(self):
        args = lab.sandbox_args(Path('/fixture'))
        self.assertNotIn('--dev-bind', args)
        self.assertIn('--unshare-all', args)
        binds = [args[i+1:i+3] for i, value in enumerate(args) if value == '--ro-bind']
        self.assertIn(['/dev/null', '/dev/dri/renderD128'], binds)
        self.assertIn(['/fixture/sys', '/sys'], binds)
        self.assertFalse(any(source.startswith(('/sys', '/dev/dri', '/run/user')) for source, _ in binds))

    def test_fixture_has_only_parser_metadata(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            lab.create_sysfs(root)
            files = {p.name for p in root.rglob('*') if p.is_file()}
            self.assertEqual(files, {'uevent', 'vendor', 'device', 'subsystem_vendor', 'subsystem_device'})
            self.assertEqual((root/'dev/char/1:3/device/subsystem').readlink(), Path('/sys/bus/pci'))
            self.assertFalse((root/'dev/char/1:3/device/config').exists())

    def test_inner_refuses_host(self):
        with patch.object(lab, 'INSIDE', False):
            with self.assertRaisesRegex(ValueError, 'private_synthetic_lab_required'):
                lab.inside()


if __name__ == '__main__':
    unittest.main()
