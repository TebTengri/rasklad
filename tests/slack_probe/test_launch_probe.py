# SPDX-License-Identifier: MIT
import importlib.util,unittest
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('launch',Path(__file__).resolve().parents[2]/'tools/slack_launch_probe.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Probe:
 def __init__(self):self.pids=[set(),{42}];self.observed=False
 def slack_pids(self):return self.pids.pop(0) if self.pids else {42}
 def run(self,observe=False):
  self.observed=observe;return {'schema':1,'truncated':False,'textRead':False,'writes':False}
class LaunchTests(unittest.TestCase):
 def test_refuses_existing_slack_without_launching(self):
  p=Probe();p.pids=[{42}]
  with patch.object(m.os,'geteuid',return_value=1000):
   with self.assertRaisesRegex(RuntimeError,'slack-already-running'):
    m.run(p,lambda *a:self.fail('must not validate'),lambda *a,**k:self.fail('must not launch'))
 def test_fixed_launch_scoped_environment_no_kill(self):
  p=Probe();calls=[];source={'HOME':'/home/example','ACCESSIBILITY_ENABLED':'0'}
  def launch(args,**kwargs):calls.append((args,kwargs));return object()
  with patch.object(m.os,'geteuid',return_value=1000):r=m.run(p,lambda *a:None,launch,source)
  self.assertEqual(calls[0][0],[m.EXECUTABLE,'--force-renderer-accessibility'])
  self.assertEqual(calls[0][1]['env']['ACCESSIBILITY_ENABLED'],'1')
  self.assertEqual(source['ACCESSIBILITY_ENABLED'],'0');self.assertTrue(p.observed)
  self.assertFalse(r['systemStatusWritten']);self.assertFalse(r['toolTerminatedSlack']);self.assertFalse(r['editorSupportVerified'])
  self.assertTrue(r['measurementCompleted']);self.assertNotIn('HOME',str(r))
 def test_validation_failure_prevents_launch(self):
  def reject(*args):raise ValueError('invalid')
  with patch.object(m.os,'geteuid',return_value=1000):
   with self.assertRaises(ValueError):m.run(Probe(),reject,lambda *a,**k:self.fail('must not launch'))
if __name__=='__main__':unittest.main()
