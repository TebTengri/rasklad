# SPDX-License-Identifier: MIT
"""Admission/report tests: no Fcitx, compositor, host input or display."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('drain_fixture',Path(__file__).with_name('run.py'))
fixture=importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)

class RunnerTests(unittest.TestCase):
    def report(self,code):
        child=subprocess.Popen([sys.executable,'-c',code],stdout=subprocess.PIPE)
        try:return fixture.read_line(child,.2)
        finally:
            if child.poll() is None:child.terminate()
            child.wait(timeout=2)
            child.stdout.close()

    def test_partial_report_cannot_block_past_deadline(self):
        with self.assertRaisesRegex(RuntimeError,'server_timeout'):
            self.report("import sys,time;sys.stdout.write('{');sys.stdout.flush();time.sleep(5)")

    def test_oversized_report_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError,'report_bound'):
            self.report("print('x'*4097)")

    def test_complete_report(self):
        self.assertEqual(self.report("print('{\"ready\":true}')"),{'ready':True})

    def test_eof_is_not_readiness(self):
        with self.assertRaisesRegex(RuntimeError,'server_eof'):
            self.report('pass')

    def test_matching_counts_without_gate_evidence_cannot_pass(self):
        report={'completed':True,'vkPress':1,'vkRelease':1,
                'gatedPairSent':True,'ackMatched':True,'waitedForAck':True}
        self.assertTrue(fixture.delivery_passed(report,'baseline','ack-gated',True,0))
        for key in ('gatedPairSent','ackMatched','waitedForAck'):
            broken=dict(report);broken[key]=False
            self.assertFalse(fixture.delivery_passed(broken,'baseline','ack-gated',True,0))
        self.assertFalse(fixture.delivery_passed(report,'baseline','ack-gated',False,0))
        self.assertFalse(fixture.delivery_passed(report,'baseline','ack-gated',True,2))
        report['vkPress']=0
        self.assertFalse(fixture.delivery_passed(report,'baseline','ack-gated',True,0))
        self.assertTrue(fixture.delivery_passed(report,'baseline','original',True,0))

    def test_inactive_rejection_is_not_generic_failure(self):
        report={'raced':True,'fcitxExitCode':125,'grabs':1,'releasedGrabs':0,'vkPress':0,'vkRelease':0}
        self.assertTrue(fixture.delivery_passed(report,'keep-grab','inactive',True,2))
        for key,value in [('fcitxExitCode',-9),('raced',False),('grabs',2),('vkPress',1)]:
            broken=dict(report);broken[key]=value
            self.assertFalse(fixture.delivery_passed(broken,'keep-grab','inactive',True,2))
        self.assertFalse(fixture.delivery_passed(report,'keep-grab','inactive',False,2))
        self.assertFalse(fixture.delivery_passed(report,'keep-grab','inactive',True,0))

    def test_ab_lifecycle_and_release_evidence_required(self):
        report={'completed':True,'vkPress':1,'vkRelease':1,'releaseAfterAck':True,
                'ackMatched':True,'grabs':1,'releasedGrabs':0}
        self.assertTrue(fixture.delivery_passed(report,'keep-grab','grab-ab',True,0))
        for key,value in [('grabs',2),('releasedGrabs',1),('releaseAfterAck',False),('ackMatched',False)]:
            broken=dict(report);broken[key]=value
            self.assertFalse(fixture.delivery_passed(broken,'keep-grab','grab-ab',True,0))
        report.update(vkPress=0,grabs=2,releasedGrabs=1)
        self.assertTrue(fixture.delivery_passed(report,'baseline','grab-ab',True,0))

    def test_raw_inactive_requires_pair_and_reactivation_evidence(self):
        report={'completed':True,'vkPress':1,'vkRelease':1,'grabs':1,'releasedGrabs':0,
                'inactiveRawPairCompleted':True,'ackMatched':True}
        self.assertTrue(fixture.delivery_passed(report,'keep-grab','inactive-raw',True,0))
        for key in ('inactiveRawPairCompleted','ackMatched'):
            broken=dict(report);broken[key]=False
            self.assertFalse(fixture.delivery_passed(broken,'keep-grab','inactive-raw',True,0))

    def test_modifier_retained_requires_exact_admission_and_every_proof(self):
        proofs=('releaseAfterAck','ackMatched','oneLiveGrabInvariant','modifierDownObserved',
                'modifierUpObserved','shiftAtBothKeys','modifierReturnedToZero','modifierWaitedForAck')
        report=dict.fromkeys(proofs,True)
        report.update(completed=True,vkPress=1,vkRelease=1,grabs=1,releasedGrabs=0)
        self.assertTrue(fixture.delivery_passed(report,'keep-grab','modifier-retained',True,0))
        for key in proofs:
            for value in (False,None):
                broken=dict(report);broken[key]=value
                self.assertFalse(fixture.delivery_passed(broken,'keep-grab','modifier-retained',True,0),key)
        for key,value in (('grabs',2),('releasedGrabs',1),('vkPress',0),('vkRelease',0),('completed',False)):
            broken=dict(report);broken[key]=value
            self.assertFalse(fixture.delivery_passed(broken,'keep-grab','modifier-retained',True,0),key)
        self.assertFalse(fixture.delivery_passed(report,'keep-grab','modifier-retained',False,0))
        self.assertFalse(fixture.delivery_passed(report,'keep-grab','modifier-retained',True,2))
        for variant in ('baseline','drain','keep-grab-control'):
            self.assertFalse(fixture.delivery_passed(report,variant,'modifier-retained',True,0))
            with patch.object(fixture,'base') as base,patch.object(fixture.subprocess,'Popen') as start:
                with self.assertRaisesRegex(RuntimeError,'modifier_peer_requires_keep_grab'):
                    fixture.run(variant,'modifier-retained')
                base.assert_not_called();start.assert_not_called()

    def test_diagnostic_candidate_cannot_run_as_delivery_fix(self):
        with patch.object(fixture.subprocess,'Popen') as start:
            with self.assertRaisesRegex(RuntimeError,'diagnostic_candidate_not_delivery_fix'):
                fixture.run('drain')
            start.assert_not_called()

    def test_forged_environment_cannot_enter_private_mode(self):
        env={'RASKLAD_NATIVE_WAYLAND_LAB':'1','HOME':'/home/lab',
             'XDG_RUNTIME_DIR':'/run/lab','WAYLAND_DISPLAY':'wayland-lab'}
        with patch.dict(fixture.os.environ,env,clear=True),patch.object(fixture.subprocess,'Popen') as start:
            with self.assertRaisesRegex(RuntimeError,'namespace_guard'):fixture.inside('baseline')
            start.assert_not_called()

if __name__=='__main__':unittest.main()
