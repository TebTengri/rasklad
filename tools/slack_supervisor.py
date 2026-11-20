"""Supervised-session state machine (pure logic; the launcher supplies `write`).

IDLE -> ARMED (F10 on a bound normal-text field) -> SEAT_LOCKED (own seat grabbed)
-> INJECT (one token per scripted command) -> REVOKED (F12, deadline, any lifecycle
change, any error). REVOKED is terminal. Every public method holds one lock, and
`send` writes while holding it, so a revoke cannot interleave with a write. Any
unexpected input or exception revokes (fail closed).
"""
import importlib.util
import math
import secrets
import threading
import time
from pathlib import Path

_spec = importlib.util.spec_from_file_location('slack_injector_fixed', Path(__file__).with_name('slack_injector.py'))
inj = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(inj)

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
    def __init__(self, slack_pids, case, now=time.monotonic, seconds=SESSION_SECONDS):
        """`case` selects a fixed script; commands and wire bytes come only from slack_injector.
        The launcher's `write` passed to send() must be non-blocking (O_NONBLOCK pipe, raise on EAGAIN
        or a short write)."""
        pids = list(slack_pids)
        if not pids or not all(type(p) is int and p > 1 for p in pids):
            raise ValueError('slack-pids')
        if type(seconds) not in (int, float) or not math.isfinite(seconds) or not 0 < seconds <= SESSION_SECONDS:
            raise ValueError('seconds')
        if not callable(now):
            raise ValueError('clock')
        try:
            commands = inj.script(case)
        except inj.Refused:
            raise ValueError('script') from None
        if not set(commands) <= inj.ALLOWED or len(commands) > 1 + 1 + inj.MAX_BACKSPACE:
            raise ValueError('script')
        start = now()
        if type(start) not in (int, float) or not math.isfinite(start):
            raise ValueError('clock')
        self._lock = threading.RLock()
        # Set without the lock by any revoke, checked right before each write.
        self._revoked = threading.Event()
        self.case = case
        self._expected = inj.EXPECTED[case]
        self._typed = len(inj.TYPED[case])
        self._receipt_ok = False
        self.attempted = 0
        self.slack_pids = frozenset(pids)
        self.now = now
        self.deadline = start + seconds
        self._queue = commands
        self._wire = {c: inj.WIRE[c] for c in inj.ALLOWED}
        self.state = 'IDLE'
        self.binding = None
        self._token = None
        self.sent = 0
        self.revoke_reason = None

    # -- internal (lock held) --
    def _expired(self):
        try:
            t = self.now()
            bad_clock = type(t) not in (int, float) or not math.isfinite(t)
        except Exception:
            bad_clock = True
        if self.state != 'REVOKED' and (bad_clock or t >= self.deadline):
            self._revoke('error' if bad_clock else 'deadline')
        return self.state == 'REVOKED'

    def _revoke(self, reason):
        self._revoked.set()
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
            try:
                if self._expired() or self.state != 'ARMED':
                    self._revoke('error')
                    return False
                self.state = 'SEAT_LOCKED'
                return True
            except Exception:
                self._revoke('error')
                return False

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
                # Cleanup only after the addon receipt matched the expected text exactly.
                if command == 'backspace' and not self._receipt_ok:
                    self._revoke('commit-mismatch')
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
                if self._revoked.is_set():
                    self._revoke('error')
                    return False
                self.attempted += 1
                write(self._wire[command])
                self.sent += 1
                return True
            except Exception:
                self._revoke('error')
                return False

    def commit_receipt(self, committed):
        """After the typing commands: the addon's committed text must equal this case's expected text."""
        with self._lock:
            if (self._expired() or self.state != 'INJECT' or self.sent != self._typed or self._receipt_ok
                    or type(committed) is not str or committed != self._expected):
                self._revoke('commit-mismatch')
                return False
            self._receipt_ok = True
            return True

    def done(self):
        with self._lock:
            return not self._expired() and not self._queue

    def event(self, name):
        self._revoked.set()  # immediately, even while a write holds the lock
        with self._lock:
            try:
                reason = name if type(name) is str and name in REVOKE_EVENTS else 'error'
            except Exception:
                reason = 'error'
            self._revoke(reason)

    def revoke(self, reason='error'):
        self.event(reason)
