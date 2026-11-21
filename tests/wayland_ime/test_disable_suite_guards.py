# SPDX-License-Identifier: MIT
"""Disable fixture admission is explicit and grants no new sandbox access."""
import importlib.util
import inspect
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('disable_lab',ROOT/'tools/wayland_ime_lab.py')
lab=importlib.util.module_from_spec(spec);spec.loader.exec_module(lab)

class DisableSuiteGuards(unittest.TestCase):
    def args(self,**options):
        paths={'addon':ROOT/'build/chromium-ime/profile-addon/raskladlab.so'}
        with patch.object(lab.chrome,'argv',side_effect=lambda paths,*unused:['--ro-bind',str(paths['addon']),'/lab/addons/raskladlab.so','--','/lab/runner.py']):
            return lab.sandbox_args(paths,{},None,Path('/runtime'),Path('/config'),**options)

    def test_appended_inert_parameters(self):
        for function in (lab.run,lab.sandbox_args):
            params=list(inspect.signature(function).parameters.values())
            while params[-1].name in ('manual_suite','manual_gesture','product_suite'):  # SYS-225: keyword-only, run() only
                self.assertIs(params[-1].kind,inspect.Parameter.KEYWORD_ONLY);self.assertIs(params[-1].default,None if params[-1].name=='product_suite' else False);params=params[:-1]
            self.assertEqual(params[-1].name,'slack_gate')
            self.assertIs(params[-1].default,False)
            self.assertEqual(params[-2].name,'disable_suite')
            self.assertIs(params[-2].default,False)
            self.assertEqual(params[-3].name,'frontend_variant')

    def test_default_has_no_new_access_or_environment(self):
        with patch.object(lab.base,'regular_file') as regular:
            default=self.args();explicit=self.args(disable_suite=False)
            self.assertEqual(default,explicit)
            self.assertNotIn('RASKLAD_DISABLE_TEST_LAB',default)
            self.assertFalse(any('disable-addon' in item for item in default))
            regular.assert_not_called()

    def test_selected_changes_only_addon_and_child_flag(self):
        common={'frontend_variant':'keep-grab','snapshot_fds':{'/lab/addons/libwaylandim.so':41,'/lab/rasklad-wayland-keys':42,'/lab/addons/raskladlab.so':43}}
        original=self.args(**common);selected=self.args(disable_suite=True,**common)
        i=selected.index('RASKLAD_DISABLE_TEST_LAB')
        self.assertEqual(selected[i-1:i+2],['--setenv','RASKLAD_DISABLE_TEST_LAB','1'])
        self.assertEqual(selected[:i-1]+selected[i+2:],original)
        for target,fd in (('/lab/rasklad-wayland-keys',42),('/lab/addons/raskladlab.so',43)):
            i=selected.index(target)
            self.assertEqual(selected[i-4:i],['--perms','0755','--ro-bind-data',str(fd)])
        with patch.object(lab.chrome,'argv',return_value=['--','/lab/runner.py']) as argv:
            lab.sandbox_args({'addon':Path('/original')},{},None,Path('/runtime'),Path('/config'),disable_suite=True,frontend_variant='keep-grab',snapshot_fds={'/lab/addons/libwaylandim.so':41})
            self.assertEqual(argv.call_args.args[0]['addon'],ROOT/'build/chromium-ime/disable-addon/raskladlab.so')

    def test_incompatible_matrix_refuses_before_access(self):
        cases=[{'frontend_variant':variant} for variant in ('system','baseline')]
        cases += [{'frontend_variant':'keep-grab',name:True} for name in ('nested_smoke','focus_stress','space_telemetry','addon_witness','witness_no_query','core_entry_witness','grab_entry_witness','server_route_witness','grab_epoch_witness')]
        cases += [{'frontend_variant':'keep-grab','focus_repeats':count} for count in (2,3,4,5)]
        for options in cases:
            with self.subTest(options=options),patch.object(lab.chrome,'argv') as argv,patch.object(lab.chrome,'files') as files,patch.object(lab,'validate_frontend') as frontend,patch.object(lab.base,'regular_file') as regular,patch.object(lab.subprocess,'Popen') as popen:
                with self.assertRaisesRegex(lab.base.Refused,'disable_suite_'):
                    lab.sandbox_args({}, {}, None, Path('/runtime'),Path('/config'),disable_suite=True,**options)
                with self.assertRaisesRegex(lab.base.Refused,'disable_suite_'):
                    lab.run('/unused',disable_suite=True,**options)
                argv.assert_not_called();files.assert_not_called();frontend.assert_not_called();regular.assert_not_called();popen.assert_not_called()

if __name__=='__main__':unittest.main()
