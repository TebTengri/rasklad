# SPDX-License-Identifier: MIT
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('fcitx_lab_runner',ROOT/'tools/fcitx_lab_runner.py')
lab=importlib.util.module_from_spec(spec);spec.loader.exec_module(lab)


class RunnerGuards(unittest.TestCase):
    def test_fixed_plan_seals_host_surfaces(self):
        argv=lab.sandbox_argv(lab.fixed_files())
        self.assertIn('--unshare-all',argv)
        self.assertIn('--clearenv',argv)
        self.assertIn('--die-with-parent',argv)
        self.assertIn('--new-session',argv)
        self.assertNotIn('--share-net',argv)
        self.assertNotIn('--dev-bind',argv)
        self.assertNotIn('--ro-bind', argv[argv.index('--',argv.index('--chdir'))+1:])
        for forbidden in ('DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET','DBUS_SESSION_BUS_ADDRESS','XAUTHORITY'):
            self.assertNotIn(forbidden,argv)
        self.assertEqual(argv[-6:],['/lab/session-bus.conf','--','/usr/bin/python3','-I','/lab/runner.py','--inside'])

    def test_mounts_only_fixed_project_files(self):
        argv=lab.sandbox_argv(lab.fixed_files())
        pairs=[(argv[i+1],argv[i+2]) for i,a in enumerate(argv) if a=='--ro-bind']
        self.assertEqual({target for source,target in pairs}, {'/usr','/lab/runner.py','/lab/rasklad-ime-lab','/lab/addons/raskladlab.so','/lab/addon.conf','/lab/session-bus.conf'})
        self.assertNotIn((str(ROOT),'/lab'),pairs)
        self.assertFalse(any('/home/mvn' in source for source,target in pairs))

    def test_identity_files_are_synthetic_not_host_mounts(self):
        files={name:Path('/generated')/name for name in ('passwd','group','machine-id')}
        argv=lab.sandbox_argv(lab.fixed_files(),files)
        pairs=[(argv[i+1],argv[i+2]) for i,a in enumerate(argv) if a=='--ro-bind']
        for name in files:
            self.assertIn(('/generated/'+name,'/etc/'+name),pairs)
            self.assertNotIn(('/etc/'+name,'/etc/'+name),pairs)

    def test_root_run_refused_before_launch(self):
        with patch.object(lab.os,'geteuid',return_value=0),patch.object(lab.subprocess,'Popen') as popen:
            with self.assertRaises(lab.Refused):lab.run()
            popen.assert_not_called()

    def test_credential_and_network_addons_not_enabled(self):
        self.assertEqual(set(lab.ALLOW_ADDONS.split(',')),{'keyboard','dbus','dbusfrontend','raskladlab'})
        bus=(ROOT/'tests/fcitx_transport/session-bus.conf').read_text()
        self.assertIn('<listen>unix:tmpdir=/run/lab</listen>',bus)
        self.assertNotIn('<servicedir>',bus)
        self.assertNotIn('tcp:',bus)

    def test_symlink_artifact_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);(base/'real').write_text('fixture');(base/'link').symlink_to(base/'real')
            with self.assertRaises(lab.Refused):lab.regular_file(base/'link')

    def test_world_writable_artifact_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'artifact';p.write_text('fixture');p.chmod(0o666)
            with self.assertRaises(lab.Refused):lab.regular_file(p)

    def test_symlink_build_directory_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'elsewhere').mkdir();(root/'build').symlink_to(root/'elsewhere')
            with self.assertRaises(lab.Refused):lab.ensure_private_base(root)

    def test_inside_refuses_real_session(self):
        with patch.dict(os.environ,{'HOME':'/home/mvn','DISPLAY':':0'},clear=True),patch.object(lab.subprocess,'Popen') as popen:
            with self.assertRaises(lab.Refused):lab.inside()
            popen.assert_not_called()

    def test_default_is_inert(self):
        with patch('sys.argv',['fcitx_lab_runner']),patch.object(lab.subprocess,'Popen') as popen,patch.object(lab,'build') as build:
            self.assertEqual(lab.main(),0)
            popen.assert_not_called();build.assert_not_called()


if __name__=='__main__':unittest.main()
