# SPDX-License-Identifier: MIT
"""Pure preparation tests: no parent endpoint, GPU metadata, or runtime access."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
lab=load('exact_private_frontend',ROOT/'tools/hyprland_ime_lab.py')

class ExactPrivateFrontend(unittest.TestCase):
    def arguments(self,variant,snapshots):
        return lab.sandbox_args(Path('/private'),9,{}, {}, {},snapshots,frontend_variant=variant)

    def test_selected_sealed_mount_and_child_marker_only(self):
        for variant in ('baseline','keep-grab'):
            with patch.object(lab.probe,'sandbox_args',return_value=['bwrap','--','old']):
                args=self.arguments(variant,{'/lab/rasklad-wayland-keys':10,'/lab/addons/libwaylandim.so':11})
            i=args.index('/lab/addons/libwaylandim.so')
            self.assertEqual(args[i-4:i+1],['--perms','0755','--ro-bind-data','11','/lab/addons/libwaylandim.so'])
            self.assertEqual(args[args.index('RASKLAD_PRIVATE_FRONTEND_VARIANT')+1],variant)
            self.assertNotIn('RASKLAD_FCITX_KEEP_GRAB_LAB',args)
            self.assertNotIn('RASKLAD_KEEP_GRAB_INACTIVE_RAW',args)
            self.assertEqual(args[-5:],['--','/usr/bin/python3','-I','/lab/runner.py','--inside'])

    def test_no_unsealed_fallback_and_invalid_variant_before_probe(self):
        with patch.object(lab.probe,'sandbox_args') as probe:
            for variant in ('baseline','keep-grab'):
                with self.assertRaisesRegex(lab.base.Refused,'snapshot_required'):self.arguments(variant,{})
            with self.assertRaisesRegex(lab.base.Refused,'private_frontend_variant'):self.arguments('other',{})
            probe.assert_not_called()

    def test_failed_shared_admission_precedes_any_host_access(self):
        with patch.object(lab.reference,'validate_frontend',side_effect=lab.reference.base.Refused('private_frontend_build_record')) as validate,patch.object(lab.probe,'endpoint') as endpoint,patch.object(lab,'load') as load_module,patch.object(lab.subprocess,'Popen') as spawn:
            with self.assertRaisesRegex(lab.base.Refused,'build_record'):lab.run('/unused',frontend_variant='keep-grab')
            validate.assert_called_once_with('keep-grab');endpoint.assert_not_called();load_module.assert_not_called();spawn.assert_not_called()

    def test_plans_remain_inert_and_explicitly_require_permission(self):
        for variant in ('system','baseline','keep-grab'):
            output=io.StringIO()
            with patch('sys.argv',['lab','--frontend-variant',variant]),patch.object(lab,'run') as run,patch.object(lab.reference,'validate_frontend') as validate,patch.object(lab.probe,'endpoint') as endpoint,patch.object(lab.subprocess,'Popen') as spawn,contextlib.redirect_stdout(output):
                self.assertEqual(lab.main(),0)
                run.assert_not_called();validate.assert_not_called();endpoint.assert_not_called();spawn.assert_not_called()
            report=json.loads(output.getvalue())
            self.assertEqual(report['frontendVariant'],variant);self.assertTrue(report['requiresOwnerApproval'])
            self.assertFalse(report['hostDisplayConnected']);self.assertEqual(report['maxTrialSeconds'],75)

if __name__=='__main__':unittest.main()
