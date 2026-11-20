import importlib.util
import sys
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
PID, GEN, EPOCH = 4242, 3, 7


class Clock:
    def __init__(self):
        self.t = 100.0

    def __call__(self):
        return self.t


def armed(clock=None):
    s = sup.Supervisor([PID, PID + 1], now=clock or Clock())
    assert s.arm(PID, GEN, EPOCH, 'normal') and s.seat_locked()
    return s


class Injector(unittest.TestCase):
    def test_cases_only_use_allowed_commands(self):
        self.assertEqual(inj.case_commands('correction'), ['word', 'space'])
        self.assertEqual(inj.case_commands('english'), ['hello'])
        for case in inj.CASES:
            self.assertTrue(set(inj.case_commands(case)) <= inj.ALLOWED)
        with self.assertRaises(inj.Refused):
            inj.case_commands('undo')

    def test_dangerous_helper_commands_never_allowed(self):
        for bad in ('undo', 'escape', 'lab-off', 'lab-on', 'xspace', 'sample2', 'mixed', 'long', 'enter', 'word\nundo', ''):
            s = armed()
            token = s.issue(PID, GEN, EPOCH)
            with self.assertRaises(inj.Refused):
                inj.encode(bad, token, s)

    def test_every_command_needs_a_fresh_token(self):
        s = armed()
        token = s.issue(PID, GEN, EPOCH)
        self.assertEqual(inj.encode('word', token, s), b'word\n')
        with self.assertRaisesRegex(inj.Refused, 'token'):
            inj.encode('space', token, s)

    def test_cleanup_by_committed_grapheme_count(self):
        self.assertEqual(inj.graphemes(inj.EXPECTED['correction']), 7)
        self.assertEqual(inj.cleanup_commands(7), ['backspace'] * 7)
        for bad in (-1, 17, True, 7.0, '7'):
            with self.assertRaises(inj.Refused):
                inj.cleanup_commands(bad)
        for bad in ('é', 'a\nb', 'a\x7f'):
            with self.assertRaises(inj.Refused):
                inj.graphemes(bad)


class SupervisorStates(unittest.TestCase):
    def test_linear_happy_path(self):
        s = sup.Supervisor([PID], now=Clock())
        self.assertEqual(s.state, 'IDLE')
        self.assertIsNone(s.issue(PID, GEN, EPOCH))
        self.assertTrue(s.arm(PID, GEN, EPOCH, 'normal'))
        self.assertIsNone(s.issue(PID, GEN, EPOCH))  # seat not grabbed yet
        self.assertTrue(s.seat_locked())
        self.assertTrue(s.consume(s.issue(PID, GEN, EPOCH)))
        self.assertEqual(s.state, 'INJECT')

    def test_one_outstanding_token(self):
        s = armed()
        self.assertIsNotNone(s.issue(PID, GEN, EPOCH))
        self.assertIsNone(s.issue(PID, GEN, EPOCH))

    def test_arm_refuses_foreign_client_or_protected_purpose(self):
        for pid, purpose in ((999, 'normal'), (PID, 'password'), (PID, 'pin'), (PID, 'digits'), (PID, None)):
            s = sup.Supervisor([PID], now=Clock())
            self.assertFalse(s.arm(pid, GEN, EPOCH, purpose))
            self.assertEqual(s.state, 'REVOKED')

    def test_binding_change_revokes(self):
        for changed in ((PID + 1, GEN, EPOCH), (PID, GEN + 1, EPOCH), (PID, GEN, EPOCH + 1)):
            s = armed()
            self.assertIsNone(s.issue(*changed))
            self.assertEqual((s.state, s.revoke_reason), ('REVOKED', 'focus-change'))

    def test_every_lifecycle_event_revokes_and_is_terminal(self):
        for name in sorted(sup.REVOKE_EVENTS) + ['unknown-event']:
            s = armed()
            token = s.issue(PID, GEN, EPOCH)
            s.event(name)
            self.assertEqual(s.state, 'REVOKED')
            self.assertFalse(s.consume(token))
            self.assertFalse(s.arm(PID, GEN, EPOCH, 'normal'))
            self.assertIsNone(s.issue(PID, GEN, EPOCH))
        s = armed(); s.event('unknown-event')
        self.assertEqual(s.revoke_reason, 'error')

    def test_deadline_revokes_even_with_token(self):
        clock = Clock()
        s = armed(clock)
        token = s.issue(PID, GEN, EPOCH)
        clock.t += sup.SESSION_SECONDS
        self.assertFalse(s.consume(token))
        self.assertEqual(s.revoke_reason, 'deadline')

    def test_wrong_token_revokes(self):
        s = armed()
        s.issue(PID, GEN, EPOCH)
        self.assertFalse(s.consume('0' * 32))
        self.assertEqual(s.state, 'REVOKED')

    def test_constructor_bounds(self):
        for pids, seconds in (([], 10), ([1], 10), ([True], 10), ([PID], 0), ([PID], 601)):
            with self.assertRaises(ValueError):
                sup.Supervisor(pids, now=Clock(), seconds=seconds)


if __name__ == '__main__':
    unittest.main()
