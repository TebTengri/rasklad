# SPDX-License-Identifier: MIT
"""Epoch selection preserves the server witness's capabilities and consent gate."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('epoch_server_guards',ROOT/'tests/wayland_ime/test_server_route_witness_guards.py')
common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)
OPTIONS={**common.OPTIONS,'server_route_witness':True}
class GrabEpochGuards(unittest.TestCase):
    def test_selected_plan_diff_is_only_epoch_marker(self):
        for module,make_args in ((common.REFERENCE,common.reference_args),(common.EXACT,common.exact_args)):
            with self.subTest(module=module.__name__),patch.object(module.base,'regular_file'):
                old=make_args(**OPTIONS)
                self.assertEqual(old,make_args(**OPTIONS,grab_epoch_witness=False))
                selected=make_args(**OPTIONS,grab_epoch_witness=True)
                at=selected.index('RASKLAD_GRAB_EPOCH_WITNESS_LAB')
                self.assertEqual(selected[at-1:at+2],['--setenv','RASKLAD_GRAB_EPOCH_WITNESS_LAB','1'])
                del selected[at-1:at+2]
                self.assertEqual(old,selected)
                self.assertNotIn('LD_PRELOAD',selected)
    def test_missing_server_gate_refuses_before_reads_or_processes(self):
        for module,make_args in ((common.REFERENCE,common.reference_args),(common.EXACT,common.exact_args)):
            with self.subTest(module=module.__name__),patch.object(module.base,'regular_file') as reads,patch.object(module.subprocess,'Popen') as spawn,patch.object(module.core_guard,'validate_core_entry') as abi:
                with self.assertRaisesRegex(module.base.Refused,'grab_epoch_requires_server_route_witness'):
                    make_args(grab_epoch_witness=True)
                with self.assertRaisesRegex(module.base.Refused,'grab_epoch_requires_server_route_witness'):
                    module.run('/unused',grab_epoch_witness=True)
                reads.assert_not_called();spawn.assert_not_called();abi.assert_not_called()
    def test_epoch_still_requires_entire_querying_witness_chain(self):
        for module,make_args in ((common.REFERENCE,common.reference_args),(common.EXACT,common.exact_args)):
            for removed in ['grab_entry_witness','core_entry_witness','addon_witness','focus_stress','space_telemetry']:
                options={**OPTIONS,'grab_epoch_witness':True,removed:False}
                with self.subTest(module=module.__name__,removed=removed),patch.object(module.base,'regular_file') as reads,patch.object(module.subprocess,'Popen') as spawn:
                    with self.assertRaises(module.base.Refused):make_args(**options)
                    reads.assert_not_called();spawn.assert_not_called()
if __name__=='__main__':unittest.main()
