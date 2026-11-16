# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
lab=load('bridge_control',ROOT/'tools/a11y_bridge_lab.py')
chrome=load('bridge_chrome',ROOT/'tools/chromium_ime_lab.py')
class BridgeControlGuards(unittest.TestCase):
 def test_only_explicit_private_context_may_mutate_status(self):
  env={'HOME':'/home/lab','XDG_RUNTIME_DIR':'/run/lab','RASKLAD_A11Y_CONTROL':'1','DISPLAY':':77',
   'RASKLAD_A11Y_MODE':'enabled','RASKLAD_A11Y_TARGET':'chromium','DBUS_SESSION_BUS_ADDRESS':'unix:path=/run/lab/bus,guid=abc'}
  self.assertTrue(lab.private_context(env,True));self.assertFalse(lab.private_context(env,False))
  for key,value in [('HOME','/home/user'),('DISPLAY',':0'),('RASKLAD_A11Y_MODE','other'),('RASKLAD_A11Y_TARGET','/usr/bin/sh'),
    ('DBUS_SESSION_BUS_ADDRESS','unix:path=/run/user/1000/bus'),('DBUS_SESSION_BUS_ADDRESS','unix:path=/run/lab/bus;unix:path=/host'),
    ('WAYLAND_DISPLAY','wayland-1'),('WAYLAND_SOCKET','9'),('RASKLAD_PARENT_FD','8')]:
   with self.subTest(key=key):self.assertFalse(lab.private_context(dict(env,**{key:value}),True))
 def test_wayland_is_offline_and_owns_display(self):
  env={'HOME':'/home/lab','XDG_RUNTIME_DIR':'/run/lab','RASKLAD_A11Y_CONTROL':'1',
   'RASKLAD_A11Y_MODE':'enabled','RASKLAD_A11Y_TARGET':'slack','DBUS_SESSION_BUS_ADDRESS':'unix:path=/run/lab/bus',
   'RASKLAD_A11Y_BACKEND':'wayland','WAYLAND_DISPLAY':'wayland-lab'}
  self.assertTrue(lab.private_context(env,True))
  for key,value in [('WAYLAND_DISPLAY','wayland-1'),('DISPLAY',':0'),('RASKLAD_A11Y_BRIDGE','online')]:
   self.assertFalse(lab.private_context(dict(env,**{key:value}),True))
  paths={'runner':ROOT/'tools/a11y_bridge_lab.py'}
  with self.assertRaises(ValueError):lab.sandbox_args(chrome,paths,{},'enabled','slack','online',backend='wayland')
  with self.assertRaises(ValueError):lab.sandbox_args(chrome,paths,{},'enabled',backend='wayland')
  args=lab.sandbox_args(chrome,paths,{},'enabled',backend='wayland',snapshots={'sway':12})
  self.assertIn('--unshare-all',args);self.assertNotIn('--share-net',args)
  values={args[i+1]:args[i+2] for i,x in enumerate(args) if x=='--setenv'}
  self.assertNotIn('DISPLAY',values);self.assertNotIn('WAYLAND_DISPLAY',values)
  self.assertEqual(values['WLR_RENDERER'],'pixman');self.assertEqual(values['WLR_BACKENDS'],'headless')
 def test_extended_startup_cannot_reuse_network_bridge(self):
  with self.assertRaisesRegex(ValueError,'startup-offline-only'):
   lab.run('slack','online',False,'x11',True)
  flags=lab.stderr_categories([b'ENOENT EACC',b'ES ERR_CONNECTION_REFUSED ProcessSingleton'])
  for key in ('missingResource','permissionDenied','connectionFailure','singletonFailure'):
   self.assertTrue(flags[key])
 def test_diagnostics_return_only_fixed_categories(self):
  flags=lab.stderr_categories([b'private-token example org.freedesktop.sec',b'rets failed to connect to the bus'])
  self.assertTrue(flags['secretService']);self.assertTrue(flags['busUnavailable'])
  self.assertNotIn('private-token',str(flags));self.assertEqual(set(flags),set(lab.ERROR_MARKERS))
  self.assertEqual(lab.process_kind([b'--password=private-token',b'--type=renderer']),'renderer')
  self.assertEqual(lab.process_kind([b'renamed-child']),'unknown')
  self.assertEqual(lab.process_kind([b'renamed-root'],True),'main')
  self.assertEqual(lab.process_kind([b'--type=utility',b'--utility-sub-type=network.mojom.NetworkService']),'network')
 def test_window_metadata_cannot_touch_host_display(self):
  with self.assertRaisesRegex(RuntimeError,'not-private-control'):lab.visible_windows()
 def test_builder_keeps_isolation_and_drops_disabled_bridge(self):
  paths={'runner':ROOT/'tools/a11y_bridge_lab.py'};identity={}
  args=lab.sandbox_args(chrome,paths,identity,'enabled')
  self.assertIn('--unshare-all',args);self.assertIn('--clearenv',args)
  self.assertNotIn('--share-net',args);self.assertNotIn('--bind',args)
  env={args[i+1]:args[i+2] for i,x in enumerate(args) if x=='--setenv'}
  self.assertEqual(env['RASKLAD_A11Y_TARGET'],'chromium')
  self.assertEqual(env['NO_AT_BRIDGE'],'0');self.assertEqual(env['RASKLAD_A11Y_MODE'],'enabled')
  self.assertNotIn('DBUS_SESSION_BUS_ADDRESS',env);self.assertNotIn('WAYLAND_DISPLAY',env)
  self.assertNotIn('FCITX_ADDON_DIRS',env)
  self.assertEqual(args[-1],'--inside')
  self.assertIn('/usr/bin/dbus-run-session',args)
  with self.assertRaises(ValueError):lab.sandbox_args(chrome,paths,identity,'host')
  with self.assertRaises(ValueError):lab.sandbox_args(chrome,paths,identity,'enabled','/usr/bin/sh')
  selected=lab.sandbox_args(chrome,paths,identity,'enabled','slack')
  selected_env={selected[i+1]:selected[i+2] for i,x in enumerate(selected) if x=='--setenv'}
  self.assertEqual(selected_env['RASKLAD_A11Y_TARGET'],'slack')
  self.assertNotIn('DBUS_SESSION_BUS_ADDRESS',selected)
 def test_bridge_never_shares_host_network(self):
  paths={'runner':ROOT/'tools/a11y_bridge_lab.py'};identity={'passwd':Path('/private/passwd')}
  for mode in ('check','online'):
   args=lab.sandbox_args(chrome,paths,identity,'enabled','slack',mode)
   self.assertNotIn('--share-net',args);self.assertIn('--unshare-all',args)
   self.assertIn('/lab/https.sock',args);self.assertIn('rasklad-a11y-lab',args)
  with self.assertRaises(ValueError):lab.sandbox_args(chrome,paths,identity,'enabled','chromium','online')
  with self.assertRaises(ValueError):lab.sandbox_args(chrome,paths,identity,'enabled','slack',None,True)
  default=lab.sandbox_args(chrome,paths,identity,'enabled','slack','online')
  self.assertNotIn('RASKLAD_ECH_OUTER_ALLOWED',default)
  explicit=lab.sandbox_args(chrome,paths,identity,'enabled','slack','online',True)
  self.assertIn('RASKLAD_ECH_OUTER_ALLOWED',explicit);self.assertNotIn('--share-net',explicit)

if __name__=='__main__':unittest.main()
