# SPDX-License-Identifier: MIT
import importlib.util,os,signal,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('status_trial',Path(__file__).resolve().parents[2]/'tools/slack_status_trial.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Fake:
 def __init__(self):self.value=False;self.name=':1.2';self.writes=[];self.fail=False
 def owner(self):return self.name
 def uid(self,owner):return os.getuid()
 def get(self,owner):return self.value
 def set(self,owner,value):
  self.value=value;self.writes.append(value)
  if value and self.fail:raise TimeoutError()
class StatusTrialTests(unittest.TestCase):
 def test_restores_after_success_and_probe_failure(self):
  for fail in (False,True):
   c=Fake()
   def probe():
    if fail:raise RuntimeError('private content')
    return {'schema':1,'textRead':False,'writes':False,'truncated':False}
   r=m.trial(c,probe)
   self.assertEqual(c.writes,[True,False]);self.assertTrue(r['restored'])
   self.assertEqual(r['passed'],not fail);self.assertNotIn('private content',str(r))
 def test_uncertain_enable_reply_is_restored(self):
  c=Fake();c.fail=True;r=m.trial(c,lambda:{})
  self.assertTrue(r['restored']);self.assertFalse(r['passed'])
 def test_does_not_mutate_replacement_owner(self):
  c=Fake()
  def probe():c.name=':1.3';return {}
  r=m.trial(c,probe)
  self.assertEqual(c.writes,[True]);self.assertFalse(r['restored']);self.assertEqual(r['restoreError'],'owner-changed-before-restore')
 def test_retry_restore_and_reject_failed_measurement(self):
  c=Fake();original=c.set;attempts=[]
  def flaky(owner,value):
   if not value:
    attempts.append(value)
    if len(attempts)==1:raise TimeoutError()
   original(owner,value)
  c.set=flaky
  r=m.trial(c,lambda:{'passed':False})
  self.assertTrue(r['restored']);self.assertEqual(r['restoreAttempts'],2)
  self.assertFalse(r['passed']);self.assertFalse(r['measurementCompleted'])
 def test_readback_mismatch_stays_failure(self):
  c=Fake();original=c.set
  def ignore_restore(owner,value):
   if value:original(owner,value)
  c.set=ignore_restore
  r=m.trial(c,lambda:{})
  self.assertFalse(r['restored']);self.assertEqual(r['restoreAttempts'],3)
  self.assertEqual(r['restoreError'],'restore-readback-mismatch')
 def test_handled_signals_cannot_interrupt_restore(self):
  c=Fake();original=c.set
  def restore_signal(owner,value):
   if not value:
    os.kill(os.getpid(),signal.SIGTERM);os.kill(os.getpid(),signal.SIGINT)
   original(owner,value)
  c.set=restore_signal
  old=signal.signal(signal.SIGTERM,lambda *args:(_ for _ in ()).throw(RuntimeError('interrupted')))
  try:
   r=m.trial(c,lambda:{})
   self.assertTrue(r['restored'])
  finally:signal.signal(signal.SIGTERM,old)
 def test_refuses_existing_enabled_status_and_wrong_uid(self):
  c=Fake();c.value=True
  with self.assertRaises(m.Refused):m.trial(c,lambda:{})
  self.assertEqual(c.writes,[])
  c=Fake();c.uid=lambda owner:os.getuid()+1
  with self.assertRaises(m.Refused):m.trial(c,lambda:{})
  self.assertEqual(c.writes,[])
if __name__=='__main__':unittest.main()
