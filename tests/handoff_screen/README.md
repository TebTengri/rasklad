# Cooperative handoff contract screening

This is a pure, closed model, not a compositor, Fcitx frontend, input hook or
shipping correction feature. It handles fixed symbolic targets and keys only.
It does not read devices, text, profiles, network or the display.

Hypothesis: an explicit acknowledgment binding epoch + target incarnation +
eligible grab incarnation can prevent premature dispatch during a same-target
handoff. Queue each new physical press with its captured target; preserve its
release ownership. A persistent grab must acknowledge its new binding as well.
Resource creation by itself is not an acknowledgment.

The candidate deliberately rejects target switches with queued/held input,
unmatched releases and bounded queue overflow. A successful rejection test is
NOT successful delivery. These cases remain unsupported, so this is not a
lossless or production-ready design. A missing acknowledgment cannot be waited
on indefinitely in an eventual implementation; this synchronous model does not
provide a timer, shutdown or disconnect policy.

A generic input-method commit serial is insufficient for this expanded contract.
The inspected pinned XML says it reflects the number of done events and governs
pending text-input changes. It does not explicitly bind target/grab identity or
transfer held keyboard state. The same protocol forbids further processing an
event after forwarding it to the grab holder; compositor-side replay is not a
permitted fallback. Public local source:
`tests/input_lab/protocol/input-method-unstable-v2.xml`, commit/grab_keyboard/key.
Its existing license and provenance remain with that file; no protocol body was
copied here.

Independent contract review also requires keymap/modifier coherence, target ID
incarnations, repeat cancellation, callback quiescence, destroyed-owner cleanup
and timeouts before any real-transport experiment. Those are outside this small
screen; no result here proves them or compatibility with existing IMEs.

## Measured scope

Run `python3 -m unittest discover -s tests/handoff_screen -v` and
`python3 tests/handoff_screen/run.py --run`. The latter defaults to inert without
`--run`. Twelve tests passed. Enumeration covers 11,110 command prefixes up to
length four, including continuations after terminal rejection; these are not
11,110 distinct reachable states or an unbounded correctness proof. The oracle
checks both target and grab pairing in the delivery-binding trace, queue bounds,
stale/unacknowledged dispatch and terminal trace stability.

The ten fixed aggregate scenarios contain three accepted delivery cases, six
explicit rejection cases and one missing-ack case that stays pending. Rejection must never be counted as preserved input.
A rejected held-target/grab switch leaves a delivered press without a delivered
release in its prefix; the screen makes no successful cleanup/cancellation claim.
Same-target replacement after an undelivered queued press is allowed; replacement
with an already delivered held press is rejected. Persistent same-grab/same-target
rebinding is allowed under this hypothetical contract, with a new exact ack.

Targets A/B and grabs g1/g2 are assumed to denote stable incarnations. This screen
does not implement reuse/destruction of those identities, modifier/keymap state,
real dispatch/reentrancy, or timers. No timeout/liveness/compatibility claim.
Its trace lists retain fixed symbolic history solely for inspection; they are
not production bounded telemetry or a resource/performance benchmark.

Authorship: Claude Opus5.5 produced the model draft from a short synthetic task.
Codex added held-delivery ownership, rejected silent grab transfer/early or boolean
acknowledgment, added regression tests, exact delivery bindings, private aggregate
runner and fixed result checks. An independent agent reviewed the contract and
code statically, found no concrete reachable acceptance counterexample within the
scope and identified reporting/oracle limits. Codex strengthened the oracle to
include grab identity; reviewer executed no tests.

Existing protocol/source mapping and the missing-ack gap:
`docs/HANDOFF-PROTOCOL-MAPPING-001.md`. Pending is not successful delivery.
