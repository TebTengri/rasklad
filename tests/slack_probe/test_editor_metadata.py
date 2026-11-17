# SPDX-License-Identifier: MIT
import importlib.util,unittest
from enum import IntEnum
from pathlib import Path
from types import SimpleNamespace
s=importlib.util.spec_from_file_location('metadata',Path(__file__).resolve().parents[2]/'tools/slack_accessibility_probe.py')
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class State(IntEnum):ACTIVE=0;EDITABLE=1;FOCUSED=2;MULTI_LINE=3
class Role(IntEnum):
 ENTRY=1;PASSWORD_TEXT=2
 @property
 def value_nick(self):return self.name.lower()
Atspi=SimpleNamespace(StateType=State,Role=Role)
class EditorMetadataTests(unittest.TestCase):
 def scan(self,budget):
  root=m.ROOT_PATH;focus=root+'/g12/n11';calls=[]
  def transport(bus,path,interface,method,arg):
   calls.append(method)
   if method=='GetChildren':
    if path==root:return ([(bus,root+'/g'+str(i)) for i in range(16)],)
    if path.count('/')==root.count('/')+1:return ([(bus,path+'/n'+str(i)) for i in range(13)],)
    return ([],)
   if method=='GetState':return ([14 if path==focus else 0,0],)
   if method=='GetInterfaces':return (['org.a11y.atspi.Text'] if path==focus else [],)
   if method=='GetRole':return (int(Role.ENTRY),)
   raise AssertionError('unexpected method')
  reader=m.Reader(transport);reader.owned={':1.10'}
  report={'truncated':False,'editableCandidates':[],'focusedEditableObjects':[]}
  m.walk_metadata(reader,Atspi,report,budget)
  return report,calls
 def test_focused_seventeenth_candidate_is_retained_and_summary_marked(self):
  report={'editableCandidates':[]}
  for i in range(16):m.append_candidate(report,{'focused':False,'multiline':False})
  focused={'focused':True,'multiline':True,'editableTextInterface':False}
  m.append_candidate(report,focused)
  self.assertTrue(report['candidateSummaryTruncated']);self.assertEqual(len(report['editableCandidates']),16)
  self.assertIn(focused,report['editableCandidates'])
 def test_deeper_editor_requires_explicit_larger_budget(self):
  small,_=self.scan(128);large,calls=self.scan(512)
  self.assertTrue(small['truncated']);self.assertEqual(small['editableCandidates'],[])
  self.assertFalse(large['truncated']);self.assertEqual(large['inspectedNodes'],225)
  self.assertEqual(len(large['editableCandidates']),1)
  field=large['editableCandidates'][0]
  self.assertTrue(field['focused']);self.assertTrue(field['multiline']);self.assertTrue(field['editableState'])
  self.assertFalse(field['activeAncestor']);self.assertFalse(field['editableTextInterface'])
  self.assertEqual(large['focusedEditableObjects'],[])
  self.assertLessEqual(set(calls),m.NODE_METHODS)
if __name__=='__main__':unittest.main()
