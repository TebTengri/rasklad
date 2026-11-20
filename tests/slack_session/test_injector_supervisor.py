import importlib.util
import re
import sys
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name + '_under_test', ROOT / f'tools/{name}.py')
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


inj = load('slack_injector')
sup = load('slack_supervisor')
PID, GEN, EPOCH, P = 4242, 3, 7, 'normal'
B = (PID, GEN, EPOCH, P)


class Clock:
    def __init__(self):
        self.t = 100.0

    def __call__(self):
        return self.t


def new(case='correction', clock=None, seconds=sup.SESSION_SECONDS):
    return sup.Supervisor([PID, PID + 1], inj.script(case), inj.WIRE, now=clock or Clock(), seconds=seconds)


class Armed(unittest.TestCase):
    def armed(self, case='correction', clock=None):
        s = new(case, clock)
        self.assertTrue(s.arm(PID, GEN, EPOCH, P))
        self.assertTrue(s.seat_locked())
        return s

    def step(self, s, command, out):
        token = s.issue(command, *B)
        return s.send(token, command, *B, out.append)


class Injector(unittest.TestCase):
    def test_scripts_are_fixed_and_cleanup_matches_expected_text(self):
        self.assertEqual(inj.script('correction'), ['word', 'space'] + ['backspace'] * 7)
        self.assertEqual(inj.script('english'), ['hello'] + ['backspace'] * 6)
        for case in inj.TYPED:
            self.assertTrue(set(inj.script(case)) <= inj.ALLOWED)
        for bad in ('undo', 'Correction', None, ['english']):
            with self.assertRaises(inj.Refused):
                inj.script(bad)

    def test_allowlist_parity_with_helper_parse(self):
        source = (ROOT / 'tests/wayland_ime/keys.cpp').read_text()
        parse = source[source.index('Cmd parse('):source.index('bool run(')]
        helper = set(re.findall(r'line == "([a-z0-9-]+)"', parse))
        self.assertTrue(inj.ALLOWED <= helper)
        self.assertEqual(helper - inj.ALLOWED, inj.FORBIDDEN)
        self.assertFalse(inj.ALLOWED & inj.FORBIDDEN)
        for name, wire in inj.WIRE.items():
            self.assertEqual(wire, name.encode('ascii') + b'\n')

    def test_graphemes_whitelist(self):
        self.assertEqual(inj.graphemes('привет '), 7)
        self.assertEqual(inj.graphemes('hello '), 6)
        for bad in ('é', 'a\nb', 'a\x7f', 'a\x85', 'a​b', '﻿a', 'a҃', '😀', 'a ', '', None, b'ab', 'ab1'):
            with self.assertRaises(inj.Refused):
                inj.graphemes(bad)


class HappyPath(Armed):
    def test_full_script_writes_exact_bytes_in_order(self):
        s, out = self.armed(), []
        for command in inj.script('correction'):
            self.assertTrue(self.step(s, command, out))
        self.assertEqual(out, [b'word\n', b'space\n'] + [b'backspace\n'] * 7)
        self.assertTrue(s.done())
        self.assertIsNone(s.issue('backspace', *B))  # budget exhausted
        self.assertEqual(s.state, 'REVOKED')

    def test_alpha_purpose_arms_and_stays_bound(self):
        s, out = new('english'), []
        self.assertTrue(s.arm(PID, GEN, EPOCH, 'alpha'))
        self.assertTrue(s.seat_locked())
        token = s.issue('hello', PID, GEN, EPOCH, 'alpha')
        self.assertTrue(s.send(token, 'hello', PID, GEN, EPOCH, 'alpha', out.append))
        self.assertEqual(out, [b'hello\n'])

    def test_commit_receipt_must_match_exactly(self):
        s = self.armed()
        self.assertTrue(s.commit_receipt('привет ', inj.EXPECTED['correction']))
        for bad in ('привет', 'ghbdtn ', None):
            s = self.armed()
            self.assertFalse(s.commit_receipt(bad, inj.EXPECTED['correction']))
            self.assertEqual(s.revoke_reason, 'commit-mismatch')


class Refusals(Armed):
    def test_out_of_script_command_revokes(self):
        for command in ('space', 'backspace', 'hello', 'undo', 'enter', '', None, ['word']):
            s = self.armed()
            self.assertIsNone(s.issue(command, *B))
            self.assertEqual(s.state, 'REVOKED')

    def test_str_subclass_command_and_token_refused(self):
        class Sneaky(str):
            def __eq__(self, other): return True
            __hash__ = str.__hash__
        s, out = self.armed(), []
        self.assertIsNone(s.issue(Sneaky('space'), *B))
        s = self.armed()
        token = s.issue('word', *B)
        self.assertFalse(s.send(Sneaky(token), 'word', *B, out.append))
        self.assertEqual(out, [])

    def test_token_bound_to_command_and_single_use(self):
        s, out = self.armed(), []
        token = s.issue('word', *B)
        self.assertFalse(s.send(token, 'space', *B, out.append))
        self.assertEqual(s.state, 'REVOKED')
        s = self.armed()
        token = s.issue('word', *B)
        self.assertTrue(s.send(token, 'word', *B, out.append))
        self.assertFalse(s.send(token, 'space', *B, out.append))
        self.assertEqual(out, [b'word\n'])

    def test_bad_tokens_revoke_without_exception(self):
        for bad in ('é' * 32, None, 1234, 'x', '0' * 32, b'0' * 32):
            s, out = self.armed(), []
            s.issue('word', *B)
            self.assertFalse(s.send(bad, 'word', *B, out.append))
            self.assertEqual((s.state, out), ('REVOKED', []))

    def test_send_without_issue_and_foreign_token(self):
        s, other, out = self.armed(), self.armed(), []
        self.assertFalse(s.send('0' * 32, 'word', *B, out.append))
        s = self.armed()
        s.issue('word', *B)
        self.assertFalse(s.send(other.issue('word', *B), 'word', *B, out.append))
        self.assertEqual(out, [])

    def test_type_confusion_in_binding_refused(self):
        for changed in ((PID, True, EPOCH), (PID, 3.0, EPOCH), (4242.0, GEN, EPOCH), (PID, GEN, 7.0)):
            s = new()
            self.assertFalse(s.arm(*changed, P))
            s = self.armed()
            self.assertIsNone(s.issue('word', *changed, P))
            self.assertEqual(s.revoke_reason, 'focus-change')

    def test_binding_or_purpose_change_at_send_revokes(self):
        for changed in ((PID + 1, GEN, EPOCH, P), (PID, GEN + 1, EPOCH, P), (PID, GEN, EPOCH + 1, P),
                        (PID, GEN, EPOCH, 'password'), (PID, GEN, EPOCH, None)):
            s, out = self.armed(), []
            token = s.issue('word', *B)
            self.assertFalse(s.send(token, 'word', *changed, out.append))
            self.assertEqual((s.state, out), ('REVOKED', []))

    def test_arm_refusals(self):
        class Sneaky(str):
            def __eq__(self, other): return True
            __hash__ = str.__hash__
        for args in ((999, GEN, EPOCH, P), (PID, GEN, EPOCH, 'password'), (PID, GEN, EPOCH, 'pin'),
                     (PID, GEN, EPOCH, ['normal']), (PID, GEN, EPOCH, Sneaky('x')), (1, GEN, EPOCH, P), (PID, -1, EPOCH, P)):
            s = new()
            self.assertFalse(s.arm(*args))
            self.assertEqual(s.state, 'REVOKED')

    def test_out_of_order_transitions_revoke(self):
        s = new()
        self.assertFalse(s.seat_locked())
        self.assertEqual(s.state, 'REVOKED')
        s = new()
        self.assertTrue(s.arm(*B))
        self.assertIsNone(s.issue('word', *B))  # seat not grabbed
        s = self.armed()
        self.assertFalse(s.arm(*B))  # double arm
        self.assertEqual(s.state, 'REVOKED')

    def test_failing_write_revokes(self):
        def boom(_): raise OSError('pipe')
        s = self.armed()
        token = s.issue('word', *B)
        self.assertFalse(s.send(token, 'word', *B, boom))
        self.assertEqual(s.state, 'REVOKED')


class Revocation(Armed):
    def test_every_event_revokes_terminally(self):
        for name in sorted(sup.REVOKE_EVENTS) + ['unknown', ['list'], {'d': 1}, None]:
            s, out = self.armed(), []
            token = s.issue('word', *B)
            s.event(name)
            self.assertEqual(s.state, 'REVOKED')
            self.assertFalse(s.send(token, 'word', *B, out.append))
            self.assertFalse(s.arm(*B))
            self.assertIsNone(s.issue('word', *B))
            self.assertEqual(out, [])
        s = self.armed(); s.event(['list'])
        self.assertEqual(s.revoke_reason, 'error')

    def test_deadline_boundary(self):
        clock = Clock()
        s = self.armed(clock=clock)
        clock.t += sup.SESSION_SECONDS - 0.001
        token = s.issue('word', *B)
        self.assertIsNotNone(token)
        clock.t += 0.001
        self.assertFalse(s.send(token, 'word', *B, [].append))
        self.assertEqual(s.revoke_reason, 'deadline')
        clock = Clock()
        s = self.armed(clock=clock)
        clock.t += sup.SESSION_SECONDS
        self.assertIsNone(s.issue('word', *B))

    def test_revoke_cannot_interleave_with_write(self):
        s = self.armed()
        token = s.issue('word', *B)
        written, entered, release = [], threading.Event(), threading.Event()

        def slow_write(data):
            entered.set()
            release.wait(2)
            written.append((data, s.state))

        t = threading.Thread(target=s.send, args=(token, 'word', *B, slow_write))
        t.start()
        entered.wait(2)
        revoker = threading.Thread(target=s.event, args=('f12',))
        revoker.start()
        revoker.join(0.2)
        self.assertTrue(revoker.is_alive())  # blocked until the write finishes
        release.set()
        t.join(2); revoker.join(2)
        self.assertEqual(written, [(b'word\n', 'INJECT')])
        self.assertEqual(s.state, 'REVOKED')
        self.assertIsNone(s.issue('space', *B))

    def test_constructor_bounds(self):
        for pids, seconds in (([], 10), ([1], 10), ([True], 10), ([PID], 0), ([PID], 601), ([PID], True),
                              ([PID], float('nan')), ([PID], float('inf'))):
            with self.assertRaises(ValueError):
                sup.Supervisor(pids, inj.script('english'), inj.WIRE, now=Clock(), seconds=seconds)
        for commands in ([], ['undo'], [None]):
            with self.assertRaises(ValueError):
                sup.Supervisor([PID], commands, inj.WIRE, now=Clock())
        with self.assertRaises(ValueError):
            sup.Supervisor([PID], ['word'], inj.WIRE, now=None)


if __name__ == '__main__':
    unittest.main()
