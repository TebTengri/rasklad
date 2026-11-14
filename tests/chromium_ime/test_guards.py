# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('chromium_ime_lab',ROOT/'tools/chromium_ime_lab.py')
lab=importlib.util.module_from_spec(spec);spec.loader.exec_module(lab)

class IsolationGuards(unittest.TestCase):
 def test_default_does_not_launch_or_verify_runtime(self):
  with patch('sys.argv',['lab']),patch.object(lab,'run') as run,patch.object(lab.subprocess,'Popen') as popen:
   self.assertEqual(lab.main(),0);run.assert_not_called();popen.assert_not_called()
 def test_private_display_and_bus_no_host_environment(self):
  a=lab.argv(lab.files(),{k:Path('/temporary')/k for k in ['passwd','group','machine-id']})
  self.assertIn('--unshare-all',a);self.assertIn('--clearenv',a)
  self.assertNotIn('--share-net',a);self.assertNotIn('--dev-bind',a)
  self.assertNotIn('WAYLAND_DISPLAY',a);self.assertNotIn('XAUTHORITY',a)
  self.assertNotIn('DBUS_SESSION_BUS_ADDRESS',a)
  i=a.index('DISPLAY');self.assertEqual(a[i+1],':77')
  ro=[(a[i+1],a[i+2]) for i,x in enumerate(a) if x=='--ro-bind']
  self.assertFalse(any(s==str(ROOT) for s,t in ro))
  self.assertFalse(any(s.startswith('/run/user/') for s,t in ro))
  self.assertEqual({t for s,t in ro if t.startswith('/etc/')},{'/etc/fonts','/etc/passwd','/etc/group','/etc/machine-id'})
 def test_unknown_binary_is_rejected_before_spawn(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);(root/'data').mkdir();(root/'build/chromium-ime').mkdir(parents=True)
   (root/'build/chromium-ime/Xvfb').write_bytes(b'not reviewed')
   (root/'data/xvfb-lab-lock.json').write_text('{"signatureVerified":true,"binarySha256":"0000"}')
   with patch.object(lab,'ROOT',root),patch.object(lab.base,'regular_file'),patch.object(lab.os,'geteuid',return_value=1000),patch.object(lab.os,'stat',return_value=SimpleNamespace(st_uid=0,st_mode=0o755)),patch.object(lab.subprocess,'Popen') as popen:
    with self.assertRaisesRegex(lab.base.Refused,'xvfb_pin'):lab.run()
    popen.assert_not_called()
 def test_root_is_rejected_before_runtime_access(self):
  with patch.object(lab.os,'geteuid',return_value=0),patch.object(lab,'files') as files:
   with self.assertRaisesRegex(lab.base.Refused,'root_not_admitted'):lab.run()
   files.assert_not_called()
 def test_inner_mode_cannot_run_on_host(self):
  with patch.object(lab,'INSIDE',False),patch.object(lab.subprocess,'Popen') as popen:
   with self.assertRaisesRegex(lab.base.Refused,'not_private_lab'):lab.inside()
   popen.assert_not_called()
 def test_key_helper_refuses_without_private_marker(self):
  result=subprocess.run(['/usr/bin/python3','-I',str(ROOT/'tests/chromium_ime/keys.py'),'word','123'],env={'PATH':'/usr/bin'},capture_output=True,timeout=3)
  self.assertNotEqual(result.returncode,0);self.assertIn(b'not private lab',result.stderr)

if __name__=='__main__':unittest.main()
