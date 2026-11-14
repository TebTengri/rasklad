# SPDX-License-Identifier: MIT
import importlib.util
import os
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('hypr_probe', ROOT/'tools/hyprland_nested_probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class ProbeBoundaries(unittest.TestCase):
    def test_default_does_not_connect_or_run(self):
        with patch('sys.argv', ['probe']), patch.object(probe, 'run') as run, patch.object(probe.socket, 'socket') as sock:
            self.assertEqual(probe.main(), 0)
            run.assert_not_called()
            sock.assert_not_called()

    def test_only_render_node_and_no_parent_socket_path(self):
        args = probe.sandbox_args(Path('/fixture'), 9)
        self.assertIn('--unshare-all', args)
        self.assertIn('--clearenv', args)
        self.assertEqual(args.count('--dev-bind'), 1)
        index = args.index('--dev-bind')
        self.assertEqual(args[index+1:index+3], ['/dev/dri/renderD128']*2)
        self.assertFalse(any('/run/user/' in item for item in args))
        for forbidden in ('DISPLAY', 'DBUS_SESSION_BUS_ADDRESS', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', '--share-net'):
            self.assertNotIn(forbidden, args)
        self.assertNotIn('fcitx5', args)
        self.assertNotIn('slack', args)

    def test_host_cannot_call_inner(self):
        with patch.object(probe, 'INSIDE', False), patch.object(probe.subprocess, 'Popen') as popen:
            with self.assertRaisesRegex(ValueError, 'private_probe_required'):
                probe.inside()
            popen.assert_not_called()

    def test_unexpected_display_or_inherited_fd_rejected(self):
        for env in ({'XDG_RUNTIME_DIR': '/tmp', 'WAYLAND_DISPLAY': '/host/socket'},
                    {'XDG_RUNTIME_DIR': '/tmp', 'WAYLAND_DISPLAY': 'wayland-1', 'WAYLAND_SOCKET': '7'}):
            with self.assertRaises(ValueError):
                probe.endpoint(env)


if __name__ == '__main__':
    unittest.main()
