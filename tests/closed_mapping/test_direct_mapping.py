# SPDX-License-Identifier: MIT
"""Boundary guards for route-B host-parent nested mapping probe (no host/GPU)."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('direct_mapping', ROOT / 'tools/hyprland_mapping_probe.py')
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)

METADATA = {'statOnlyPrimary': '/dev/dri/card1', 'sysfs': '/owned/snapshot/sys',
            'primaryBindMustBeNodevReadOnly': True, 'hostSysfsBind': False}
GOOD = dict(ok=True, mappingMode=True, mappingVerified=True, observerCleanup=True,
            actualHyprland=True, hostParent=True, appWindowsMapped=2,
            observerOwnedCounts=[2, 2], observerClosedCounts=[1, 1])


def raw(report):
    return json.dumps(report).encode()


class DirectMapping(unittest.TestCase):
    def test_sandbox_runs_own_inside_with_only_one_parent_fd(self):
        args = lab.sandbox_args({'probe': 9}, [], METADATA, 7)
        self.assertEqual(args[-5:], ['--', '/usr/bin/python3', '-I', '/lab/hyprland_mapping_probe.py', '--inside'])
        self.assertEqual(args.count('--'), 1)
        self.assertNotIn('RASKLAD_CLOSED_MAPPING', args)
        self.assertNotIn('/lab/mapping.py', args[args.index('--'):])
        self.assertIn(['--setenv', 'RASKLAD_HOST_PARENT_FD', '7'],
                      [args[i:i + 3] for i in range(len(args) - 2)])
        grants = [args[i + 1:i + 3] for i, a in enumerate(args) if a == '--dev-bind']
        self.assertEqual(grants, [['/dev/dri/renderD128', '/dev/dri/renderD128']])
        binds = [args[i + 1:i + 3] for i, a in enumerate(args) if a == '--ro-bind']
        self.assertNotIn(['/sys', '/sys'], binds)
        for name in ('WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'DBUS_SESSION_BUS_ADDRESS', 'DISPLAY'):
            self.assertNotIn(name, args)
        for bad in (2, 1024, '7', None):
            with self.subTest(fd=bad), self.assertRaises(lab.base.Refused):
                lab.sandbox_args({'probe': 9}, [], METADATA, bad)

    def test_socket_readiness_or_partial_report_is_never_success(self):
        cases = [({'passed': True, 'phase': 'private_socket_ready', 'ok': True}, 0),
                 (dict(GOOD, actualHyprland=None), 0),
                 (dict(GOOD, hostParent=False), 0),
                 (GOOD, 1)]
        cases += [(dict(GOOD, **{key: 1}), 0) for key in ('ok', 'mappingMode', 'mappingVerified', 'observerCleanup')]
        for report, code in cases:
            with self.subTest(report=report, code=code):
                self.assertIs(lab.final_report(raw(report), code)['ok'], False)
        empty = lab.final_report(b'', 72)
        self.assertEqual((empty['ok'], empty['reason'], empty['scopeLimitsVerified']), (False, 'no-report', False))

    def test_success_is_rebuilt_from_bounded_fields_only(self):
        report = lab.final_report(raw(dict(GOOD, title='private', iterations='9', fieldBindingVerified=True)), 0)
        self.assertTrue(report['ok'])
        self.assertNotIn('title', report)
        self.assertNotIn('iterations', report)
        self.assertNotIn('fieldBindingVerified', report)
        self.assertEqual(report['observerOwnedCounts'], [2, 2])

    def test_failure_keeps_finite_stage_and_reason_through_both_layers(self):
        inner = dict(ok=False, phase='mapping-client', reason='parent-fd-shape', hyprlandExit=None,
                     hyprlandStartup='gpu_allocator_unavailable', text='private log',
                     clientStage=dict(initialMatch=True, remapMatch='yes', appWindowsMapped=9,
                                      observerOwnedCounts=[2, 'x'], observerClosedCounts=[0, 0], title='t'))
        report = lab.final_report(raw(lab.failure(inner, 1)), 1)
        self.assertEqual((report['phase'], report['reason']), ('mapping-client', 'parent-fd-shape'))
        self.assertEqual(report['clientStage'], {'initialMatch': True, 'observerClosedCounts': [0, 0]})
        self.assertNotIn('text', report)
        unknown = lab.failure(dict(inner, reason='/run/user/1000/wayland-1'), 1)
        self.assertEqual(unknown['reason'], 'mapping-not-verified')

    def test_default_invocation_is_inert(self):
        with mock.patch.object(lab, 'run', side_effect=AssertionError('run')), \
                mock.patch.object(lab, 'host_parent', side_effect=AssertionError('host')), \
                mock.patch.object(sys, 'argv', ['hyprland_mapping_probe.py']), \
                contextlib.redirect_stdout(io.StringIO()) as out:
            self.assertEqual(lab.main(), 0)
        plan = json.loads(out.getvalue())
        self.assertEqual((plan['mode'], plan['hostConnectionOpened'], plan['fieldBindingVerified'],
                          plan['userWindowAuthority'], plan['ownerMustNotType'], plan['automatedRetries']),
                         ('plan', False, False, False, True, 0))


if __name__ == '__main__':
    unittest.main()
