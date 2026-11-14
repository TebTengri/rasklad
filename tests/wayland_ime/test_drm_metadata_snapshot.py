# SPDX-License-Identifier: MIT
import importlib.util
import os
from pathlib import Path
import stat
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('metadata_snapshot', ROOT/'tools/drm_metadata_snapshot.py')
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)


class MetadataSnapshot(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sys = self.root / 'sys'
        self.dev = self.root / 'dev'
        self.pci = self.sys / 'devices/pci0000:00/0000:00:02.0'
        (self.pci / 'drm').mkdir(parents=True)
        (self.sys / 'class/drm').mkdir(parents=True)
        (self.sys / 'bus/pci').mkdir(parents=True)
        (self.pci / 'subsystem').symlink_to(self.sys / 'bus/pci')
        (self.pci / 'uevent').write_text('DRIVER=not-copied\nPCI_SLOT_NAME=0000:00:02.0\nOTHER=not-copied\n')
        for name in lab.ATTRIBUTES:
            (self.pci / name).write_text('0x1234\n')
        (self.dev / 'dri').mkdir(parents=True)
        for name, minor in [('card1', 1), ('renderD128', 128)]:
            node = self.pci / 'drm' / name
            node.mkdir()
            (node / 'device').symlink_to('../..')
            (node / 'dev').write_text(f'226:{minor}\n')
            (self.sys / 'class/drm' / name).symlink_to(node)
            (self.dev / 'dri' / name).touch()

    def fake_stat(self, path):
        minor = 128 if path.name == 'renderD128' else 1
        return SimpleNamespace(st_mode=stat.S_IFCHR | 0o660, st_uid=os.getuid(), st_rdev=os.makedev(226, minor))

    def collect(self):
        return lab.collect_metadata(self.sys, self.dev, stat_node=self.fake_stat)

    def test_default_never_collects(self):
        with patch('sys.argv', ['snapshot']), patch.object(lab, 'collect_metadata') as collect:
            self.assertEqual(lab.main(), 0)
            collect.assert_not_called()

    def test_collects_only_validated_values(self):
        metadata = self.collect()
        self.assertEqual(metadata['pciSlot'], '0000:00:02.0')
        self.assertEqual({n['name']: n['access'] for n in metadata['nodes']}, {'card1': 'stat_only', 'renderD128': 'render'})
        self.assertNotIn('DRIVER', str(metadata))
        self.assertNotIn(str(self.pci), str(metadata))

    def test_rejects_regular_device_and_identity_mismatch(self):
        with self.assertRaisesRegex(ValueError, 'device_identity'):
            lab.collect_metadata(self.sys, self.dev)
        with self.assertRaisesRegex(ValueError, 'device_identity'):
            lab.collect_metadata(self.sys, self.dev, stat_node=lambda _: SimpleNamespace(st_mode=stat.S_IFCHR, st_uid=os.getuid(), st_rdev=os.makedev(1, 3)))

    def test_rejects_device_outside_sys_devices(self):
        (self.pci/'drm/renderD128/device').unlink()
        (self.pci/'drm/renderD128/device').symlink_to(self.root)
        with self.assertRaisesRegex(ValueError, 'pci_device_outside_devices'):
            self.collect()

    def test_rejects_non_pci_and_oversized_attribute(self):
        (self.pci/'subsystem').unlink()
        (self.pci/'subsystem').symlink_to(self.sys/'bus')
        with self.assertRaisesRegex(ValueError, 'non_pci_subsystem'):
            self.collect()
        (self.pci/'subsystem').unlink()
        (self.pci/'subsystem').symlink_to(self.sys/'bus/pci')
        (self.pci/'vendor').write_text('x'*65)
        with self.assertRaisesRegex(ValueError, 'metadata_bound'):
            self.collect()

    def test_rejects_symlink_attribute_and_duplicate_slot(self):
        (self.pci/'vendor').unlink()
        (self.pci/'vendor').symlink_to(self.pci/'device')
        with self.assertRaises(OSError):
            self.collect()
        (self.pci/'vendor').unlink()
        (self.pci/'vendor').write_text('0x1234\n')
        (self.pci/'uevent').write_text('PCI_SLOT_NAME=0000:00:02.0\n'*2)
        with self.assertRaisesRegex(ValueError, 'pci_slot'):
            self.collect()

    def test_snapshot_private_new_under_build_without_host_links(self):
        project = self.root/'project'
        parent = project/'build/private'
        parent.mkdir(parents=True, mode=0o700)
        output = parent/'snapshot'
        plan = lab.write_snapshot(self.collect(), output, project_root=project)
        self.assertEqual(output.stat().st_mode & 0o777, 0o700)
        self.assertTrue(plan['primaryBindMustBeNodevReadOnly'])
        self.assertFalse(plan['hostSysfsBind'])
        files = [p for p in output.rglob('*') if p.is_file() and not p.is_symlink()]
        self.assertEqual(len(files), 7)
        self.assertTrue(all('not-copied' not in p.read_text() for p in files))
        for path in output.rglob('*'):
            if path.is_symlink():
                self.assertNotIn(str(self.root), str(path.readlink()))
        for identity in ('226:1', '226:128'):
            device = output/'sys/dev/char'/identity/'device'
            self.assertTrue((device/'drm').is_dir())
            self.assertEqual(device.resolve(), output/'sys/devices/rasklad-pci')
            self.assertEqual((device/'vendor').read_text(), '0x1234\n')
            self.assertEqual((device/'subsystem').readlink(), Path('/sys/bus/pci'))
        with self.assertRaises(FileExistsError):
            lab.write_snapshot(self.collect(), output, project_root=project)

    def test_rejects_external_or_public_output_and_foreign_uid(self):
        metadata = self.collect()
        with self.assertRaisesRegex(ValueError, 'project_build_only'):
            lab.write_snapshot(metadata, self.root/'external', project_root=self.root/'project')
        project = self.root/'project'
        (project/'build').mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, 'existing_private_parent_required'):
            lab.write_snapshot(metadata, project/'build/snapshot', project_root=project)
        metadata['collectorUid'] = os.getuid()+1
        with self.assertRaisesRegex(ValueError, 'same_uid_required'):
            lab.validate_metadata(metadata)

    def test_dotdot_cannot_escape_build_to_private_sibling(self):
        project = self.root/'project'
        (project/'build').mkdir(parents=True)
        (project/'private').mkdir(mode=0o700)
        with self.assertRaisesRegex(ValueError, 'project_build_only'):
            lab.write_snapshot(self.collect(), project/'build/../private/output', project_root=project)
        self.assertFalse((project/'private/output').exists())


if __name__ == '__main__':
    unittest.main()
