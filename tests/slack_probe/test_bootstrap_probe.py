# SPDX-License-Identifier: MIT
import importlib.util, json, os, socket, subprocess, sys, tempfile, time, unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


probe = load('slack_bootstrap_probe', ROOT / 'tools/slack_bootstrap_probe.py')
bridge = load('slack_https_bridge', ROOT / 'tools/slack_https_bridge.py')


class PlanAndScan(unittest.TestCase):
    def test_default_is_inert_plan(self):
        out = subprocess.run([sys.executable, '-I', str(ROOT / 'tools/slack_bootstrap_probe.py')],
                             capture_output=True, timeout=10, env={'PATH': '/usr/bin'})
        report = json.loads(out.stdout)
        self.assertEqual(out.returncode, 0)
        self.assertEqual(report['mode'], 'plan')
        self.assertEqual(report['input'], 'none')
        self.assertFalse(report['textRead'])

    def test_markers_across_chunk_boundary_and_first_seen(self):
        clock = iter([0.0, 1.0, 2.0, 3.0, 4.0])
        flags, first = probe.scan([b'xdg_surface#9.get_top', b'level(new id xdg_toplevel#10)',
                                   b'wl_surface#3.attach(wl_buffer#12, 0, 0)'], now=lambda: next(clock), started=0.0)
        self.assertTrue(flags['toplevelCreated'])
        self.assertTrue(flags['bufferAttached'])
        self.assertFalse(flags['fatal'])
        self.assertEqual(set(first), {'toplevelCreated', 'bufferAttached', 'attachCount'})
        self.assertEqual(first['attachCount'], 1)

    def test_scan_keeps_no_text(self):
        flags, first = probe.scan([b'xdg_toplevel#10.set_title("zqxfictionalws")'])
        self.assertNotIn("zqxfictionalws", json.dumps([flags, first]))


class Modes(unittest.TestCase):
    def test_chromium_control_only_in_dry_run(self):
        with self.assertRaisesRegex(RuntimeError, 'control-dry-only'):
            probe.run(dry=False, control=True)


class Reap(unittest.TestCase):
    def test_never_kills_processes_in_own_mount_namespace(self):
        child = subprocess.Popen(['/usr/bin/sleep', '5'])
        try:
            left = probe.reap_sandboxed([child.pid])
            self.assertIn(child.pid, left)
            self.assertIsNone(child.poll())
        finally:
            child.kill(); child.wait()


class Refusals(unittest.TestCase):
    def setUp(self):
        self.home = Path(tempfile.mkdtemp())
        (self.home / '.config/Slack').mkdir(parents=True, mode=0o700)
        os.chmod(self.home / '.config', 0o700)

    def test_accepts_private_profile(self):
        self.assertEqual(probe.profile_dir(self.home), self.home / '.config/Slack')

    def test_refuses_singleton_lock_even_dangling(self):
        os.symlink('otherhost-123', self.home / '.config/Slack/SingletonLock')
        with self.assertRaisesRegex(ValueError, 'singleton-lock-present'):
            probe.profile_dir(self.home)

    def test_refuses_group_writable_profile(self):
        os.chmod(self.home / '.config/Slack', 0o770)
        with self.assertRaisesRegex(ValueError, 'profile-ownership'):
            probe.profile_dir(self.home)

    def test_refuses_symlinked_profile(self):
        other = Path(tempfile.mkdtemp())
        (self.home / '.config/Slack').rmdir()
        os.symlink(other, self.home / '.config/Slack')
        with self.assertRaisesRegex(ValueError, 'profile-symlink-parent'):
            probe.profile_dir(self.home)

    def test_host_bus_address_shape(self):
        self.assertEqual(probe.host_bus_address({'DBUS_SESSION_BUS_ADDRESS': 'unix:path=/run/user/1000/bus'}, 1000),
                         'unix:path=/run/user/1000/bus')
        for bad in ('', 'unix:path=/tmp/bus', 'unix:path=/run/user/1000/bus;tcp:host=x', 'unix:abstract=/run/user/1000/bus'):
            with self.assertRaisesRegex(ValueError, 'host-bus-address'):
                probe.host_bus_address({'DBUS_SESSION_BUS_ADDRESS': bad}, 1000)

    def test_proxy_talks_only_to_secret_service(self):
        command = probe.proxy_command('unix:path=/run/user/1000/bus', '/x/bus')
        self.assertIn('--filter', command)
        talks = [a for a in command if a.startswith(('--talk=', '--own=', '--see=', '--call=', '--broadcast='))]
        self.assertEqual(talks, ['--talk=org.freedesktop.secrets'])


class SandboxShape(unittest.TestCase):
    def test_only_profile_from_host_home_and_no_display(self):
        args = probe.sandbox_args(Path('/home/u'), Path('/home/u/.config/Slack'), 1000, 'u', Path('/w'), {'sway': 5})
        self.assertIn('--unshare-all', args)
        self.assertNotIn('--share-net', args)
        binds = [(args[i], args[i + 1], args[i + 2]) for i, a in enumerate(args) if a in ('--bind', '--ro-bind', '--ro-bind-try')]
        host_home = [b for b in binds if b[2].startswith('/home/')]
        self.assertEqual(host_home, [('--bind', '/home/u/.config/Slack', '/home/u/.config/Slack')])
        envs = {args[i + 1]: args[i + 2] for i, a in enumerate(args) if a == '--setenv'}
        for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET'):
            self.assertNotIn(key, envs)
        self.assertEqual(envs['DBUS_SESSION_BUS_ADDRESS'], 'unix:path=/run/user/1000/bus')
        self.assertIn(('--bind', '/w/bus', '/run/user/1000/bus'), binds)
        self.assertEqual(envs['WLR_BACKENDS'], 'headless')

    def test_private_context_rejects_host_display(self):
        env = {'RASKLAD_SLACK_BOOTSTRAP_LAB': '1', 'XDG_RUNTIME_DIR': '/run/user/7', 'DBUS_SESSION_BUS_ADDRESS': 'unix:path=/run/user/7/bus'}
        self.assertTrue(probe.private_context(env, True, 7))
        self.assertFalse(probe.private_context(env, False, 7))
        self.assertFalse(probe.private_context(dict(env, WAYLAND_DISPLAY='wayland-1'), True, 7))
        self.assertFalse(probe.private_context(dict(env, DISPLAY=':0'), True, 7))


class BridgeRejectedDomains(unittest.TestCase):
    def test_suffix_hides_subdomains(self):
        self.assertEqual(bridge.rejected_suffix(b'CONNECT team-name.example.org:443 HTTP/1.1'), 'other')
        self.assertEqual(bridge.rejected_suffix(b'CONNECT a.b.slack-files.com:443 HTTP/1.1'), 'slack-files.com')
        self.assertEqual(bridge.rejected_suffix(b'CONNECT \xff:443 HTTP/1.1'), 'malformed')
        self.assertEqual(bridge.rejected_suffix(b'GARBAGE'), 'malformed')

    def test_error_kinds_are_fixed_categories(self):
        import socket as so, subprocess as sp
        self.assertEqual(bridge.error_kind(so.timeout(), 'hello'), 'hello:timeout')
        self.assertEqual(bridge.error_kind(OSError('dns'), 'resolve'), 'resolve:dns')
        self.assertEqual(bridge.error_kind(OSError('connect'), 'connect'), 'connect:connect')
        self.assertEqual(bridge.error_kind(ConnectionResetError(), 'pump'), 'pump:reset')
        self.assertEqual(bridge.error_kind(sp.TimeoutExpired('x', 1), 'resolve'), 'resolve:dns-timeout')
        self.assertEqual(bridge.error_kind(RuntimeError('secret.example'), 'pump'), 'pump:other')

    def test_accept_hello_is_offline_only(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(ValueError, 'accept-hello-offline-only'):
                bridge.Broker(Path(d) / 's', online=True, accept_hello=True)

    def test_bounds_validated(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(ValueError, 'broker-bounds'):
                bridge.Broker(Path(d) / 's', seconds=0)

    def test_rejected_domain_counted_and_refused(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'https.sock'
            broker = bridge.Broker(path, online=False, seconds=5)
            try:
                for host in (b'a.example.org', b'b.example.org', b'slack.com'):
                    with socket.socket(socket.AF_UNIX) as client:
                        client.connect(str(path))
                        client.sendall(b'CONNECT ' + host + b':443 HTTP/1.1\r\n\r\n')
                        self.assertTrue(client.recv(64).startswith(b'HTTP/1.1 403'))
                time.sleep(.2)
            finally:
                stats = broker.close()
            self.assertEqual(stats['rejectedDomains'], {'other': 2})
            self.assertEqual(stats['opened'], 0)
            self.assertTrue(stats['cleanupComplete'])


if __name__ == '__main__':
    unittest.main()
