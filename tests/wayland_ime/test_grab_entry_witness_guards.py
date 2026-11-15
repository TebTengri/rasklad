# SPDX-License-Identifier: MIT
"""Grab callback diagnostics keep the same isolated capabilities and hard bounds."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('grab_core_tests',ROOT/'tests/wayland_ime/test_core_entry_witness_guards.py')
common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)
REFERENCE,EXACT=common.REFERENCE,common.EXACT
OPTIONS={**common.OPTIONS,'core_entry_witness':True}
GRAB_ADDON=ROOT/'build/chromium-ime/grab-entry-addon/raskladlab.so'
GRAB_SHIM=ROOT/'build/chromium-ime/grab-entry-shim/raskladcoreentry.so'


def remove_marker(arguments,name,value):
    args=list(arguments);at=args.index(name)
    if args[at-1:at+2]!=['--setenv',name,value]:raise AssertionError('marker_shape')
    del args[at-1:at+2];return args


class GrabEntryGuards(unittest.TestCase):
    def test_grab_selection_changes_only_addon_and_marker_with_same_sealed_target(self):
        for module,make_args in ((REFERENCE,common.core_reference_args),(EXACT,common.core_exact_args)):
            with self.subTest(module=module.__name__),patch.object(module.base,'regular_file'):
                core=make_args(**OPTIONS)
                grab=make_args(**OPTIONS,grab_entry_witness=True)
                self.assertNotIn('RASKLAD_GRAB_ENTRY_WITNESS_LAB',core)
                self.assertNotIn('LD_PRELOAD',grab)
                self.assertNotIn(str(GRAB_SHIM),grab)
                cleaned=remove_marker(grab,'RASKLAD_GRAB_ENTRY_WITNESS_LAB','1')
                self.assertEqual(cleaned,[str(GRAB_ADDON) if a==str(common.CORE_ADDON) else a for a in core])
                self.assertIn('/lab/raskladcoreentry.so',grab)

    def test_grab_requires_core_before_admission_plan_or_process(self):
        for module,make_args,owner,helper in ((REFERENCE,common.core_reference_args,REFERENCE.chrome,'argv'),
                                             (EXACT,common.core_exact_args,EXACT.probe,'sandbox_args')):
            with self.subTest(module=module.__name__),patch.object(module.core_guard,'validate_core_entry') as admit,patch.object(module.base,'regular_file') as regular,patch.object(module.subprocess,'Popen') as popen,patch.object(owner,helper) as plan:
                with self.assertRaisesRegex(module.base.Refused,'grab_entry_witness_requires_core_entry_witness'):
                    make_args(**common.OPTIONS,grab_entry_witness=True)
                with self.assertRaisesRegex(module.base.Refused,'grab_entry_witness_requires_core_entry_witness'):
                    module.run('/unused',**common.OPTIONS,grab_entry_witness=True)
                admit.assert_not_called();regular.assert_not_called();popen.assert_not_called();plan.assert_not_called()

    def test_grab_admits_fixed_shim_before_endpoint(self):
        for module in (REFERENCE,EXACT):
            def regular(path,*args):
                if path==GRAB_SHIM:raise module.base.Refused('fixed_grab_shim_observed')
            with self.subTest(module=module.__name__),patch.object(module.core_guard,'validate_core_entry',return_value=True),patch.object(module.os,'geteuid',return_value=1000),patch.object(module.os,'stat',return_value=SimpleNamespace(st_uid=0,st_mode=0o100755)),patch.object(module.base,'regular_file',side_effect=regular) as admit,patch.object(module.subprocess,'Popen') as popen:
                if module is EXACT:
                    with patch.object(module.probe,'endpoint') as endpoint:
                        with self.assertRaisesRegex(module.base.Refused,'fixed_grab_shim_observed'):
                            module.run('/unused',**OPTIONS,grab_entry_witness=True)
                        endpoint.assert_not_called()
                else:
                    with self.assertRaisesRegex(module.base.Refused,'fixed_grab_shim_observed'):
                        module.run('/unused',**OPTIONS,grab_entry_witness=True)
                admit.assert_any_call(GRAB_SHIM);popen.assert_not_called()

    def test_repeats_adds_only_bounded_environment_value(self):
        for module,make_args in ((REFERENCE,common.common.reference_args),(EXACT,common.common.exact_args)):
            with self.subTest(module=module.__name__):
                once=make_args(focus_stress=True)
                self.assertNotIn('RASKLAD_FOCUS_REPEATS_LAB',once)
                for count in (2,5):
                    repeated=make_args(focus_stress=True,focus_repeats=count)
                    self.assertEqual(remove_marker(repeated,'RASKLAD_FOCUS_REPEATS_LAB',str(count)),once)

    def test_invalid_repeats_refuse_before_files_abi_or_process(self):
        for module,make_args in ((REFERENCE,common.common.reference_args),(EXACT,common.common.exact_args)):
            for count in (True,False,None,'2',2.0,0,6,-1):
                with self.subTest(module=module.__name__,count=count),patch.object(module.core_guard,'validate_core_entry') as admit,patch.object(module.base,'regular_file') as regular,patch.object(module.subprocess,'Popen') as popen:
                    with self.assertRaisesRegex(module.base.Refused,'focus_repeats_bound'):
                        make_args(focus_stress=True,focus_repeats=count)
                    with self.assertRaisesRegex(module.base.Refused,'focus_repeats_bound'):
                        module.run('/unused',focus_stress=True,focus_repeats=count)
                    admit.assert_not_called();regular.assert_not_called();popen.assert_not_called()
            with self.subTest(module=module.__name__,focus=False),patch.object(module.base,'regular_file') as regular,patch.object(module.subprocess,'Popen') as popen:
                with self.assertRaisesRegex(module.base.Refused,'focus_repeats_requires_focus_suite'):
                    make_args(focus_repeats=2)
                with self.assertRaisesRegex(module.base.Refused,'focus_repeats_requires_focus_suite'):
                    module.run('/unused',focus_repeats=2)
                regular.assert_not_called();popen.assert_not_called()

    def test_grab_child_preload_remains_scoped_to_fcitx(self):
        clients=common.clients
        env={'RASKLAD_CORE_ENTRY_WITNESS_LAB':'1','RASKLAD_GRAB_ENTRY_WITNESS_LAB':'1','RASKLAD_ADDON_WITNESS_LAB':'1',
             'RASKLAD_FOCUS_STRESS_LAB':'1','RASKLAD_SPACE_TELEMETRY_LAB':'1','RASKLAD_FOCUS_REPEATS_LAB':'5'}
        with patch.dict(os.environ,env,clear=True):
            child=clients.fcitx_child_env(REFERENCE.base)
            self.assertEqual(child['LD_PRELOAD'],'/lab/raskladcoreentry.so')
            self.assertNotIn('LD_PRELOAD',os.environ)
            self.assertEqual({k:v for k,v in child.items() if k!='LD_PRELOAD'},dict(os.environ))

    def test_cli_rejects_grab_without_core_and_out_of_range_repeat(self):
        for module in (REFERENCE,EXACT):
            output=io.StringIO()
            with self.subTest(module=module.__name__),patch('sys.argv',['lab','--grab-entry-witness']),patch.object(module,'run') as run,patch.object(module.subprocess,'Popen') as popen,contextlib.redirect_stdout(output):
                self.assertEqual(module.main(),2);run.assert_not_called();popen.assert_not_called()
            self.assertEqual(json.loads(output.getvalue())['reason'],'grab_entry_witness_requires_core_entry_witness')
            with patch('sys.argv',['lab','--focus-stress','--focus-repeats','6']),patch.object(module,'run') as run,patch.object(module.subprocess,'Popen') as popen,contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:module.main()
                self.assertEqual(error.exception.code,2);run.assert_not_called();popen.assert_not_called()

    def test_inert_plans_preserve_cap_and_expose_repeat_count(self):
        for module in (REFERENCE,EXACT):
            for arguments,grab,repeats in (([],False,1),(['--focus-stress','--focus-repeats','5'],False,5),
                (['--focus-stress','--space-telemetry','--addon-witness','--core-entry-witness','--grab-entry-witness','--focus-repeats','5'],True,5)):
                output=io.StringIO()
                with self.subTest(module=module.__name__,grab=grab,repeats=repeats),patch('sys.argv',['lab',*arguments]),patch.object(module,'run') as run,patch.object(module.core_guard,'validate_core_entry') as admit,patch.object(module.subprocess,'Popen') as popen,contextlib.redirect_stdout(output):
                    self.assertEqual(module.main(),0);run.assert_not_called();admit.assert_not_called();popen.assert_not_called()
                report=json.loads(output.getvalue())
                self.assertIs(report['grabEntryWitnessPlanned'],grab);self.assertEqual(report['focusRepeatCount'],repeats)
                if module is EXACT:self.assertEqual(report['maxTrialSeconds'],75)


if __name__=='__main__':unittest.main()
