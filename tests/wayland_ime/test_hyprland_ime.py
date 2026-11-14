# SPDX-License-Identifier: MIT
import importlib.util
import os
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT/'tools'/filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


lab = load('hypr_ime', 'hyprland_ime_lab.py')
clients = load('clients', 'native_ime_clients.py')


class ExactHyprlandGuards(unittest.TestCase):
    def test_default_does_not_connect_or_load_profile(self):
        with patch('sys.argv', ['lab']), patch.object(lab, 'run') as run, patch.object(lab.socket, 'socket') as sock:
            self.assertEqual(lab.main(), 0)
            run.assert_not_called()
            sock.assert_not_called()

    def test_explicit_profile_required_before_any_endpoint_or_process(self):
        with patch.object(lab.os, 'geteuid', return_value=1000), patch.object(lab.probe, 'endpoint') as endpoint:
            with self.assertRaisesRegex(lab.base.Refused, 'explicit_input_root_required'):
                lab.run(None)
            endpoint.assert_not_called()

    def test_host_cannot_enter_inner(self):
        with patch.object(lab, 'INSIDE', False), patch.object(lab.subprocess, 'Popen') as popen:
            with self.assertRaisesRegex(lab.base.Refused, 'private_hyprland_ime_required'):
                lab.inside()
            popen.assert_not_called()

    def test_full_plan_keeps_only_render_device_and_synthetic_sysfs(self):
        directory = Path('/private')
        plan = {'sysfs': '/private/gpu-metadata/sys', 'statOnlyPrimary': '/dev/dri/card0',
                'primaryBindMustBeNodevReadOnly': True, 'hostSysfsBind': False}
        paths = lab.chrome.files()
        paths.pop('keys')
        paths.pop('xvfb')
        snapshots = {'/lab/addons/raskladlab.so': 9, '/lab/rasklad-wayland-keys': 10}
        args = lab.sandbox_args(directory, 8, plan, paths, {}, snapshots)
        self.assertEqual(args.count('--dev-bind'), 1)
        self.assertEqual(args[args.index('--dev-bind')+1:args.index('--dev-bind')+3], ['/dev/dri/renderD128']*2)
        self.assertIn('--unshare-all', args)
        self.assertIn('--clearenv', args)
        for absent in ('DISPLAY', 'WAYLAND_SOCKET', 'DBUS_SESSION_BUS_ADDRESS', '--share-net', '--no-sandbox'):
            self.assertNotIn(absent, args)
        self.assertFalse(any('/run/user/' in item for item in args))
        self.assertEqual(args.count('--ro-bind-data'), 2)
        binds = [args[i+1:i+3] for i, value in enumerate(args) if value == '--ro-bind']
        self.assertIn(['/private/gpu-metadata/sys', '/sys'], binds)
        self.assertFalse(any(source == '/sys' or source == '/home' or source == str(ROOT) for source, _ in binds))

    def test_focus_suite_requires_explicit_option_without_new_devices(self):
        directory = Path('/private')
        plan = {'sysfs': '/private/gpu-metadata/sys', 'statOnlyPrimary': '/dev/dri/card0',
                'primaryBindMustBeNodevReadOnly': True, 'hostSysfsBind': False}
        paths = lab.chrome.files()
        paths.pop('keys'); paths.pop('xvfb')
        snapshots = {'/lab/addons/raskladlab.so': 9, '/lab/rasklad-wayland-keys': 10}
        default = lab.sandbox_args(directory, 8, plan, paths, {}, snapshots)
        selected = lab.sandbox_args(directory, 8, plan, paths, {}, snapshots, focus_stress=True)
        self.assertNotIn('RASKLAD_FOCUS_STRESS_LAB', default)
        self.assertIn('RASKLAD_FOCUS_STRESS_LAB', selected)
        self.assertIn('/lab/adversarial-focus.mjs', selected)
        self.assertEqual(selected.count('--dev-bind'), default.count('--dev-bind'))

    def test_space_telemetry_refuses_without_focus_before_endpoint(self):
        with patch.object(lab.probe, 'endpoint') as endpoint:
            with self.assertRaisesRegex(lab.base.Refused, 'space_telemetry_requires_focus_suite'):
                lab.run('/unused', space_telemetry=True)
            endpoint.assert_not_called()

    def test_private_bus_refuses_unreleased_parent_fd(self):
        for marker in ('RASKLAD_PARENT_FD', 'WAYLAND_SOCKET'):
            with patch.dict(os.environ, {marker: '8'}, clear=True), patch.object(lab.subprocess, 'Popen') as popen:
                with self.assertRaisesRegex(lab.base.Refused, 'parent_capability_not_released'):
                    lab.start_bus([])
                popen.assert_not_called()

    def test_clients_refuse_parent_connection_marker(self):
        env = {'RASKLAD_NATIVE_WAYLAND_LAB': '1', 'HOME': '/home/lab', 'XDG_RUNTIME_DIR': '/run/lab',
               'WAYLAND_DISPLAY': 'wayland-lab', 'RASKLAD_PARENT_FD': '8'}
        with patch.dict(os.environ, env, clear=True), patch.object(clients.subprocess, 'Popen') as popen:
            with self.assertRaisesRegex(lab.base.Refused, 'private_native_clients_required'):
                clients.run(lab.base, [])
            popen.assert_not_called()

    def test_dbus_transport_failure_is_not_frontend_absence(self):
        self.assertFalse(clients.input_frontend_absent(1, b'', b'Connection refused'))
        self.assertFalse(clients.input_frontend_absent(1, b'', b'Timeout'))
        self.assertFalse(clients.input_frontend_absent(0, b'org.fcitx.Fcitx.InputMethod1', b''))
        self.assertTrue(clients.input_frontend_absent(1, b'', b'org.freedesktop.DBus.Error.UnknownObject'))
        self.assertTrue(clients.input_frontend_absent(0, b'<node/>', b''))


if __name__ == '__main__':
    unittest.main()
