# SPDX-License-Identifier: MIT
from pathlib import Path
import importlib.machinery,importlib.util,signal,sys,unittest
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
loader=importlib.machinery.SourceFileLoader('quit_request',str(ROOT/'bin/slack-request-quit'))
spec=importlib.util.spec_from_loader(loader.name,loader);q=importlib.util.module_from_spec(spec);loader.exec_module(q)
class QuitGuards(unittest.TestCase):
 def test_unique_tree_root_not_arbitrary_oldest_process(self):
  rows={1:{'pid':1,'ppid':99,'start':1},2:{'pid':2,'ppid':1,'start':2}}
  self.assertEqual(q.unique_root({1,2},rows.get),rows[1])
  rows[2]['ppid']=100;self.assertIsNone(q.unique_root({1,2},rows.get))
 def test_signal_uses_verified_pidfd_once_and_closes_it(self):
  row={'pid':1,'ppid':99,'start':1};sent=[];closed=[]
  self.assertTrue(q.request(row,lambda _:row,lambda *_:7,lambda *a:sent.append(a),closed.append))
  self.assertEqual(sent,[(7,signal.SIGTERM,None,0)]);self.assertEqual(closed,[7])
 def test_changed_process_never_signalled(self):
  row={'pid':1,'ppid':99,'start':1};sent=[];closed=[];calls=0
  def inspect(_):
   nonlocal calls;calls+=1;return row if calls==1 else {**row,'start':2}
  self.assertFalse(q.request(row,inspect,lambda *_:7,lambda *a:sent.append(a),closed.append))
  self.assertEqual(sent,[]);self.assertEqual(closed,[7])
 def test_missing_target_does_not_open_a_pidfd(self):
  calls=[];row={'pid':1,'ppid':99,'start':1}
  self.assertFalse(q.request(row,lambda _:None,lambda *a:calls.append(a),lambda *_:None,lambda _:None));self.assertEqual(calls,[])
if __name__=='__main__':unittest.main()
