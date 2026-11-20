"""Supervised-session state machine (pure; I/O is wired by the launcher).

IDLE -> ARMED (F10 on a bound normal-text field) -> SEAT_LOCKED (own seat grabbed)
-> INJECT (one token per key) -> REVOKED (F12, deadline, any lifecycle change).
REVOKED is terminal: a new session needs a new supervisor.
"""
import secrets
import time

STATES = ('IDLE', 'ARMED', 'SEAT_LOCKED', 'INJECT', 'REVOKED')
NORMAL_PURPOSES = frozenset({'normal', 'alpha'})
REVOKE_EVENTS = frozenset({'f12', 'deadline', 'pid-exit', 'focus-change', 'ic-reset', 'purpose-change',
                           'surface-change', 'fcitx-exit', 'compositor-exit', 'seat-lost', 'error'})
SESSION_SECONDS = 600


class Supervisor:
    def __init__(self, slack_pids, now=time.monotonic, seconds=SESSION_SECONDS):
        if not slack_pids or not all(type(p) is int and p > 1 for p in slack_pids):
            raise ValueError('slack-pids')
        if not 0 < seconds <= SESSION_SECONDS:
            raise ValueError('seconds')
        self.slack_pids = frozenset(slack_pids)
        self.now = now
        self.deadline = now() + seconds
        self.state = 'IDLE'
        self.binding = None
        self.token = None
        self.revoke_reason = None

    def _expired(self):
        if self.state != 'REVOKED' and self.now() >= self.deadline:
            self.revoke('deadline')
        return self.state == 'REVOKED'

    def arm(self, client_pid, surface_generation, ic_epoch, purpose):
        """F10: bind to exactly one field of a Slack-owned client with a normal-text purpose."""
        if self._expired() or self.state != 'IDLE':
            return False
        if client_pid not in self.slack_pids or purpose not in NORMAL_PURPOSES:
            self.revoke('error')
            return False
        if type(surface_generation) is not int or type(ic_epoch) is not int:
            self.revoke('error')
            return False
        self.binding = (client_pid, surface_generation, ic_epoch)
        self.state = 'ARMED'
        return True

    def seat_locked(self):
        if self._expired() or self.state != 'ARMED':
            return False
        self.state = 'SEAT_LOCKED'
        return True

    def issue(self, client_pid, surface_generation, ic_epoch):
        """Single-use token for exactly one key, only while the binding is unchanged."""
        if self._expired() or self.state not in ('SEAT_LOCKED', 'INJECT') or self.token is not None:
            return None
        if (client_pid, surface_generation, ic_epoch) != self.binding:
            self.revoke('focus-change')
            return None
        self.state = 'INJECT'
        self.token = secrets.token_hex(16)
        return self.token

    def consume(self, token):
        if self._expired() or self.state != 'INJECT' or self.token is None:
            return False
        ok = secrets.compare_digest(str(token), self.token)
        self.token = None
        if not ok:
            self.revoke('error')
        return ok

    def event(self, name):
        if name not in REVOKE_EVENTS:
            name = 'error'
        self.revoke(name)

    def revoke(self, reason):
        if self.state != 'REVOKED':
            self.revoke_reason = reason
        self.state = 'REVOKED'
        self.token = None
        self.binding = None
