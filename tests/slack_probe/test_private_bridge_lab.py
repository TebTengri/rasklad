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
   'RASKLAD_A11Y_MODE':'enabled','DBUS_SESSION_BUS_ADDRESS':'unix:path=/run/lab/bus,guid=abc'}
  self.assertTrue(lab.private_context(env,True));self.assertFalse(lab.private_context(env,False))
  for key,value in [('HOME','/home/user'),('DISPLAY',':0'),('RASKLAD_A11Y_MODE','other'),
    ('DBUS_SESSION_BUS_ADDRESS','unix:path=/run/user/1000/bus'),('DBUS_SESSION_BUS_ADDRESS','unix:path=/run/lab/bus;unix:path=/host'),
    ('WAYLAND_DISPLAY','wayland-1'),('WAYLAND_SOCKET','9'),('RASKLAD_PARENT_FD','8')]:
   with self.subTest(key=key):self.assertFalse(lab.private_context(dict(env,**{key:value}),True))
 def test_builder_keeps_isolation_and_drops_disabled_bridge(self):
  paths={'runner':ROOT/'tools/a11y_bridge_lab.py'};identity={}
  args=lab.sandbox_args(chrome,paths,identity,'enabled')
  self.assertIn('--unshare-all',args);self.assertIn('--clearenv',args)
  self.assertNotIn('--share-net',args);self.assertNotIn('--bind',args)
  env={args[i+1]:args[i+2] for i,x in enumerate(args) if x=='--setenv'}
  self.assertEqual(env['NO_AT_BRIDGE'],'0');self.assertEqual(env['RASKLAD_A11Y_MODE'],'enabled')
  self.assertNotIn('DBUS_SESSION_BUS_ADDRESS',env);self.assertNotIn('WAYLAND_DISPLAY',env)
  self.assertNotIn('FCITX_ADDON_DIRS',env)
  self.assertEqual(args[-1],'--inside')
  self.assertIn('/usr/bin/dbus-run-session',args)
  with self.assertRaises(ValueError):lab.sandbox_args(chrome,paths,identity,'host')
if __name__=='__main__':unittest.main()
