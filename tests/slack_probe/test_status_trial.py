# SPDX-License-Identifier: MIT
import importlib.util,os,signal,threading,unittest
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
 def test_probe_signals_do_not_bypass_restore_and_handlers_return(self):
  c=Fake();seen=[]
  def handler(*args):seen.append(1)
  previous={sig:signal.signal(sig,handler) for sig in (signal.SIGINT,signal.SIGTERM)}
  try:
   def probe():
    os.kill(os.getpid(),signal.SIGTERM);os.kill(os.getpid(),signal.SIGINT)
    return {'schema':1,'textRead':False,'writes':False,'truncated':False}
   r=m.trial(c,probe)
   self.assertTrue(r['passed']);self.assertIs(c.value,False);self.assertEqual(seen,[])
   for sig in previous:self.assertIs(signal.getsignal(sig),handler)
  finally:
   for sig,old in previous.items():signal.signal(sig,old)
 def test_refuses_existing_enabled_status_and_wrong_uid(self):
  c=Fake();c.value=True
  with self.assertRaises(m.Refused):m.trial(c,lambda:{})
  self.assertEqual(c.writes,[])
  c=Fake();c.uid=lambda owner:os.getuid()+1
  with self.assertRaises(m.Refused):m.trial(c,lambda:{})
  self.assertEqual(c.writes,[])
 def test_missing_slack_refuses_before_any_control_call(self):
  class NoControl:
   def owner(self):raise AssertionError('bus must not be accessed')
  with self.assertRaisesRegex(m.Refused,'slack-not-running'):
   m.trial(NoControl(),lambda:None,lambda:False)
 def test_slack_exit_before_mutation_does_not_write(self):
  ready=iter([True,False]);c=Fake()
  r=m.trial(c,lambda:None,lambda:next(ready))
  self.assertEqual(c.writes,[]);self.assertFalse(r['passed'])
  self.assertEqual(r['error'],'slack-not-running')
 def test_completed_measurement_does_not_mean_app_ready(self):
  c=Fake();r=m.trial(c,lambda:{'schema':1,'textRead':False,'writes':False,'truncated':False,'targetProcesses':0})
  self.assertTrue(r['measurementCompleted']);self.assertFalse(r['targetReady'])
 def test_refuses_worker_thread_before_any_control_call(self):
  calls=[]
  class Recording(Fake):
   def owner(self):calls.append('owner');return super().owner()
   def uid(self,owner):calls.append('uid');return super().uid(owner)
   def get(self,owner):calls.append('get');return super().get(owner)
   def set(self,owner,value):calls.append('set');super().set(owner,value)
  c=Recording();probed=[];outcome={}
  def worker():
   try:outcome['report']=m.trial(c,lambda:probed.append(1) or {})
   except BaseException as exc:outcome['exc']=exc
  t=threading.Thread(target=worker);t.start();t.join(5)
  self.assertFalse(t.is_alive());self.assertNotIn('report',outcome)
  self.assertIsInstance(outcome.get('exc'),m.Refused)
  self.assertEqual(str(outcome['exc']),'not-main-thread')
  self.assertEqual(calls,[]);self.assertEqual(c.writes,[]);self.assertEqual(probed,[])
  self.assertIs(c.value,False)
  # The guard is not overbroad: the same control works from the main thread.
  r=m.trial(c,lambda:{'schema':1,'textRead':False,'writes':False,'truncated':False})
  self.assertTrue(r['passed']);self.assertEqual(c.writes,[True,False])
 def test_unconfirmed_enable_is_restored_without_probe(self):
  for stored in (False,1):
   with self.subTest(stored=stored):
    c=Fake();original=c.set;probed=[]
    def bad_enable(owner,value,c=c,original=original,stored=stored):
     if value:c.writes.append(value);c.value=stored
     else:original(owner,value)
    c.set=bad_enable
    r=m.trial(c,lambda:probed.append(1) or {})
    self.assertEqual(r['error'],'enable-not-confirmed');self.assertTrue(r['enableAttempted'])
    self.assertEqual(r['enableOutcome'],'unknown');self.assertFalse(r['statusChanged'])
    self.assertEqual(probed,[]);self.assertNotIn('probe',r)
    self.assertEqual(c.writes,[True,False]);self.assertIs(c.value,False)
    self.assertTrue(r['restored']);self.assertEqual(r['restoreAttempts'],1);self.assertNotIn('restoreError',r)
    self.assertFalse(r['measurementCompleted']);self.assertFalse(r['passed'])
if __name__=='__main__':unittest.main()
