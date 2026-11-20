"""Supervised-session state machine (pure logic; the launcher supplies `write`).

IDLE -> ARMED (F10 on a bound normal-text field) -> SEAT_LOCKED (own seat grabbed)
-> INJECT (one token per scripted command) -> REVOKED (F12, deadline, any lifecycle
change, any error). REVOKED is terminal. Every public method holds one lock, and
`send` writes while holding it, so a revoke cannot interleave with a write. Any
unexpected input or exception revokes (fail closed).
"""
import math
import secrets
import threading
import time

STATES = ('IDLE', 'ARMED', 'SEAT_LOCKED', 'INJECT', 'REVOKED')
NORMAL_PURPOSES = frozenset({'normal', 'alpha'})
REVOKE_EVENTS = frozenset({'f12', 'deadline', 'pid-exit', 'focus-change', 'ic-reset', 'purpose-change',
                           'surface-change', 'fcitx-exit', 'compositor-exit', 'seat-lost', 'commit-mismatch', 'error'})
SESSION_SECONDS = 600


def _binding(client_pid, surface_generation, ic_epoch):
    values = (client_pid, surface_generation, ic_epoch)
    if not all(type(v) is int for v in values) or client_pid <= 1 or surface_generation < 0 or ic_epoch < 0:
        return None
    return values


class Supervisor:
    def __init__(self, slack_pids, commands, wire, now=time.monotonic, seconds=SESSION_SECONDS):
        pids = list(slack_pids)
        if not pids or not all(type(p) is int and p > 1 for p in pids):
            raise ValueError('slack-pids')
        if type(seconds) not in (int, float) or not math.isfinite(seconds) or not 0 < seconds <= SESSION_SECONDS:
            raise ValueError('seconds')
        if not callable(now):
            raise ValueError('clock')
        commands = list(commands)
        if not commands or not all(type(c) is str and type(wire.get(c)) is bytes for c in commands):
            raise ValueError('script')
        self._lock = threading.RLock()
        self.slack_pids = frozenset(pids)
        self.now = now
        self.deadline = now() + seconds
        self._queue = commands
        self._wire = dict(wire)
        self.state = 'IDLE'
        self.binding = None
        self._token = None
        self.sent = 0
        self.revoke_reason = None

    # -- internal (lock held) --
    def _expired(self):
        if self.state != 'REVOKED' and self.now() >= self.deadline:
            self._revoke('deadline')
        return self.state == 'REVOKED'

    def _revoke(self, reason):
        if self.state != 'REVOKED':
            self.revoke_reason = reason
        self.state = 'REVOKED'
        self._token = None
        self.binding = None
        self._queue = []

    def _current_ok(self, client_pid, surface_generation, ic_epoch, purpose):
        if type(purpose) is not str or purpose not in NORMAL_PURPOSES:
            self._revoke('purpose-change')
            return False
        if _binding(client_pid, surface_generation, ic_epoch) != self.binding:
            self._revoke('focus-change')
            return False
        return True

    # -- public --
    def arm(self, client_pid, surface_generation, ic_epoch, purpose):
        """F10: bind to exactly one field of a Slack-owned client with a normal-text purpose."""
        with self._lock:
            try:
                if self._expired() or self.state != 'IDLE':
                    self._revoke('error')
                    return False
                binding = _binding(client_pid, surface_generation, ic_epoch)
                if (binding is None or client_pid not in self.slack_pids or type(purpose) is not str
                        or purpose not in NORMAL_PURPOSES):
                    self._revoke('error')
                    return False
                self.binding = binding
                self.state = 'ARMED'
                return True
            except Exception:
                self._revoke('error')
                return False

    def seat_locked(self):
        with self._lock:
            if self._expired() or self.state != 'ARMED':
                self._revoke('error')
                return False
            self.state = 'SEAT_LOCKED'
            return True

    def issue(self, command, client_pid, surface_generation, ic_epoch, purpose):
        """Single-use token for exactly the next scripted command, only while the binding is unchanged."""
        with self._lock:
            try:
                if self._expired() or self.state not in ('SEAT_LOCKED', 'INJECT') or self._token is not None:
                    self._revoke('error')
                    return None
                if not self._current_ok(client_pid, surface_generation, ic_epoch, purpose):
                    return None
                if type(command) is not str or not self._queue or command != self._queue[0]:
                    self._revoke('error')
                    return None
                self.state = 'INJECT'
                self._token = (secrets.token_hex(16), command)
                return self._token[0]
            except Exception:
                self._revoke('error')
                return None

    def send(self, token, command, client_pid, surface_generation, ic_epoch, purpose, write):
        """Re-check everything and write the fixed bytes while holding the lock; any failure revokes."""
        with self._lock:
            try:
                expected, self._token = self._token, None
                if self._expired() or self.state != 'INJECT' or expected is None:
                    self._revoke('error')
                    return False
                if (type(token) is not str or not token.isascii() or len(token) != 32
                        or not secrets.compare_digest(token, expected[0]) or type(command) is not str
                        or command != expected[1] or not self._queue or self._queue[0] != command):
                    self._revoke('error')
                    return False
                if not self._current_ok(client_pid, surface_generation, ic_epoch, purpose):
                    return False
                self._queue.pop(0)
                write(self._wire[command])
                self.sent += 1
                return True
            except Exception:
                self._revoke('error')
                return False

    def commit_receipt(self, committed, expected):
        """After typing: the addon's committed text must equal the case's expected text exactly."""
        with self._lock:
            if type(committed) is not str or type(expected) is not str or committed != expected:
                self._revoke('commit-mismatch')
                return False
            return self.state != 'REVOKED'

    def done(self):
        with self._lock:
            return self.state != 'REVOKED' and not self._queue

    def event(self, name):
        with self._lock:
            try:
                reason = name if type(name) is str and name in REVOKE_EVENTS else 'error'
            except Exception:
                reason = 'error'
            self._revoke(reason)

    def revoke(self, reason='error'):
        self.event(reason)
