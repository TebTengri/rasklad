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
    def test_debugger_disables_init_autoload_network_and_frame_arguments(self):
        args = probe.debugger_command()
        for required in ('-nx', '-nh', 'set auto-load off', 'set debuginfod enabled off',
                         'set startup-with-shell off', 'set print frame-arguments none'):
            self.assertIn(required, args)
        structured = next(value for value in args if value.startswith('python\n'))
        self.assertIn('gdb.newest_frame()', structured)
        self.assertNotIn('read_var', structured)
        self.assertNotIn('frame_args', structured)
        self.assertNotIn('full', args)

    def test_debugger_report_omits_arguments_and_non_system_paths(self):
        text = 'Program received signal SIGSEGV\n#0  vendor_call (secret="never") from /usr/lib/dri/example_dri.so\n#1  ?? () from /private/home/plugin.so\n'
        summary = probe.parse_debugger_summary(text)
        self.assertEqual(summary['debuggerSignal'], 'SIGSEGV')
        self.assertEqual(summary['debuggerFrames'], [{'symbol': 'vendor_call', 'library': 'example_dri.so'},
                                                     {'symbol': '??', 'library': None}])
        self.assertNotIn('never', str(summary))
        self.assertNotIn('/private', str(summary))

    def test_structured_frames_preserve_cpp_names_without_arguments(self):
        text = 'Program received signal SIGSEGV\nRASKLAD_FRAME:{"symbol":"some_function(std::string const&)","library":"libexample.so"}\n#0 other (secret="no")\n'
        self.assertEqual(probe.parse_debugger_summary(text)['debuggerFrames'],
                         [{'symbol': 'some_function(std::string const&)', 'library': 'libexample.so'}])

    def test_stat_only_card_requires_readonly_and_nodev(self):
        row = '31 20 0:5 /null /dev/dri/card0 ro,nosuid,nodev - devtmpfs udev rw'
        probe.require_stat_only_mounts(row, ['/dev/dri/card0'])
        for bad in (row.replace(',nodev', ''), row.replace('ro,nosuid', 'rw,nosuid'), ''):
            with self.assertRaisesRegex(ValueError, 'metadata_node_not_stat_only'):
                probe.require_stat_only_mounts(bad, ['/dev/dri/card0'])

    def test_stat_only_guard_rejects_unexpected_or_shadowed_node(self):
        with self.assertRaises(ValueError):
            probe.require_stat_only_mounts('', ['/dev/dri/renderD128'])
        row = '31 20 0:5 /null /dev/dri/card0 ro,nodev - devtmpfs udev rw'
        with self.assertRaises(ValueError):
            probe.require_stat_only_mounts(row+'\n'+row, ['/dev/dri/card0'])

    def test_specific_error_takes_priority_over_generic_abort(self):
        self.assertEqual(probe.classify_startup(b'drmGetDeviceFromDevId failed\nCBackend::create() failed'),
                         'drm_device_discovery_failed')

    def test_missing_evidence_is_not_guessed(self):
        self.assertEqual(probe.classify_startup(b''), 'unclassified_startup_failure')

    def test_diagnostic_does_not_return_raw_log_text(self):
        self.assertEqual(probe.classify_startup(b'private value\nwl_display_connect failed'),
                         'parent_wayland_connect_failed')

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

    def test_metadata_mounts_are_reconstructed_and_primary_is_nodev(self):
        directory = Path('/fixture')
        plan = {'sysfs': '/fixture/gpu-metadata/sys', 'statOnlyPrimary': '/dev/dri/card1',
                'primaryBindMustBeNodevReadOnly': True, 'hostSysfsBind': False}
        args = probe.sandbox_args(directory, 9, plan)
        binds = [args[i+1:i+3] for i, value in enumerate(args) if value == '--ro-bind']
        self.assertIn(['/fixture/gpu-metadata/sys', '/sys'], binds)
        self.assertIn(['/dev/dri/card1', '/dev/dri/card1'], binds)
        self.assertFalse(any(source == '/sys' or source.startswith('/sys/') for source, _ in binds))
        self.assertEqual(args.count('--dev-bind'), 1)
        for bad in ({**plan, 'sysfs': '/sys'}, {**plan, 'statOnlyPrimary': '/dev/dri/renderD128'},
                    {**plan, 'hostSysfsBind': True}):
            with self.assertRaises(ValueError):
                probe.sandbox_args(directory, 9, bad)

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
