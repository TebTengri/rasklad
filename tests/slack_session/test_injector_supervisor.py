import importlib.util
import re
import sys
import threading
import time
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
    return sup.Supervisor([PID, PID + 1], case, now=clock or Clock(), seconds=seconds)


class Armed(unittest.TestCase):
    def armed(self, case='correction', clock=None):
        s = new(case, clock)
        self.assertTrue(s.arm(PID, GEN, EPOCH, P))
        self.assertTrue(s.seat_locked())
        return s

    def step(self, s, command, out):
        token = s.issue(command, *B)
        return s.send(token, command, *B, out.append)

    def typed(self):
        s, out = self.armed(), []
        self.assertTrue(self.step(s, 'word', out) and self.step(s, 'space', out))
        return s


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

    def test_receipt_records(self):
        self.assertEqual(inj.receipt('correction'), b'c07\n')
        self.assertEqual(inj.receipt('english'), b'o06\n')
        with self.assertRaises(inj.Refused):
            inj.receipt('undo')

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
            if command == 'backspace' and not s._receipt_ok:
                self.assertTrue(s.commit_receipt(b'c07\n'))
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
        self.assertTrue(self.typed().commit_receipt(b'c07\n'))
        for bad in (b'c06\n', b'o07\n', b'o06\n', 'привет ', None, b'c07', b'c07\n\n'):
            s = self.typed()
            self.assertFalse(s.commit_receipt(bad))
            self.assertEqual(s.revoke_reason, 'commit-mismatch')

    def test_backspace_requires_matching_receipt(self):
        s = self.typed()
        self.assertIsNone(s.issue('backspace', *B))
        self.assertEqual(s.revoke_reason, 'commit-mismatch')

    def test_receipt_before_typing_finished_or_twice_revokes(self):
        s, out = self.armed(), []
        self.step(s, 'word', out)
        self.assertFalse(s.commit_receipt(b'c07\n'))
        s = self.typed()
        self.assertTrue(s.commit_receipt(b'c07\n'))
        self.assertFalse(s.commit_receipt(b'c07\n'))
        self.assertEqual(s.state, 'REVOKED')


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
                sup.Supervisor(pids, 'english', now=Clock(), seconds=seconds)
        for case in ('undo', None, ['word'], 'lab-off'):
            with self.assertRaises(ValueError):
                sup.Supervisor([PID], case, now=Clock())
        for clock in (None, lambda: float('nan'), lambda: 'now'):
            with self.assertRaises(ValueError):
                sup.Supervisor([PID], 'english', now=clock)

    def test_wire_bytes_only_from_fixed_table(self):
        s = new()
        self.assertEqual(s._wire, inj.WIRE)
        self.assertNotIn('undo', s._wire)

    def test_bad_clock_after_start_revokes(self):
        clock = Clock()
        s = self.armed(clock=clock)
        token = s.issue('word', *B)
        clock.t = float('nan')
        self.assertFalse(s.send(token, 'word', *B, [].append))
        self.assertEqual((s.state, s.revoke_reason), ('REVOKED', 'error'))

    def test_revoke_flag_set_while_write_blocks_and_stops_next_write(self):
        s = self.typed()
        self.assertTrue(s.commit_receipt(b'c07\n'))
        token = s.issue('backspace', *B)
        entered, release, written = threading.Event(), threading.Event(), []

        def blocking_write(data):
            entered.set(); release.wait(2); written.append(data)

        t = threading.Thread(target=s.send, args=(token, 'backspace', *B, blocking_write))
        t.start(); entered.wait(2)
        revoker = threading.Thread(target=s.event, args=('f12',))
        revoker.start(); time.sleep(0.05)
        self.assertTrue(s._revoked.is_set())  # visible before the lock is free
        release.set(); t.join(2); revoker.join(2)
        self.assertEqual(s.state, 'REVOKED')
        self.assertIsNone(s.issue('backspace', *B))
        self.assertEqual(written, [b'backspace\n'])
        self.assertEqual(s.attempted, 3)


if __name__ == '__main__':
    unittest.main()
