# SPDX-License-Identifier: MIT
import importlib.util,unittest
from pathlib import Path
s=importlib.util.spec_from_file_location('metadata',Path(__file__).resolve().parents[2]/'tools/slack_accessibility_probe.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Clock:
 def __init__(self):self.t=0
 def now(self):return self.t
 def pause(self,n):self.t+=n
class ObservationTests(unittest.TestCase):
 def scan(self,transport,observe=True,pids=lambda budget:{42}):
  c=Clock();r=m.Reader(transport);report={'pidLookups':0,'pidLookupErrors':0,'truncated':False}
  m.discover_peers(r,report,1,observe,pids,c.now,c.pause)
  return r,report,c
 def test_delayed_peer_is_observed_without_app_object_reads(self):
  calls=[];scans=[]
  def transport(bus,path,interface,method,arg):
   calls.append(method)
   if method=='ListNames':
    scans.append(1);return ([ ':1.1'] if len(scans)<3 else [':1.1',':1.2'],)
   return (42 if arg==':1.2' else 99,)
  r,report,c=self.scan(transport)
  self.assertEqual(r.owned,{':1.2'});self.assertEqual(report['discoveryScans'],3)
  self.assertEqual(report['pidLookups'],2);self.assertEqual(c.t,.5)
  self.assertTrue(set(calls)<={'ListNames','GetConnectionUnixProcessID'})
 def test_absent_peer_waits_bounded_window(self):
  r,report,c=self.scan(lambda *args:([],))
  self.assertEqual(r.owned,set());self.assertEqual(c.t,1)
  self.assertEqual(report['discoveryScans'],4)
 def test_snapshot_stays_single_scan(self):
  r,report,c=self.scan(lambda *args:([],),False)
  self.assertEqual(c.t,0);self.assertEqual(report['discoveryScans'],1)
 def test_missing_process_does_not_query_bus(self):
  def forbidden(*args):raise AssertionError('must not query')
  r,report,c=self.scan(forbidden,pids=lambda budget:set())
  self.assertEqual(report['targetProcesses'],0);self.assertEqual(c.t,0)
 def test_slow_pid_source_receives_remaining_budget_and_stops_bus(self):
  c=Clock();r=m.Reader(lambda *args:(_ for _ in ()).throw(AssertionError('late bus call')))
  report={'pidLookups':0,'pidLookupErrors':0,'truncated':False};budgets=[]
  def slow(budget):budgets.append(budget);c.pause(budget);return {42}
  m.discover_peers(r,report,.4,True,slow,c.now,c.pause)
  self.assertEqual(budgets,[.4]);self.assertEqual(c.t,.4)
 def test_lookup_volume_is_bounded(self):
  n=[0]
  def transport(bus,path,interface,method,arg):
   if method=='ListNames':
    n[0]+=1;return ([f':{n[0]}.{i}' for i in range(200)],)
   return (99,)
  r,report,c=self.scan(transport)
  self.assertTrue(report['truncated']);self.assertEqual(report['pidLookups'],256)
if __name__=='__main__':unittest.main()
