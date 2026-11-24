# Real Fcitx handoff controls, closed peer

The missing-press pattern is now reproduced with an actual Fcitx5.1.23 process
and the rebuilt **unmodified** upstream frontend. A cooperating synthetic server
also preserves the new pair using existing requests/events. This is no longer
just a symbolic model, but it is not a Hyprland, GUI or Slack fix.

| Same frontend and peer binary | VK presses | VK releases | Outcome |
| --- | ---: | ---: | --- |
| Original: press sent to retiring grab, release to replacement | 0 | 1 | Expected loss reproduced |
| Ack-gated: new fixed pair withheld until replacement grab + current commit | 1 | 1 | Pair preserved in closed fixture |

Both controls enforce one live server grab, actual module mapping by device/inode,
fixed seat/keymap, no unsolicited key counts, clean peer completion, and live
Fcitx during inspection. Positive control explicitly requires `waitedForAck`,
`ackMatched` and `gatedPairSent`. Thus new-grab availability by itself did not
open the positive gate. Seven launcher/report controls pass, including rejection
of matching key counts when any gate proof is absent.

## What changed

No Fcitx patch is used for either control. The peer's optional `ack-gated` mode
withholds a new synthetic Space pair before forwarding, replaces the grab after
release, and sends the pair only after a commit matching its done count. It does
not resend an event already forwarded to an IME. No new custom wire protocol.
This source/ordering hypothesis works for the pinned client and fixed scenario;
it is not a readiness promise made by every IME.

Earlier retired-input frontend patches remain diagnostics only and are not used
as the positive solution. Their raw-forward candidate was rejected independently.

## Environment and setup corrections

The ordinary execution context reports system binaries owned by root and passes
existing ABI/ownership admission. The restricted agent sandbox maps those same
owners to nobody. Earlier continuation notes incorrectly treated that view as a
machine-wide runtime blocker; read-only comparison identified the distinction.
Admission was preserved; no system file was chowned or loosened.

The narrow baseline build ran inside bwrap without display/GPU/network/input,
verified953 upstream source files against the pinned archive, and produced only
a project-local module. Initial runtime attempts found two fixture defects:
ROOT computation assumed a repository path despite `/lab/runner.py`, and D-Bus
could not resolve the private user without synthetic identity files. The fixed
fixture uses its own passwd/group/machine-id, as the established transport lab
does, and never binds host identity files or user profiles.

Diagnostics are bounded and private in ignored build/, mode0600; raw stderr is
removed before public reports. Failed protocol reports are retained rather than
collapsed into a generic JSON/pipe failure. Independent source review found the
gate-assertion gap, fixed before positive execution, and found no remaining
concrete runner blocker. Reviewer executed no code. Codex implemented this phase;
Claude's prior model informed the separate symbolic contract, not this server.

## Scope still missing

No Hyprland compositor or application is running in this fixture. No text field,
focus identity/incarnation, password field, correction recognizer, held modifier,
repeat, timeout lossless fallback or target disappearance is validated. The
positive test withholds a fixed pair; it does not implement a general event queue.
A 100ms post-release observation catches bounded duplicates, not arbitrary delay.
This result does not establish latency, performance, other-IME compatibility or
a production mechanism. The symbolic screen still rejects held-target/grab
transfers; actual runtime does not make those cases disappear.

Next: draft the smallest sender-side handoff change for the pinned compositor,
with explicit capability/scope and bounded state/held-key policy. Review first,
then test in closed reference/native fixtures before proposing any new host probe.
No old host permission is reusable. Existing installed IME/Slack/config unchanged.

## Reproduction

Build only: `python3 tools/fcitx_frontend_build.py --build`.
Build peer: `python3 tests/fcitx_drain_server/run.py --build`.
Closed controls: `python3 tests/fcitx_drain_server/run.py --run --variant baseline
--handoff-mode original` and the same with `--handoff-mode ack-gated`.
Use the ordinary admitted context so real system owners are visible; the runner
then enters its own closed namespace. Do not disable admission inside a restricted
outer namespace. Default invocation remains inert.

Evidence: `data/actual-fcitx-handoff-001-result.json` and
`data/fcitx-frontend-baseline-build-001-result.json`.
