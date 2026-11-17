# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('closed_mapping', ROOT / 'tools/closed_hyprland_mapping.py')
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)


class Boundary(unittest.TestCase):
    def test_only_one_device_and_private_namespace(self):
        args = lab.sandbox_args({'probe': 9}, [], {
            'statOnlyPrimary': '/dev/dri/card1', 'sysfs': '/owned/snapshot/sys',
            'primaryBindMustBeNodevReadOnly': True, 'hostSysfsBind': False})
        self.assertIn('--unshare-all', args)
        self.assertIn('--clearenv', args)
        grants = [args[i+1:i+3] for i, arg in enumerate(args) if arg == '--dev-bind']
        self.assertEqual(grants, [['/dev/dri/renderD128', '/dev/dri/renderD128']])
        binds = [args[i+1:i+3] for i, arg in enumerate(args) if arg == '--ro-bind']
        self.assertIn(['/dev/dri/card1', '/dev/dri/card1'], binds)
        self.assertIn(['/owned/snapshot/sys', '/sys'], binds)
        self.assertNotIn(['/sys', '/sys'], binds)
        for name in ('WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'DBUS_SESSION_BUS_ADDRESS', 'DISPLAY'):
            self.assertNotIn(name, args)

    def test_scope_has_aggregate_limits(self):
        args = lab.scope_command(['/owned/probe'], 'rasklad-mapping-test.scope')
        for prop in ('MemoryMax=1536M', 'MemorySwapMax=0', 'TasksMax=96',
                     'CPUQuota=100%', 'RuntimeMaxSec=27s'):
            self.assertIn('--property=' + prop, args)
        with self.assertRaises(lab.base.Refused):
            lab.scope_command(['/owned/probe'], 'slack.service')

    def test_failure_cannot_become_verified(self):
        good = dict(ok=True, mappingMode=True, mappingVerified=True, observerCleanup=True)
        self.assertEqual(lab.verified_report(good, 0), good)
        with self.assertRaises(lab.base.Refused):
            lab.verified_report(good, 1)
        for key in good:
            for value in (False, 1, None):
                with self.subTest(key=key, value=value), self.assertRaises(lab.base.Refused):
                    lab.verified_report(dict(good, **{key: value}), 0)

    def test_dead_server_socket_is_not_ready(self):
        server = SimpleNamespace(poll=lambda: 1)
        directory = SimpleNamespace(glob=lambda _: [SimpleNamespace(is_socket=lambda: True)])
        with self.assertRaises(lab.base.Refused):
            lab.wait_socket(server, directory, 1)


if __name__ == '__main__':
    unittest.main()
