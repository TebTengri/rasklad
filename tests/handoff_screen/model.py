# SPDX-License-Identifier: MIT
# Claude Opus5.5 draft; Codex validation/ownership corrections. Synthetic only.
from dataclasses import dataclass, field
from enum import Enum

TARGETS, KEYS, STATES = ("A", "B"), ("K", "M"), ("press", "release")
QUEUE_CAP = 4


class Outcome(Enum):
    IDLE = "idle"
    AWAITING_ACK = "awaiting_ack"
    ACTIVE = "active"
    STALE_ACK_IGNORED = "stale_ack_ignored"
    UNSUPPORTED_QUEUED_SWITCH = "unresolved_unsupported_queued_target_switch"
    UNSUPPORTED_HELD_SWITCH = "unresolved_unsupported_held_key_target_switch"
    UNSUPPORTED_TRANSFER = "unresolved_unsupported_held_state_transfer"
    REJECTED_INPUT = "rejected_duplicate_or_unmatched_input"
    OVERFLOW = "rejected_queue_overflow"


TERMINAL = {Outcome.UNSUPPORTED_QUEUED_SWITCH, Outcome.UNSUPPORTED_HELD_SWITCH,
            Outcome.UNSUPPORTED_TRANSFER, Outcome.REJECTED_INPUT, Outcome.OVERFLOW}


@dataclass
class ScreeningModel:
    """Symbolic screening model only; claims no protocol compatibility."""
    epoch: int = 0
    target: str = None
    grab: str = None
    acked: bool = False
    queue: list = field(default_factory=list)
    held: dict = field(default_factory=dict)
    trace: list = field(default_factory=list)
    delivered: dict = field(default_factory=dict)
    delivery_bindings: list = field(default_factory=list)
    state: Outcome = Outcome.IDLE

    def outcome(self):
        return self.state

    def _fail(self, why):
        self.state = why
        return why

    def offer(self, target, grab):
        if self.state in TERMINAL:
            return self.state
        if target not in TARGETS or grab not in ("g1", "g2"):
            return self._fail(Outcome.REJECTED_INPUT)
        if self.delivered and target != self.target:
            return self._fail(Outcome.UNSUPPORTED_HELD_SWITCH)
        if self.delivered and grab != self.grab:
            return self._fail(Outcome.UNSUPPORTED_TRANSFER)
        self.epoch += 1
        if self.target is not None and target != self.target and self.queue:
            return self._fail(Outcome.UNSUPPORTED_QUEUED_SWITCH)
        self.target, self.grab, self.acked = target, grab, False
        self.state = Outcome.AWAITING_ACK
        return self.epoch

    def ack(self, epoch, target, grab):
        if self.state in TERMINAL:
            return self.state
        if self.target is None or type(epoch) is not int:
            return Outcome.STALE_ACK_IGNORED
        if (epoch, target, grab) != (self.epoch, self.target, self.grab) or self.acked:
            return Outcome.STALE_ACK_IGNORED
        self.acked, self.state = True, Outcome.ACTIVE
        pending, self.queue = self.queue, []
        for key, st, owner in pending:
            if owner != self.target:
                return self._fail(Outcome.UNSUPPORTED_QUEUED_SWITCH)
            self._deliver(owner, key, st)
        return self.state

    def _deliver(self, owner, key, state):
        if state == "press":
            self.delivered[key] = (owner, self.grab)
        else:
            if self.delivered.get(key) != (owner, self.grab):
                self._fail(Outcome.UNSUPPORTED_TRANSFER)
                return
            del self.delivered[key]
        self.trace.append((owner, key, state))
        self.delivery_bindings.append((owner, self.grab, key, state))

    def key(self, key, state):
        if self.state in TERMINAL:
            return self.state
        if key not in KEYS or state not in STATES or self.target is None:
            return self._fail(Outcome.REJECTED_INPUT)
        if state == "press":
            if key in self.held:
                return self._fail(Outcome.REJECTED_INPUT)
            owner = self.target
        else:
            if key not in self.held:
                return self._fail(Outcome.REJECTED_INPUT)
            owner = self.held[key]
            if owner != self.target:
                return self._fail(Outcome.UNSUPPORTED_HELD_SWITCH)
        if not self.acked and len(self.queue) >= QUEUE_CAP:
            return self._fail(Outcome.OVERFLOW)
        if state == "press":
            self.held[key] = owner
        else:
            del self.held[key]
        if self.acked:
            self._deliver(owner, key, state)
        else:
            self.queue.append((key, state, owner))
        return self.state


def _result(m, **extra):
    return dict(outcome=m.outcome().value, trace=list(m.trace), **extra)


def run_cases():
    r = {}
    m = ScreeningModel(); e = m.offer("A", "g1")
    m.key("K", "press"); m.key("K", "release"); m.ack(e, "A", "g1")
    r["same_target_race_pair"] = _result(m)
    m = ScreeningModel(); e1 = m.offer("A", "g1"); m.key("K", "press")
    e2 = m.offer("A", "g2"); s = m.ack(e1, "A", "g1"); before = list(m.trace)
    m.ack(e2, "A", "g2"); m.key("K", "release")
    r["stale_ack"] = _result(m, stale=s.value, trace_after_stale=before)
    m = ScreeningModel(); e1 = m.offer("A", "g1"); m.ack(e1, "A", "g1")
    m.key("K", "press"); e2 = m.offer("A", "g1"); m.key("K", "release")
    s = m.ack(e1, "A", "g1"); m.ack(e2, "A", "g1")
    r["persistent_rebind"] = _result(m, stale=s.value)
    m = ScreeningModel(); m.offer("A", "g1"); m.key("K", "press"); m.offer("B", "g2")
    r["target_switch_queued_press"] = _result(m, queued_not_replayed=len(m.queue))
    m = ScreeningModel(); e = m.offer("A", "g1"); m.ack(e, "A", "g1")
    m.key("K", "press"); e = m.offer("B", "g2"); m.ack(e, "B", "g2"); m.key("K", "release")
    r["held_key_target_switch"] = _result(m)
    m = ScreeningModel(); e = m.offer("A", "g1"); m.ack(e, "A", "g1")
    m.key("K", "press"); m.offer("A", "g2")
    r["held_grab_transfer"] = _result(m)
    m = ScreeningModel(); e = m.offer("A", "g1"); m.ack(e, "A", "g1"); m.key("K", "release")
    r["unmatched_release"] = _result(m)
    m = ScreeningModel(); e = m.offer("A", "g1"); m.ack(e, "A", "g1")
    m.key("K", "press"); m.key("K", "release"); m.key("K", "release")
    r["duplicate_release"] = _result(m)
    m = ScreeningModel(); m.offer("A", "g1")
    for k, st in [("K", "press"), ("M", "press"), ("K", "release"), ("M", "release"), ("K", "press")]:
        m.key(k, st)
    r["overflow"] = _result(m, queued_not_replayed=len(m.queue))
    return r
