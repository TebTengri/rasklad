import ast
import importlib.util
import inspect
import json
import os
import socket
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]


def load(name, rel):
    spec = importlib.util.spec_from_file_location(name, ROOT / rel)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


launch = load('slack_launch_lab_test', 'tools/slack_launch_lab.py')
clients = load('native_clients_launch_test', 'tools/native_ime_clients.py')


class Base:
    class Refused(Exception):
        pass


PRE = {'user': 'owner', 'home': Path('/home/owner'), 'profile': Path('/home/owner/.config/Slack'), 'lang': 'C.UTF-8'}


class Selection(unittest.TestCase):
    def setUp(self):
        self.hypr = load('hyprland_ime_lab_launch_test', 'tools/hyprland_ime_lab.py')
        self.alone = (False, False, False, False, False, False, 1, False, False, 'system')

    def test_alone_only(self):
        self.hypr.launch_selection(False, True, True, True, True, True, True, True, True, 3, True, True, 'keep-grab')
        self.hypr.launch_selection(True, False, False, *self.alone)
        self.hypr.launch_selection(dict(PRE), False, False, *self.alone)
        for bad in ((True, True, False, *self.alone), (True, False, True, *self.alone),
                    (True, False, False, True, *self.alone[1:]), (True, False, False, *self.alone[:-1], 'keep-grab'),
                    (1, False, False, *self.alone)):
            with self.assertRaises(self.hypr.base.Refused):
                self.hypr.launch_selection(*bad)

    def test_cli_refusals(self):
        for argv, reason in ((['--slack-launch'], 'slack_launch_requires_run_approved'),
                             (['--run-approved', '--slack-launch', '--slack-supervised'], 'slack_launch_incompatible_options')):
            out = subprocess.run([sys.executable, '-I', str(ROOT / 'tools/hyprland_ime_lab.py'), *argv],
                                 capture_output=True, text=True, timeout=20)
            self.assertIn(reason, out.stdout)

    def test_run_forwards_preflight_dict(self):
        tree = ast.parse(inspect.getsource(self.hypr.run))
        calls = [n for n in ast.walk(tree) if isinstance(n, ast.Call) and getattr(n.func, 'id', None) == 'sandbox_args']
        self.assertEqual(len(calls), 1)
        keywords = {k.arg: ast.unparse(k.value) for k in calls[0].keywords}
        self.assertEqual(keywords['slack_launch'], 'pre if launch else False')

    def test_sandbox_args_refuses_bare_true(self):
        with self.assertRaises(self.hypr.base.Refused) as caught:
            self.hypr.sandbox_args(Path('/nonexistent'), 5, None, {}, {}, {}, slack_launch=True)
        self.assertEqual(str(caught.exception), 'slack_launch_preflight_required')


class SandboxShape(unittest.TestCase):
    def test_profile_bound_in_place_and_only_proxy_bus(self):
        extra = launch.sandbox_extra(Path('/w'), PRE)
        pairs = [tuple(extra[i:i + 3]) for i in range(len(extra) - 2)]
        self.assertIn(('--bind', '/home/owner/.config/Slack', '/home/owner/.config/Slack'), pairs)
        self.assertIn(('--bind', '/w/bus', launch.SLACK_BUS), pairs)
        self.assertIn(('--bind', '/w/https.sock', '/lab/https.sock'), pairs)
        binds = [extra[i + 1] for i, a in enumerate(extra) if a in ('--bind', '--ro-bind', '--ro-bind-try')]
        self.assertFalse(any(b.startswith('/run/user') for b in binds))
        self.assertNotIn('RASKLAD_SLACK_SUPERVISED_LAB', extra)
        env = {extra[i + 1]: extra[i + 2] for i, a in enumerate(extra) if a == '--setenv'}
        self.assertEqual(env['RASKLAD_SLACK_LAUNCH_LAB'], '1')
        self.assertEqual(env['RASKLAD_SLACK_HOME'], '/home/owner')


class ChildEnv(unittest.TestCase):
    def test_built_from_scratch(self):
        with mock.patch.object(launch, 'Path') as fake:
            fake.return_value.is_dir.return_value = True
            env = launch.slack_env({'RASKLAD_SLACK_HOME': '/home/owner', 'RASKLAD_SLACK_USER': 'owner',
                                    'RASKLAD_SLACK_LANG': 'ru_RU.UTF-8', 'XDG_CONFIG_HOME': '/home/lab/.config',
                                    'DBUS_SESSION_BUS_ADDRESS': 'unix:path=/run/lab/dbus-x', 'RASKLAD_SESSION_ARM_FD': '7'})
        self.assertEqual(env['HOME'], '/home/owner')
        self.assertEqual(env['DBUS_SESSION_BUS_ADDRESS'], 'unix:path=' + launch.SLACK_BUS)
        for key in ('XDG_CONFIG_HOME', 'RASKLAD_SESSION_ARM_FD', 'GTK_IM_MODULE', 'DISPLAY'):
            self.assertNotIn(key, env)

    def test_identity_mismatch_refused(self):
        for user, home, lang in (('owner', '/home/other', 'C.UTF-8'), ('Owner;', '/home/Owner;', 'C.UTF-8'), ('owner', '/home/owner', 'x')):
            with self.assertRaises(ValueError):
                launch.slack_env({'RASKLAD_SLACK_HOME': home, 'RASKLAD_SLACK_USER': user, 'RASKLAD_SLACK_LANG': lang})


class Windows(unittest.TestCase):
    def helpers(self, listed, rc=0):
        out = SimpleNamespace(returncode=rc, stdout=json.dumps(listed).encode())
        return SimpleNamespace(hyprctl=lambda base, *a: out, descends_from=lambda pid, anc: pid in (100, 101))

    def test_class_only_and_foreign_count(self):
        listed = [{'mapped': True, 'pid': 101, 'initialClass': 'Slack', 'title': 'secret workspace'},
                  {'mapped': True, 'pid': 7, 'initialClass': 'other-app'},
                  {'mapped': False, 'pid': 100, 'initialClass': 'Slack'},
                  {'mapped': True, 'pid': 100, 'initialClass': 'bad class with spaces'}]
        self.assertEqual(launch.windows(self.helpers(listed), Base, 100), (2, 1, ['Slack', 'other']))

    def test_hyprctl_failure_is_no_sample(self):
        self.assertIsNone(launch.windows(self.helpers([], rc=1), Base, 100))


class Singleton(unittest.TestCase):
    def test_only_own_lock_released(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(launch.release_own_singleton(tmp, 'host', 7), 'absent')
            os.symlink('host-8', Path(tmp, 'SingletonLock'))
            self.assertEqual(launch.release_own_singleton(tmp, 'host', 7), 'foreign-left')
            os.unlink(Path(tmp, 'SingletonLock'))
            for name, target in (('SingletonLock', 'host-7'), ('SingletonCookie', '1'), ('SingletonSocket', '/tmp/x')):
                os.symlink(target, Path(tmp, name))
            self.assertEqual(launch.release_own_singleton(tmp, 'host', 7), 'released')
            self.assertEqual(os.listdir(tmp), [])


class InsideGate(unittest.TestCase):
    def test_gate_latched_before_slack_starts(self):
        read_end, write_end = os.pipe()
        receipts, addon_end = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        watcher_r = os.dup(read_end)
        seen = {}

        def popen(argv, **kw):
            # By the time Slack is started the arm pipe already carries the latch byte and hit EOF.
            seen['latch'] = os.read(watcher_r, 2)
            seen['eof'] = os.read(watcher_r, 1)
            seen['argv'] = argv
            raise RuntimeError('stop-here')

        env = {'RASKLAD_SLACK_LAUNCH_LAB': '1'}
        relay = SimpleNamespace(port=1, refused=0, close=lambda: None)
        bridge = SimpleNamespace(Relay=lambda *a, **k: relay)
        fake_clients = SimpleNamespace(AddonRecords=lambda base, sock: SimpleNamespace(commits=[], poll=lambda t=0: []))
        with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(launch, 'load', return_value=bridge), \
                mock.patch.object(launch, 'slack_env', return_value={'HOME': '/home/owner'}), \
                mock.patch.object(launch.subprocess, 'Popen', side_effect=popen):
            with self.assertRaises(RuntimeError):
                launch.run_inside(fake_clients, Base, SimpleNamespace(poll=lambda: None),
                                  (read_end, write_end, receipts, addon_end), [])
        self.assertEqual(seen['latch'], b'x')
        self.assertEqual(seen['eof'], b'')
        self.assertIn('--enable-wayland-ime', seen['argv'])
        self.assertTrue(any(a.startswith('--proxy-server=http://127.0.0.1:') for a in seen['argv']))
        os.close(watcher_r)

    def test_refuses_with_supervised_env(self):
        with mock.patch.dict(os.environ, {'RASKLAD_SLACK_LAUNCH_LAB': '1', 'RASKLAD_SLACK_SUPERVISED_LAB': '1'}, clear=True):
            with self.assertRaises(Base.Refused):
                launch.run_inside(None, Base, None, None, [])


class Finish(unittest.TestCase):
    def test_every_step_runs_and_never_raises(self):
        calls = []
        boot = SimpleNamespace(slack_pids=lambda: calls.append('pids') or [], reap_sandboxed=lambda p: calls.append('reap') or [])
        broker = SimpleNamespace(close=mock.Mock(side_effect=OSError('x')))
        proxy = SimpleNamespace(poll=lambda: calls.append('proxy') or 0)
        with tempfile.TemporaryDirectory() as tmp:
            report = launch.finish({'broker': broker, 'proxy': proxy}, {'boot': boot, 'profile': Path(tmp)})
        self.assertEqual(calls, ['proxy', 'pids', 'reap'])
        self.assertEqual(report['finishErrors'], ['broker:OSError'])
        self.assertFalse(report['outerClean'])

    def test_clean(self):
        boot = SimpleNamespace(slack_pids=lambda: [], reap_sandboxed=lambda p: [])
        broker = SimpleNamespace(close=lambda: {'cleanupComplete': True, 'accepted': 1})
        with tempfile.TemporaryDirectory() as tmp:
            report = launch.finish({'broker': broker}, {'boot': boot, 'profile': Path(tmp)})
        self.assertTrue(report['outerClean'])
        with tempfile.TemporaryDirectory() as tmp:
            os.symlink('host-1', Path(tmp, 'SingletonLock'))
            report = launch.finish({'broker': broker}, {'boot': boot, 'profile': Path(tmp)})
        self.assertFalse(report['outerClean'])
        self.assertIn('ownerRecovery', report)

    def test_broker_outlives_trial(self):
        self.assertGreater(launch.BROKER['seconds'], launch.OBSERVE_SECONDS + launch.QUIT_SECONDS + 30)


class Helpers(unittest.TestCase):
    def test_launch_helpers_bound(self):
        self.assertIs(clients.LaunchHelpers.PURPOSES, clients.PURPOSES)
        self.assertTrue(clients.LaunchHelpers.descends_from(os.getpid(), os.getpid()))


if __name__ == '__main__':
    unittest.main()
