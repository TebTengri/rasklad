# SPDX-License-Identifier: MIT
from pathlib import Path
import importlib.machinery,importlib.util,sys,unittest
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
loader=importlib.machinery.SourceFileLoader('trial',str(ROOT/'bin/slack-accessibility-trial'))
spec=importlib.util.spec_from_loader(loader.name,loader);trial=importlib.util.module_from_spec(spec);loader.exec_module(trial)
class TrialPlanTests(unittest.TestCase):
 def test_plan_does_not_launch_or_edit(self):
  p=trial.plan();self.assertEqual(p['mode'],'plan');self.assertFalse(p['projectFileEdits']);self.assertFalse(p['correctorStarted'])
  self.assertEqual(p['command'],['/usr/lib/slack/slack','--force-renderer-accessibility'])
 def test_running_slack_is_never_killed_or_relaunched(self):
  calls=[];r=trial.launch(lambda:{123},lambda *a,**k:calls.append(a));self.assertFalse(r['started']);self.assertEqual(calls,[])
if __name__=='__main__':unittest.main()
