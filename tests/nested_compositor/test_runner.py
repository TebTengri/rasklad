"""Mocked checks for tools/nested_compositor_lab.py. No compositor, bwrap or display is started."""
import importlib.util, os, signal, stat, subprocess, tempfile, unittest
from pathlib import Path
from unittest import mock

spec = importlib.util.spec_from_file_location(
    'lab', Path(__file__).resolve().parents[2] / 'tools/nested_compositor_lab.py')
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)

HOST_ENV = {'WAYLAND_DISPLAY': 'wayland-1', 'DISPLAY': ':0', 'HYPRLAND_INSTANCE_SIGNATURE': 'sig',
            'DBUS_SESSION_BUS_ADDRESS': 'unix:path=/run/user/1000/bus', 'XDG_RUNTIME_DIR': '/run/user/1000'}


def fake_stat(mode=stat.S_IFREG | 0o755, uid=0):
    return os.stat_result((mode, 0, 0, 1, uid, 0, 0, 0, 0, 0))


def good_lstat(path):
    return fake_stat()


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class FakeProc:
    pid = 4242

    def __init__(self):
        self.returncode, self.waited = None, False

    def wait(self, timeout=None):
        self.waited, self.returncode = True, -9
        return self.returncode


class PlanTests(unittest.TestCase):
    def test_required_namespace_and_device_restrictions(self):
        argv = lab.plan_argv()
        self.assertEqual(argv[0], lab.BWRAP)
        for flag in ('--unshare-all', '--die-with-parent', '--new-session', '--clearenv'):
            self.assertIn(flag, argv)
        self.assertEqual(argv[argv.index('--cap-drop') + 1], 'ALL')
        self.assertEqual(argv[argv.index('--dev') + 1], '/dev')
        self.assertEqual(argv[argv.index('--proc') + 1], '/proc')
        for tmp in ('/tmp', '/run', '/home'):
            self.assertIn(['--tmpfs', tmp], [argv[i:i + 2] for i in range(len(argv))])
        for forbidden in ('--share-net', '--dev-bind', '--dev-bind-try', '--bind-try', '--cap-add'):
            self.assertNotIn(forbidden, argv)
        self.assertEqual(argv[-4:], ['--', lab.HYPRLAND, '--config', lab.SANDBOX_CONFIG])

    def test_only_allowlisted_mounts(self):
        argv = lab.plan_argv()
        binds = [(argv[i], argv[i + 1], argv[i + 2]) for i, a in enumerate(argv)
                 if a in ('--bind', '--ro-bind', '--ro-bind-try')]
        self.assertEqual({src for _, src, _ in binds},
                         {'/usr', '/etc/ld.so.cache', '/etc/fonts', lab.RUNTIME_TOKEN, lab.CONFIG_TOKEN})
        self.assertEqual([b for b in binds if b[0] == '--bind'], [('--bind', lab.RUNTIME_TOKEN, lab.SANDBOX_RUNTIME)])
        joined = ' '.join(argv)
        for host in ('/sys', '/dev/dri', '/dev/input', '/dev/uinput', '/run/user', str(lab.ROOT), str(Path.home())):
            self.assertNotIn(host, joined)
        self.assertNotIn(('--ro-bind', '/', '/'), binds)

    def test_fixed_env_ignores_host_display_env(self):
        with mock.patch.dict(os.environ, HOST_ENV):
            argv = lab.plan_argv()
        names = [argv[i + 1] for i, a in enumerate(argv) if a == '--setenv']
        self.assertEqual(names, [k for k, _ in lab.SANDBOX_ENV])
        for value in HOST_ENV.values():
            self.assertNotIn(value, argv)
        self.assertFalse(set(names) & {'WAYLAND_DISPLAY', 'DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'})
        self.assertFalse([n for n in names if n.startswith('HYPRLAND_INSTANCE')])

    def test_generated_config_is_standalone(self):
        text = lab.CONFIG_TEXT
        self.assertIn('enabled = false', text)
        for line in text.splitlines():
            if not line.lstrip().startswith('#'):
                for word in ('source', 'exec', 'bind', 'env', 'plugin', '/'):
                    self.assertNotIn(word, line)


class CheckTests(unittest.TestCase):
    def test_check_starts_nothing_and_is_not_tested(self):
        with mock.patch.object(subprocess, 'Popen', side_effect=AssertionError('started')), \
             mock.patch.object(lab, 'prepare_base', side_effect=AssertionError('wrote')):
            report, code = lab.check_report(lstat=good_lstat, euid=lambda: 1000)
        self.assertEqual((code, report['status'], report['tested']), (lab.EXIT_PLAN, 'plan_only', False))
        self.assertFalse(report['sandboxSafetyProven'])
        self.assertNotIn(str(lab.ROOT), str(report))

    def test_executable_states(self):
        cases = {fake_stat(stat.S_IFLNK | 0o777): 'symlink', fake_stat(uid=1000): 'not_root_owned',
                 fake_stat(stat.S_IFREG | 0o4755): 'setid', fake_stat(stat.S_IFREG | 0o775): 'writable_by_others',
                 fake_stat(stat.S_IFDIR | 0o755): 'not_regular', fake_stat(stat.S_IFREG | 0o644): 'not_executable'}
        for st, expected in cases.items():
            self.assertEqual(lab.inspect_executable('/x', lambda p, st=st: st), expected)

        def missing(path):
            raise FileNotFoundError(path)
        report, code = lab.check_report(lstat=missing, euid=lambda: 1000)
        self.assertEqual((code, report['status']), (lab.EXIT_BLOCKED, 'blocked'))


class SmokeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.clock = Clock()
        self.signals = []

    def tearDown(self):
        self.tmp.cleanup()

    def smoke(self, popen, exited=lambda p: False, socket_present=lambda r: False, **kw):
        return lab.run_smoke(root=self.root, popen=popen, clock=self.clock, sleep=self.clock.sleep,
                             killpg=lambda pid, sig: self.signals.append((pid, sig)), exited=exited,
                             socket_present=socket_present, lstat=kw.get('lstat', good_lstat),
                             euid=kw.get('euid', lambda: 1000))

    def test_blocked_without_runtime_starts_and_writes_nothing(self):
        def missing(path):
            raise FileNotFoundError(path)
        popen = mock.Mock(side_effect=AssertionError('no fallback launch'))
        for kw in ({'lstat': missing}, {'euid': lambda: 0}):
            report, code = self.smoke(popen, **kw)
            self.assertEqual((code, report['status'], report['tested']), (lab.EXIT_BLOCKED, 'blocked', False))
        popen.assert_not_called()
        self.assertFalse((self.root / 'build').exists())

    def test_symlinked_build_dir_refused_before_mutation(self):
        target = self.root / 'elsewhere'
        target.mkdir()
        (self.root / 'build').symlink_to(target)
        popen = mock.Mock()
        report, code = self.smoke(popen)
        self.assertEqual((code, report['reasons']), (lab.EXIT_BLOCKED, ['symlink:build']))
        self.assertEqual(list(target.iterdir()), [])
        popen.assert_not_called()

    def test_shared_writable_build_dir_refused(self):
        build = self.root / 'build'
        build.mkdir(mode=0o700)
        build.chmod(0o777)
        popen = mock.Mock()
        report, code = self.smoke(popen)
        self.assertEqual((code, report['reasons']), (lab.EXIT_BLOCKED, ['writable_directory:build']))
        popen.assert_not_called()
        self.assertEqual(list(build.iterdir()), [])

    def test_symlinked_config_refused_and_target_untouched(self):
        base = self.root / 'build' / 'nested_compositor'
        base.mkdir(parents=True)
        victim = self.root / 'victim.txt'
        victim.write_text('keep')
        (base / 'hyprland.conf').symlink_to(victim)
        with self.assertRaises(lab.Refused):
            lab.write_config(base)
        self.assertEqual(victim.read_text(), 'keep')

    def test_hardlinked_config_refused_without_truncating_target(self):
        base = self.root / 'config'
        base.mkdir()
        victim = self.root / 'victim.txt'
        victim.write_text('keep')
        os.link(victim, base / 'hyprland.conf')
        with self.assertRaises(lab.Refused):
            lab.write_config(base)
        self.assertEqual(victim.read_text(), 'keep')

    def test_timeout_kills_and_reaps_own_group(self):
        proc = FakeProc()
        popen = mock.Mock(return_value=proc)
        report, code = self.smoke(popen)
        self.assertEqual((code, report['status']), (lab.EXIT_TIMEOUT, 'timeout'))
        self.assertEqual(self.signals, [(4242, signal.SIGTERM), (4242, signal.SIGKILL)])
        self.assertTrue(proc.waited)
        self.assertGreaterEqual(self.clock.now, lab.TIMEOUT_S)
        self.assertLess(self.clock.now, lab.TIMEOUT_S + 3 * lab.GRACE_S)
        base = self.root / 'build' / 'nested_compositor'
        self.assertEqual(stat.S_IMODE(base.stat().st_mode), 0o700)
        self.assertEqual(list(base.iterdir()), [])  # complete private run dir removed
        popen.assert_called_once()

    def test_launch_isolation_arguments(self):
        popen = mock.Mock(return_value=FakeProc())
        with mock.patch.dict(os.environ, HOST_ENV):
            self.smoke(popen, socket_present=lambda r: True)
        argv, kwargs = popen.call_args
        argv = argv[0]
        self.assertEqual(argv[0], lab.BWRAP)
        self.assertEqual(kwargs['env'], {'PATH': '/usr/bin'})
        self.assertTrue(kwargs['start_new_session'] and kwargs['close_fds'])
        self.assertIs(kwargs['preexec_fn'], lab._child_limits)
        self.assertEqual(kwargs['stdout'], subprocess.DEVNULL)
        self.assertNotIn(lab.RUNTIME_TOKEN, argv)
        runtime = argv[argv.index('--bind') + 1]
        self.assertTrue(runtime.startswith(str(self.root / 'build' / 'nested_compositor' / 'run-')))
        self.assertEqual(argv[argv.index('--info-fd') + 1], str(kwargs['pass_fds'][0]))

    def test_ready_requires_started_sandbox_report_has_no_payload(self):
        def popen(argv, **kwargs):
            os.write(kwargs['pass_fds'][0], b'{"child-pid": 7}')
            return FakeProc()
        report, code = self.smoke(popen, socket_present=lambda r: True)
        self.assertEqual((code, report['status'], report['sandboxStarted']), (lab.EXIT_READY, 'ready', True))
        self.assertFalse(report['sandboxSafetyProven'])
        self.assertEqual(set(report), {'mode', 'tested', 'executables', 'sandboxSafetyProven', 'status',
                                       'sandboxStarted', 'exitCode', 'elapsedMs'})
        self.assertNotIn(self.tmp.name, str(report))

    def test_early_exit_without_sandbox_is_sandbox_failed(self):
        report, code = self.smoke(mock.Mock(return_value=FakeProc()), exited=lambda p: True)
        self.assertEqual((code, report['status'], report['sandboxStarted']),
                         (lab.EXIT_SANDBOX_FAILED, 'sandbox_failed', False))

    def test_socket_without_sandbox_info_is_not_ready(self):
        report, code = self.smoke(mock.Mock(return_value=FakeProc()), socket_present=lambda r: True)
        self.assertEqual((code, report['status']), (lab.EXIT_SANDBOX_FAILED, 'sandbox_failed'))

    def test_dead_compositor_with_stale_socket_is_not_ready(self):
        def popen(argv, **kwargs):
            os.write(kwargs['pass_fds'][0], b'{"child-pid": 7}')
            return FakeProc()
        report, code = self.smoke(popen, exited=lambda p: True, socket_present=lambda r: True)
        self.assertEqual((code, report['status']), (lab.EXIT_EXITED, 'exited'))

    def test_unreaped_process_is_cleanup_failure_and_keeps_private_run(self):
        proc = FakeProc()
        proc.wait = mock.Mock(side_effect=subprocess.TimeoutExpired('bwrap', 1))
        report, code = self.smoke(mock.Mock(return_value=proc))
        self.assertEqual((code, report['status']), (lab.EXIT_CLEANUP_FAILED, 'cleanup_failed'))
        self.assertEqual(len(list((self.root / 'build' / 'nested_compositor').iterdir())), 1)

    def test_pipe_setup_failure_removes_private_run(self):
        popen = mock.Mock()
        with mock.patch.object(lab.os, 'pipe', side_effect=OSError('fd limit')):
            report, code = self.smoke(popen)
        self.assertEqual((code, report['status']), (lab.EXIT_SANDBOX_FAILED, 'launch_failed'))
        popen.assert_not_called()
        self.assertEqual(list((self.root / 'build' / 'nested_compositor').iterdir()), [])

    def test_failed_directory_cleanup_is_not_reported_as_success(self):
        with mock.patch.object(lab.shutil, 'rmtree', side_effect=PermissionError('cleanup')):
            report, code = self.smoke(mock.Mock(return_value=FakeProc()), socket_present=lambda r: True)
        self.assertEqual((code, report['status']), (lab.EXIT_CLEANUP_FAILED, 'cleanup_failed'))

    def test_child_setup_failure_has_no_fallback(self):
        popen = mock.Mock(side_effect=subprocess.SubprocessError('preexec failed'))
        report, code = self.smoke(popen)
        self.assertEqual((code, report['status']), (lab.EXIT_SANDBOX_FAILED, 'launch_failed'))
        popen.assert_called_once()

    def test_launch_oserror_has_no_fallback(self):
        popen = mock.Mock(side_effect=FileNotFoundError('bwrap'))
        report, code = self.smoke(popen)
        self.assertEqual((code, report['status'], report['tested']),
                         (lab.EXIT_SANDBOX_FAILED, 'launch_failed', False))
        popen.assert_called_once()
        self.assertEqual(self.signals, [])


if __name__ == '__main__':
    unittest.main()
