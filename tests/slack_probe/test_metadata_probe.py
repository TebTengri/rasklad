# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import unittest
p=Path(__file__).resolve().parents[2]/'tools/slack_accessibility_probe.py'
spec=importlib.util.spec_from_file_location('slack_metadata',p);probe=importlib.util.module_from_spec(spec);spec.loader.exec_module(probe)
class MetadataGuardTests(unittest.TestCase):
 def setUp(self):
  self.calls=[];self.reader=probe.Reader(lambda *args:self.calls.append(args));self.reader.owned={':1.10'}
 def test_node_budget_refuses_unbounded_or_noninteger_before_transport(self):
  for budget in (0,129,100000,True,'512'):
   with self.assertRaises(probe.ProbeError):probe.run(node_budget=budget)
 def test_only_four_metadata_methods_can_reach_slack(self):
  for method in probe.NODE_METHODS:self.reader.node(':1.10',probe.ROOT_PATH,method)
  self.assertEqual({c[3] for c in self.calls},probe.NODE_METHODS)
 def test_text_names_attributes_edits_and_focus_actions_are_forbidden(self):
  for method in ['GetText','GetName','GetRoleName','GetAttributes','GetAll','SetTextContents','InsertText','DeleteText','DoAction','GrabFocus','SetCaretOffset']:
   with self.subTest(method=method):
    with self.assertRaises(probe.ProbeError):self.reader.node(':1.10',probe.ROOT_PATH,method)
  self.assertEqual(self.calls,[])
 def test_other_app_and_arbitrary_paths_are_never_queried(self):
  for bus,path in [(':1.11',probe.ROOT_PATH),(':1.10','/org/freedesktop/DBus'),(':1.10',probe.ROOT_PATH+'/../Text')]:
   with self.assertRaises(probe.ProbeError):self.reader.node(bus,path,'GetState')
  self.assertEqual(self.calls,[])
 def test_broker_cannot_activate_enable_change_or_inject(self):
  for method in ['StartServiceByName','Set','RegisterEvent','SendMessage','GetText']:
   with self.assertRaises(probe.ProbeError):self.reader.broker(method)
  self.assertEqual(self.calls,[])
 def test_status_is_restricted_to_two_system_booleans(self):
  for name in ('IsEnabled','ScreenReaderEnabled'):
   self.reader.status(name)
  self.assertEqual(self.calls,[('org.a11y.Bus','/org/a11y/bus','org.freedesktop.DBus.Properties','Get',('org.a11y.Status',name)) for name in ('IsEnabled','ScreenReaderEnabled')])
 def test_status_cannot_read_app_properties_or_mutate(self):
  for name in ('Name','Text','Attributes','GetAll','Set','org.a11y.Status','IsEnabled;Set'):
   with self.assertRaises(probe.ProbeError):self.reader.status(name)
  with self.assertRaises(probe.ProbeError):self.reader.node(':1.10',probe.ROOT_PATH,'Get')
  with self.assertRaises(probe.ProbeError):self.reader.broker('Get')
  self.assertEqual(self.calls,[])
if __name__=='__main__':unittest.main()
