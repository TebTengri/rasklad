# SPDX-License-Identifier: MIT
"""Witness selection must not start a different or less isolated experiment."""
import contextlib
import importlib.util
import inspect
import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT/'tools'/filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


REFERENCE = load('witness_reference', 'wayland_ime_lab.py')
EXACT = load('witness_exact', 'hyprland_ime_lab.py')
REASON = 'addon_witness_requires_focus_and_space_telemetry'
MODULE = ROOT/'tests/chromium_ime/addon-witness.mjs'
BINARY = ROOT/'build/chromium-ime/witness-addon/raskladlab.so'
CONFIG = ROOT/'tests/fcitx_transport/raskladwitness.conf'


def bindings(args):
    return [tuple(args[i+1:i+3]) for i, item in enumerate(args) if item == '--ro-bind']


def environment(args):
    return dict(tuple(args[i+1:i+3]) for i, item in enumerate(args) if item == '--setenv')


def reference_args(**options):
    paths = REFERENCE.chrome.files()
    paths.pop('xvfb'); paths.pop('keys')
    paths['addon'] = ROOT/'build/chromium-ime/profile-addon/raskladlab.so'
    return REFERENCE.sandbox_args(paths, {}, None, Path('/runtime'), Path('/config'), **options)


def exact_args(**options):
    paths = EXACT.chrome.files()
    paths.pop('xvfb'); paths.pop('keys')
    paths['addon'] = ROOT/'build/chromium-ime/profile-addon/raskladlab.so'
    plan = {'sysfs':'/private/gpu-metadata/sys', 'statOnlyPrimary':'/dev/dri/card0',
            'primaryBindMustBeNodevReadOnly':True, 'hostSysfsBind':False}
    return EXACT.sandbox_args(Path('/private'), 8, plan, paths, {},
                              {'/lab/rasklad-wayland-keys':10}, **options)


class WitnessGuards(unittest.TestCase):
    def test_optional_signature_is_trailing_and_defaults_false(self):
        for module in (REFERENCE, EXACT):
            for function in (module.run, module.sandbox_args):
                with self.subTest(module=module.__name__, function=function.__name__):
                    parameters = list(inspect.signature(function).parameters.values())
                    self.assertEqual(parameters[-1].name, 'addon_witness')
                    self.assertIs(parameters[-1].default, False)

    def test_invalid_combinations_refuse_before_endpoint_files_or_process(self):
        for module in (REFERENCE, EXACT):
            for focus, telemetry in ((False,False),(True,False),(False,True)):
                with self.subTest(module=module.__name__, focus=focus, telemetry=telemetry):
                    with patch.object(module.base,'regular_file') as regular, patch.object(module.subprocess,'Popen') as popen:
                        if module is EXACT:
                            with patch.object(module.probe,'endpoint') as endpoint:
                                with self.assertRaisesRegex(module.base.Refused,REASON):
                                    module.run('/unused',focus_stress=focus,space_telemetry=telemetry,addon_witness=True)
                                endpoint.assert_not_called()
                        else:
                            with patch.object(module.chrome,'files') as files:
                                with self.assertRaisesRegex(module.base.Refused,REASON):
                                    module.run('/unused',focus_stress=focus,space_telemetry=telemetry,addon_witness=True)
                                files.assert_not_called()
                        regular.assert_not_called(); popen.assert_not_called()

    def test_invalid_sandbox_combinations_refuse_before_base_plan(self):
        for module, make_args, owner, helper in ((REFERENCE,reference_args,REFERENCE.chrome,'argv'),
                                                (EXACT,exact_args,EXACT.probe,'sandbox_args')):
            for focus, telemetry in ((False,False),(True,False),(False,True)):
                with self.subTest(module=module.__name__,focus=focus,telemetry=telemetry):
                    with patch.object(owner,helper) as base_plan:
                        with self.assertRaisesRegex(module.base.Refused,REASON):
                            make_args(focus_stress=focus,space_telemetry=telemetry,addon_witness=True)
                        base_plan.assert_not_called()

    def test_witness_selects_fixed_binary_config_and_module_without_new_rights(self):
        for module, make_args in ((REFERENCE,reference_args),(EXACT,exact_args)):
            with self.subTest(module=module.__name__), patch.object(module.base,'regular_file') as regular:
                default = make_args(focus_stress=True,space_telemetry=True)
                regular.assert_not_called()
                selected = make_args(focus_stress=True,space_telemetry=True,addon_witness=True)
                regular.assert_called_once_with(MODULE)
                self.assertNotIn('/lab/addon-witness.mjs',default)
                self.assertIn((str(MODULE),'/lab/addon-witness.mjs'),bindings(selected))
                self.assertIn((str(BINARY),'/lab/addons/raskladlab.so'),bindings(selected))
                self.assertIn((str(CONFIG),'/lab/addon.conf'),bindings(selected))
                self.assertNotIn(str(ROOT/'build/chromium-ime/profile-addon/raskladlab.so'),selected)
                old_env=environment(default);new_env=environment(selected)
                self.assertNotIn('RASKLAD_ADDON_WITNESS_LAB',old_env)
                self.assertEqual(new_env.pop('RASKLAD_ADDON_WITNESS_LAB'),'1')
                self.assertEqual(old_env,new_env)
                for flag in ('--dev-bind','--unshare-all','--clearenv','--cap-drop','--preserve-fds'):
                    self.assertEqual(default.count(flag),selected.count(flag))
                for forbidden in ('--share-net','--share-pid','--no-sandbox'):
                    self.assertNotIn(forbidden,selected)
                removed=set(bindings(default))-set(bindings(selected))
                added=set(bindings(selected))-set(bindings(default))
                self.assertEqual(removed,{(str(ROOT/'build/chromium-ime/profile-addon/raskladlab.so'),'/lab/addons/raskladlab.so'),
                                          (str(ROOT/'tests/fcitx_transport/raskladlab.conf'),'/lab/addon.conf')})
                self.assertEqual(added,{(str(BINARY),'/lab/addons/raskladlab.so'),(str(CONFIG),'/lab/addon.conf'),
                                        (str(MODULE),'/lab/addon-witness.mjs')})

    def test_selected_module_must_pass_regular_file_guard(self):
        for module, make_args in ((REFERENCE,reference_args),(EXACT,exact_args)):
            with self.subTest(module=module.__name__), patch.object(module.base,'regular_file',side_effect=module.base.Refused('unsafe_artifact')):
                with self.assertRaisesRegex(module.base.Refused,'unsafe_artifact'):
                    make_args(focus_stress=True,space_telemetry=True,addon_witness=True)

    def test_default_and_valid_witness_plans_are_inert(self):
        for module in (REFERENCE,EXACT):
            for arguments,witness in (([],False),(['--focus-stress','--space-telemetry','--addon-witness'],True)):
                with self.subTest(module=module.__name__,witness=witness):
                    output=io.StringIO()
                    with patch('sys.argv',['lab',*arguments]), patch.object(module,'run') as run, patch.object(module,'inside') as inside, patch.object(module.subprocess,'Popen') as popen, contextlib.redirect_stdout(output):
                        self.assertEqual(module.main(),0)
                        run.assert_not_called(); inside.assert_not_called(); popen.assert_not_called()
                    report=json.loads(output.getvalue())
                    self.assertEqual(report['status'],'plan_only')
                    self.assertIs(report['addonWitnessPlanned'],witness)

    def test_cli_invalid_selection_does_not_start_run(self):
        for module in (REFERENCE,EXACT):
            for arguments in (['--addon-witness'],['--addon-witness','--focus-stress'],['--addon-witness','--space-telemetry']):
                with self.subTest(module=module.__name__,arguments=arguments):
                    output=io.StringIO()
                    with patch('sys.argv',['lab',*arguments]), patch.object(module,'run') as run, patch.object(module,'inside') as inside, patch.object(module.subprocess,'Popen') as popen, contextlib.redirect_stdout(output):
                        self.assertEqual(module.main(),2)
                        run.assert_not_called(); inside.assert_not_called(); popen.assert_not_called()
                    self.assertEqual(json.loads(output.getvalue())['reason'],REASON)


if __name__ == '__main__':
    unittest.main()
