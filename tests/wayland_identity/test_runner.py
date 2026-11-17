# SPDX-License-Identifier: MIT
import importlib.util,tempfile,unittest
from types import SimpleNamespace
from pathlib import Path
spec=importlib.util.spec_from_file_location('identity_runner',Path(__file__).with_name('run.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class BoundaryTests(unittest.TestCase):
 def test_no_host_mount_display_or_network_share(self):
  args=m.sandbox_args(123)
  self.assertEqual(args[0],'/usr/bin/bwrap');self.assertIn('--unshare-all',args)
  self.assertIn('--clearenv',args);self.assertNotIn('--share-net',args)
  self.assertNotIn('--bind',args);self.assertNotIn('--dev-bind',args)
  self.assertEqual(args[-1],'/lab/probe')
  env={args[i+1]:args[i+2] for i,value in enumerate(args) if value=='--setenv'}
  self.assertEqual(env,{'PATH':'/usr/bin'})
  for path in ('/home','/run','/tmp'):self.assertIn(['--tmpfs',path],[args[i:i+2] for i in range(len(args)-1)])
 def test_compile_log_does_not_follow_existing_symlink(self):
  with tempfile.TemporaryDirectory() as directory:
   root=Path(directory);target=root/'outside';target.write_bytes(b'keep')
   build=root/'build';build.mkdir();(build/'compile.log').symlink_to(target)
   def compiler(command,**kwargs):kwargs['stdout'].write(b'compiler output');return SimpleNamespace(returncode=0)
   self.assertEqual(m.compile_logged([],build,compiler).returncode,0)
   self.assertEqual(target.read_bytes(),b'keep')
   self.assertFalse((build/'compile.log').is_symlink())
   self.assertEqual((build/'compile.log').read_bytes(),b'compiler output')
 def test_stale_or_forged_provenance_refused(self):
  record={'schema':1,'built':True,'sourceSha256':'a'*64,'binarySha256':'b'*64}
  m.validate_record(record,'a'*64)
  with self.assertRaises(m.base.Refused):m.validate_record(record,'c'*64)
  for bad in ('',None,'g'*64,'a'*63):
   with self.assertRaises(m.base.Refused):m.validate_record(dict(record,binarySha256=bad),'a'*64)
if __name__=='__main__':unittest.main()
