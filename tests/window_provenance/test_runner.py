# SPDX-License-Identifier: MIT
import importlib.util,unittest
from pathlib import Path
from unittest.mock import patch
s=importlib.util.spec_from_file_location('native_runner',Path(__file__).with_name('run.py'))
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class NativeBoundaryTests(unittest.TestCase):
 def test_display_is_private_with_no_host_environment(self):
  args=m.sandbox_args({'probe':10,'sway':11},[])
  self.assertIn('--unshare-all',args);self.assertIn('--clearenv',args)
  for forbidden in ('--share-net','--bind','--dev-bind'):self.assertNotIn(forbidden,args)
  env={args[i+1]:args[i+2] for i,v in enumerate(args) if v=='--setenv'}
  self.assertEqual(env['WLR_BACKENDS'],'headless');self.assertEqual(env['WLR_RENDERER'],'pixman')
  self.assertEqual(env['XDG_RUNTIME_DIR'],'/run/lab')
  for name in ('DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET','DBUS_SESSION_BUS_ADDRESS'):self.assertNotIn(name,env)
  self.assertEqual(args[-1],'--inside')
 def test_inside_guard_does_not_spawn_on_host(self):
  with patch.object(m.subprocess,'Popen',side_effect=AssertionError('must not spawn')):
   with self.assertRaisesRegex(m.base.Refused,'not-private-lab'):m.inside()
 def test_file_limit_permits_pinned_library_materialization(self):
  with patch.object(m.resource,'setrlimit') as setlimit:m.native_limits()
  calls={c.args[0]:c.args[1] for c in setlimit.call_args_list}
  self.assertEqual(calls[m.resource.RLIMIT_FSIZE],(2097152,2097152))
  self.assertEqual(calls[m.resource.RLIMIT_CORE],(0,0))
  self.assertEqual(calls[m.resource.RLIMIT_CPU],(6,6))
if __name__=='__main__':unittest.main()
