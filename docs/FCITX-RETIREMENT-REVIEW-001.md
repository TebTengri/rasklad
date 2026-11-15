# Retirement review: lifetime is distinct from safe delivery

The first backend draft retained old wrappers and forwarded their physical keys
through the current persistent virtual keyboard. Independent review rejected
that forwarding rule. An existing current grab/focus does not prove which field
should receive a delayed old event. Old modifiers can describe another keymap;
press/release paths and deactivation's synthetic releases can diverge. Activation
without deactivation can also leave old repeat state running.

The same review found two reentrancy problems: an async sync callback can be
dispatched inside an older listener, and an active key/repeat callback can resume
after reentrant retirement. Merely waiting for sync does not establish callback
stack quiescence or validate that continuation's target.

## Accepted changes

The experiment consists of three ordered LGPL-derived patches:

1. `0001`: send server release once while preserving the complete client wrapper.
2. `0002`: count all wrapper listener frames with RAII, including nested dispatch.
3. `0003`: bounded list of eight retired wrappers; same-default-queue sync marks
   them drained. Collection waits for zero listener depth. Shutdown cancels
   outstanding callbacks before wrapper destruction. Retirement stops repeat.

The backend is now a **lifetime-only diagnostic**. Retired key, keymap or modifier
input exits the private process with status125. No retired key is forwarded or
converted into text. Reentrant retirement/destruction during active key/repeat
work also exits125 before handoff; this is an experimental rejection policy,
not acceptable production behavior. Constructor markers/private config further
limit accidental use; they are admission checks, not proof of namespace isolation.

`run.py --run --variant drain` refuses this diagnostic as a delivery candidate.
No runnable full-frontend build or installed replacement was produced here.
The pristine upstream tree and installed components remain unchanged.

## Evidence and limits

- Fresh pinned archive plus all three patches: three changed C++ translation
  units passed syntax-only compilation. `tools/fcitx_retirement_candidate.py
  --syntax` reproduces that check without executing frontend code.
- Intercepted ownership controls additionally exercised nested listener entry:
  inner return does not clear outer callback depth. These are not real wire,
  focus routing or backend integration tests.
- Six launcher/report admission tests passed, including rejection of the
  diagnostic as a functional drain variant.
- Independent static review confirmed the specific lifetime/retired-forwarding
  corrections and the conservative reentrant handoff gate. Reviewer ran no code.
- Claude's small independent algorithm review timed out65s and supplied no
  result. It is not counted as implementation or review. Codex authored patches.

Actual Fcitx runtime remains blocked by the existing system-file ownership
admission in this environment. No owner check or system file was changed.
No new host probe, Slack access, display/GPU access or installed IME change.

## Next autonomous work

Do not restore raw cross-generation forwarding simply to make a test green.
First inspect the sender's handoff semantics and establish an explicit event
ownership/press-release contract in a closed compositor/peer. It may require
compositor cooperation rather than a client-only lifetime change. This review
is not a proof that all client-only approaches are impossible; it identifies
the guarantees missing from the current proposal. Keep the existing keyboard
correction core separate, and retain the smallest reproducer for an upstream
interoperability report draft. No upstream publication is authorized here.
