# SPDX-License-Identifier: MIT
import importlib.util,os,socket,tempfile,unittest
from pathlib import Path
from types import SimpleNamespace
s=importlib.util.spec_from_file_location('host_plan',Path(__file__).resolve().parents[2]/'tools/host_registry_probe.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class HostPlanGuards(unittest.TestCase):
 def test_peer_requires_correct_uid_and_exact_binary(self):
  p=Path('/usr/bin/Hyprland')
  self.assertTrue(m.peer_is_hyprland(10,1000,p,p,1000))
  self.assertFalse(m.peer_is_hyprland(0,1000,p,p,1000))
  self.assertFalse(m.peer_is_hyprland(10,1001,p,p,1000))
  self.assertFalse(m.peer_is_hyprland(10,1000,Path('/tmp/fake'),p,1000))
 def test_no_default_display_guess_or_path_escape(self):
  for value in ('','../wayland-0','/tmp/wayland-0','wayland-0/extra'):
   with self.assertRaises(m.base.Refused):m.display_path({'XDG_RUNTIME_DIR':'/tmp','WAYLAND_DISPLAY':value},os.getuid())
 def test_child_report_never_forwards_extra_text_and_rejects_invalid_versions(self):
  raw={'ok':True,'registryOnly':True,'globalsBound':0,'stableListVersion':1,'wlrManagerVersion':3,'hyprlandMappingVersion':0,'unexpected':'private-title'}
  self.assertNotIn('unexpected',m.sanitized_report(raw))
  self.assertNotIn('private-title',str(m.sanitized_report(raw)))
  for value in (True,-1,101,'1'):
   with self.assertRaises(m.base.Refused):m.sanitized_report(dict(raw,stableListVersion=value))
 def test_binary_local_rpath_is_refused_without_execution(self):
  for data,ok in ((b'(NEEDED) systemlib',True),(b'(RUNPATH) /tmp/plugin',False),(b'(RPATH) build',False)):
   def inspector(command,**kwargs):kwargs['stdout'].write(data);return SimpleNamespace(returncode=0)
   if ok:m.reject_local_search_paths(42,inspector)
   else:
    with self.assertRaisesRegex(m.base.Refused,'dynamic-search-path'):m.reject_local_search_paths(42,inspector)
 def test_runtime_symlink_is_refused(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);(root/'link').symlink_to(root)
   with self.assertRaisesRegex(m.base.Refused,'runtime-owner'):
    m.display_path({'XDG_RUNTIME_DIR':str(root/'link'),'WAYLAND_DISPLAY':'wayland-0'},os.getuid())
if __name__=='__main__':unittest.main()
