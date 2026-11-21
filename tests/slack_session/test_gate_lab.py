import importlib.util
import os
import subprocess
import sys
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load(name, rel):
    spec = importlib.util.spec_from_file_location(name, ROOT / rel)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


clients = load('native_clients_gate_test', 'tools/native_ime_clients.py')
lab = load('wayland_lab_gate_test', 'tools/wayland_ime_lab.py')


class Base:
    class Refused(Exception):
        pass


class GatePipe(unittest.TestCase):
    def test_inactive_without_gate_env(self):
        env = {k: v for k, v in os.environ.items() if k not in ('RASKLAD_SLACK_GATE_LAB',)}
        with unittest.mock.patch.dict(os.environ, env, clear=True):
            self.assertIsNone(clients.slack_gate_pipe(Base, {}))

    def test_requires_session_env_and_system_frontend(self):
        with unittest.mock.patch.dict(os.environ, {'RASKLAD_SLACK_GATE_LAB': '1'}, clear=True):
            with self.assertRaises(Base.Refused):
                clients.slack_gate_pipe(Base, {})

    def test_fcitx_gets_only_read_end(self):
        with unittest.mock.patch.dict(os.environ, {'RASKLAD_SLACK_GATE_LAB': '1', 'RASKLAD_SLACK_SESSION_LAB': '1'}, clear=True):
            options = {}
            read_end, write_end, receipts, addon_end = clients.slack_gate_pipe(Base, options)
        try:
            self.assertEqual(options['pass_fds'], (read_end, addon_end.fileno()))
            self.assertEqual(options['env']['RASKLAD_SESSION_ARM_FD'], str(read_end))
            self.assertEqual(options['env']['RASKLAD_SESSION_RECEIPT_FD'], str(addon_end.fileno()))
            self.assertFalse(os.get_inheritable(write_end))
            self.assertFalse(receipts.get_inheritable())
            import socket
            self.assertEqual(receipts.type, socket.SOCK_SEQPACKET)
        finally:
            os.close(read_end); os.close(write_end); receipts.close(); addon_end.close()


class WriteEndDetection(unittest.TestCase):
    def test_read_end_only_is_absent_write_end_is_present(self):
        r, w = os.pipe()
        try:
            reader = subprocess.Popen(['/usr/bin/sleep', '5'], pass_fds=(r,))
            writer = subprocess.Popen(['/usr/bin/sleep', '5'], pass_fds=(w,))
            try:
                for child in (reader, writer):  # after exec the inherited fds are final
                    for _ in range(200):
                        if os.readlink(f'/proc/{child.pid}/exe') == '/usr/bin/sleep':
                            break
                        time.sleep(.01)
                self.assertFalse(clients.fd_present(reader.pid, w))
                self.assertTrue(clients.fd_present(writer.pid, w))
            finally:
                reader.kill(); writer.kill(); reader.wait(); writer.wait()
        finally:
            os.close(r); os.close(w)


class Holders(unittest.TestCase):
    def test_write_end_holders_finds_only_writers(self):
        r, w = os.pipe()
        try:
            writer = subprocess.Popen(['/usr/bin/sleep', '5'], pass_fds=(w,))
            reader = subprocess.Popen(['/usr/bin/sleep', '5'], pass_fds=(r,))
            try:
                for child in (reader, writer):
                    for _ in range(200):
                        if os.readlink(f'/proc/{child.pid}/exe') == '/usr/bin/sleep':
                            break
                        time.sleep(.01)
                holders = clients.write_end_holders(w)
                self.assertIn(writer.pid, holders)
                self.assertNotIn(reader.pid, holders)
                self.assertNotIn(os.getpid(), holders)
            finally:
                reader.kill(); writer.kill(); reader.wait(); writer.wait()
        finally:
            os.close(r); os.close(w)

    def test_driver_never_receives_the_write_end(self):
        source = (ROOT / 'tools/native_ime_clients.py').read_text()
        start = source.index('def run_gate_driver'); end = source.index('def fd_present')
        self.assertNotIn('pass_fds', source[start:end])
        self.assertNotIn('RASKLAD_SESSION_ARM_WRITE_FD', source)
        self.assertNotIn('RASKLAD_SESSION_ARM_WRITE_FD', (ROOT / 'tests/chromium_ime/driver.mjs').read_text())

    def test_supervised_driver_never_spawns_the_helper(self):
        driver = (ROOT / 'tests/chromium_ime/driver.mjs').read_text()
        self.assertIn("if(nativeWayland&&!supervised){\n vk=spawn(", driver)
        source = (ROOT / 'tools/native_ime_clients.py').read_text()
        body = source[source.index('def run_supervised_driver'):source.index('def load_lab_module')]
        self.assertIn('os.set_blocking(helper.stdin.fileno(),False)', body)
        self.assertIn("sup.Supervisor([binding[0]],'correction')", body)
        self.assertIn("token=s.issue(command,*current())", body)
        self.assertNotIn('pass_fds', body)


class HyprlandSupervised(unittest.TestCase):
    def setUp(self):
        self.hypr = load('hyprland_ime_lab_test', ROOT / 'tools/hyprland_ime_lab.py')

    def test_selection_alone_only(self):
        self.hypr.supervised_selection(False, True, True, True, True, True, True, 3, True, True, 'keep-grab')
        self.hypr.supervised_selection(True, False, False, False, False, False, False, 1, False, False, 'system')
        for bad in ((True, True, False, False, False, False, False, 1, False, False, 'system'),
                    (True, False, False, False, False, False, False, 2, False, False, 'system'),
                    (True, False, False, False, False, False, False, 1, False, False, 'keep-grab'),
                    (1, False, False, False, False, False, False, 1, False, False, 'system')):
            with self.assertRaises(self.hypr.base.Refused):
                self.hypr.supervised_selection(*bad)

    def test_cli_requires_run_approved(self):
        out = subprocess.run([sys.executable, '-I', str(ROOT / 'tools/hyprland_ime_lab.py'), '--slack-supervised'],
                             capture_output=True, text=True, timeout=20)
        self.assertIn('slack_supervised_requires_run_approved', out.stdout)

    def test_run_forwards_every_selector_to_sandbox_args(self):
        import ast, inspect
        tree = ast.parse(inspect.getsource(self.hypr.run))
        calls = [n for n in ast.walk(tree) if isinstance(n, ast.Call) and getattr(n.func, 'id', None) == 'sandbox_args']
        self.assertEqual(len(calls), 1)
        keywords = {k.arg: getattr(k.value, 'id', None) for k in calls[0].keywords}
        for name in ('slack_supervised', 'seat_probe'):
            self.assertEqual(keywords.get(name), name)

    def test_descends_from(self):
        self.assertTrue(clients.descends_from(os.getpid(), os.getpid()))
        self.assertTrue(clients.descends_from(os.getpid(), os.getppid()))
        self.assertFalse(clients.descends_from(os.getppid(), os.getpid()))


class AddonRecordParsing(unittest.TestCase):
    def test_records(self):
        import socket
        a, b = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        try:
            records = clients.AddonRecords(Base, a)
            for r in (b'in007\n', b'c0701\n', b'up007\n'):
                b.send(r)
            fresh = records.poll(.2)
            self.assertEqual(fresh, [b'in007\n', b'up007\n'])
            self.assertEqual(records.commits, [b'c0701\n'])
            for bad in (b'hello!', b'zn001\n', b'iq001\n', b'in0a1\n', b'in01\n'):
                b.send(bad)
                with self.assertRaises(Base.Refused):
                    records.poll(.2)
        finally:
            a.close(); b.close()


class FakeHypr:
    def __init__(self, keyboards, fail=()):
        self.keyboards, self.fail, self.submap, self.calls = list(keyboards), set(fail), 'default', []

    def __call__(self, base, *args, raw=False):
        import json as _json
        self.calls.append(args)
        if args == ('-j', 'devices'):
            out = _json.dumps({'keyboards': [{'name': n, 'address': a} for n, a in self.keyboards]}).encode()
            return subprocess.CompletedProcess(args, 0, out, b'')
        if args[0] in self.fail:
            result = 'error'
        elif args[:2] == ('dispatch', 'submap'):
            self.submap = 'rasklad_lock' if args[2] == 'rasklad_lock' else 'default'
            result = 'ok'
        elif args == ('submap',):
            result = self.submap
        else:
            result = 'ok'
        return result if raw else subprocess.CompletedProcess(args, 0, result.encode(), b'')


class Seat(unittest.TestCase):
    ONE = [('wl_keyboard', '0x1'), ('hl-virtual-keyboard-fcitx5', '0x2'), ('hl-virtual-keyboard-unknown', '0x3')]

    def seat(self, fake):
        original = clients.hyprctl
        clients.hyprctl = fake
        self.addCleanup(setattr, clients, 'hyprctl', original)
        return clients.HyprSeat(Base)

    def test_lock_and_unlock(self):
        fake = FakeHypr(self.ONE)
        seat = self.seat(fake)
        self.assertTrue(seat.lock())
        self.assertTrue(seat.intact())
        self.assertIn(('keyword', 'device[hl-virtual-keyboard-unknown]:keybinds', 'false'), fake.calls)
        self.assertTrue(seat.unlock())
        self.assertEqual(fake.submap, 'default')
        self.assertIn(('keyword', 'device[hl-virtual-keyboard-unknown]:keybinds', 'true'), fake.calls)
        self.assertFalse(seat.exempt)

    def test_refuses_second_helper(self):
        fake = FakeHypr(self.ONE + [('hl-virtual-keyboard-unknown-1', '0x4')])
        seat = self.seat(fake)
        self.assertFalse(seat.lock())
        self.assertNotIn(('keyword', 'device[hl-virtual-keyboard-unknown]:keybinds', 'false'), fake.calls)

    def test_replaced_device_breaks_intact(self):
        fake = FakeHypr(self.ONE)
        seat = self.seat(fake)
        self.assertTrue(seat.lock())
        fake.keyboards[-1] = ('hl-virtual-keyboard-unknown', '0x9')  # same name, new device
        self.assertFalse(seat.intact())

    def test_submap_failure_unwinds_exemption(self):
        fake = FakeHypr(self.ONE, fail={'dispatch'})
        seat = self.seat(fake)
        self.assertFalse(seat.lock())
        self.assertFalse(seat.exempt)
        self.assertIn(('keyword', 'device[hl-virtual-keyboard-unknown]:keybinds', 'true'), fake.calls)

    def test_failed_release_keeps_state(self):
        fake = FakeHypr(self.ONE)
        seat = self.seat(fake)
        self.assertTrue(seat.lock())
        fake.fail = {'keyword', 'dispatch'}
        self.assertFalse(seat.unlock())
        self.assertTrue(seat.exempt)
        self.assertTrue(seat.locked)


class LabSelection(unittest.TestCase):
    def test_slack_gate_is_exclusive(self):
        for options in (dict(focus_stress=True), dict(addon_witness=True), dict(frontend_variant='keep-grab'),
                        dict(disable_suite=True), dict(focus_repeats=2), dict(nested_smoke=True)):
            with self.assertRaises(lab.base.Refused):
                lab.slack_gate_selection(True, **options)
        lab.slack_gate_selection(True, focus_stress=False, frontend_variant='system', focus_repeats=1)
        with self.assertRaises(lab.base.Refused):
            lab.slack_gate_selection(True, focus_repeats=True)
        lab.slack_gate_selection(False, focus_stress=True)

    def test_sandbox_sets_gate_env_and_addon(self):
        source = (ROOT / 'tools/wayland_ime_lab.py').read_text()
        self.assertIn("'--setenv','RASKLAD_SLACK_SESSION_LAB','1','--setenv','RASKLAD_SLACK_GATE_LAB','1'", source)
        self.assertNotIn("/lab/addons/raskladslackgatetest.so", source)  # loads the sealed snapshot
        conf = (ROOT / 'tests/slack_session/raskladslackgatetest.conf').read_text()
        self.assertIn('Library=raskladlab\n', conf)

    def test_gate_mode_values(self):
        with self.assertRaises(lab.base.Refused):
            lab.slack_gate_selection('other')
        lab.slack_gate_selection('control', focus_stress=False)
        lab.slack_gate_selection('supervised', focus_stress=False)

    def test_gate_flags_require_plain_run(self):
        for argv in (['--slack-gate'], ['--slack-supervised'], ['--smoke-nested-hypr', '--slack-gate'],
                     ['--run', '--slack-gate', '--slack-gate-control'], ['--run', '--slack-supervised', '--slack-gate']):
            out = subprocess.run([sys.executable, '-I', str(ROOT / 'tools/wayland_ime_lab.py')] + argv,
                                 capture_output=True, text=True, timeout=20)
            self.assertNotEqual(out.returncode, 0)
            self.assertIn('slack_gate_requires_run_only', out.stderr + out.stdout)


import unittest.mock  # noqa: E402

if __name__ == '__main__':
    unittest.main()
