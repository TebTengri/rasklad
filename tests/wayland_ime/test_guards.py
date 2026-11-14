# SPDX-License-Identifier: MIT
import hashlib,importlib.util,json,os,subprocess,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('wayland_lab',ROOT/'tools/wayland_ime_lab.py');lab=importlib.util.module_from_spec(s);s.loader.exec_module(lab)
class WaylandIsolation(unittest.TestCase):
 def test_default_is_inert(self):
  with patch('sys.argv',['lab']),patch.object(lab,'run') as run,patch.object(lab.subprocess,'Popen') as popen:
   self.assertEqual(lab.main(),0);run.assert_not_called();popen.assert_not_called()
 def test_private_backend_without_host_display_or_devices(self):
  paths=lab.chrome.files();paths.pop('xvfb');paths.pop('keys')
  a=lab.sandbox_args(paths,{},None,Path('/runtime'),Path('/config'))
  for absent in ['DISPLAY','GTK_IM_MODULE','WAYLAND_SOCKET','--share-net','--dev-bind','--preserve-fds']:self.assertNotIn(absent,a)
  self.assertIn('--unshare-all',a);self.assertIn('--clearenv',a)
  self.assertEqual(a[a.index('WLR_BACKENDS')+1],'headless')
  self.assertEqual(a[a.index('WLR_RENDERER')+1],'pixman')
  self.assertEqual(a[a.index('LD_LIBRARY_PATH')+1],'/lab/wayland-runtime')
  self.assertFalse(any('/run/user/' in arg for arg in a))
 def test_requires_explicit_profile_root(self):
  with patch.object(lab.os,'geteuid',return_value=1000),patch.object(lab.subprocess,'Popen') as popen:
   with self.assertRaisesRegex(lab.base.Refused,'explicit_input_root_required'):lab.run(None)
   popen.assert_not_called()
 def test_focus_suite_is_opt_in_in_reference(self):
  paths=lab.chrome.files();paths.pop('xvfb');paths.pop('keys')
  default=lab.sandbox_args(paths,{},None,Path('/runtime'),Path('/config'))
  selected=lab.sandbox_args(paths,{},None,Path('/runtime'),Path('/config'),focus_stress=True)
  self.assertNotIn('RASKLAD_FOCUS_STRESS_LAB',default)
  self.assertIn('RASKLAD_FOCUS_STRESS_LAB',selected)
  self.assertNotIn('--dev-bind',selected)
 def test_space_telemetry_is_optional_and_requires_focus(self):
  paths=lab.chrome.files();paths.pop('xvfb');paths.pop('keys')
  default=lab.sandbox_args(paths,{},None,Path('/runtime'),Path('/config'),focus_stress=True)
  selected=lab.sandbox_args(paths,{},None,Path('/runtime'),Path('/config'),focus_stress=True,space_telemetry=True)
  self.assertNotIn('RASKLAD_SPACE_TELEMETRY_LAB',default)
  self.assertIn('RASKLAD_SPACE_TELEMETRY_LAB',selected)
  self.assertNotIn('--dev-bind',selected)
  with self.assertRaisesRegex(lab.base.Refused,'space_telemetry_requires_focus_suite'):
   lab.sandbox_args(paths,{},None,Path('/runtime'),Path('/config'),space_telemetry=True)
  with patch.object(lab.subprocess,'Popen') as popen:
   with self.assertRaisesRegex(lab.base.Refused,'space_telemetry_requires_focus_suite'):lab.run('/unused',space_telemetry=True)
   popen.assert_not_called()
 def test_inner_refuses_host(self):
  with patch.object(lab,'INSIDE',False),patch.object(lab.subprocess,'Popen') as popen:
   with self.assertRaisesRegex(lab.base.Refused,'not_private_lab'):lab.inside()
   popen.assert_not_called()
 def test_protocol_pin_and_notice(self):
  p=ROOT/'tests/wayland_ime/protocol';source=json.loads((p/'source.json').read_text());b=(p/'virtual-keyboard-unstable-v1.xml').read_bytes()
  self.assertEqual(hashlib.sha256(b).hexdigest(),source['sha256'])
  self.assertIn(b'The above copyright notice',b);self.assertNotIn(b'<!DOCTYPE',b)
 def test_sealed_binary_cannot_change_after_validation(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp)/'binary';p.write_bytes(b'original-artifact')
   fd=lab.base.sealed_snapshot(p,hashlib.sha256(b'original-artifact').hexdigest())
   try:
    p.write_bytes(b'replaced-after-check')
    self.assertEqual(os.read(fd,100),b'original-artifact')
    with self.assertRaises(OSError):os.write(fd,b'x')
   finally:os.close(fd)
 def test_wrong_binary_hash_is_rejected(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp)/'binary';p.write_bytes(b'unreviewed')
   with self.assertRaisesRegex(lab.base.Refused,'snapshot_pin'):lab.base.sealed_snapshot(p,'0'*64)
 def test_driver_refuses_without_private_markers(self):
  binary=ROOT/'build/wayland-ime/keys/rasklad-wayland-keys'
  if not binary.exists():self.skipTest('build private fixture first')
  r=subprocess.run([str(binary)],input=b'word\n',capture_output=True,env={'PATH':'/usr/bin'},timeout=2)
  self.assertEqual(r.returncode,2);self.assertEqual(json.loads(r.stdout)['error'],'lab-guard')
if __name__=='__main__':unittest.main()
