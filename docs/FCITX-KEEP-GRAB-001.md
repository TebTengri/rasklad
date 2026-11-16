# Keep one keyboard grab: closed client experiment

The smaller client-side alternative works in the tested closed chain without
changing the compositor. It is an experimental Fcitx frontend, not an installed
keyboard service or completed Hyprland/Slack support.

## Why test this alternative

Sender-side acknowledgment preserved a fixed pair, but a private compositor fork
would impose substantial maintenance on a small layout app. Independent review
recommended testing grab churn directly. Input-method-v2 describes a seat-wide
exclusive grab; deactivation changes text-input state and does not itself require
destroying the grab resource. This supports a hypothesis, not universal behavior.

Do not require permanent press/release ownership by one field: Wayland focus,
leave/enter and cancellation/reset semantics matter. The earlier symbolic screen
used a deliberately conservative rejection rule. Its unsupported cases were not
a proof that normal keyboard transitions are impossible.

## Actual peer evidence

Both sides use the same peer and runner binaries. `grab-ab` emits press on the
original grab immediately after deactivate/done + activate/done; only release
waits for the current matching commit, then uses the current grab. Press is never
buffered or replayed.

| Control | VK press/release | Grab creation/release | Meaning |
| --- | --- | --- | --- |
| Unmodified Fcitx, same peer | 0/1 | 2/1 | Original loss reproduced |
| Keep-grab candidate | 1/1 | 1/0 | Pair preserved without sender delaying press |
| Inactive default guard | 0/0; exit125 | 1/0 | Explicit rejection, not delivery |
| Optional inactive raw path, 350ms hold | 1/1 | 1/0 | No extra IME repeat within observation; reactivation acknowledged |
| Uncancelled client control, held key crosses deactivate | 1/2 | 1/0 | Introduced duplicate release reproduced, expected failure |
| Cancellation patch, release while inactive | 1/1 | 1/0 | Known cleanup cancellation normalized |
| Cancellation patch, release after reactivation | 1/1 | 1/0 | Same normalization before Core handling |
| Fresh press after cancellation | 2/2 | 1/0 | Stale cancellation does not consume new cycle |
| Unrecorded physical release | 0/1 | 1/0 | Unknown releases remain forwarded |

All tested modules were identified by device/inode before probe emission. The
uncancelled control is separately reproducible with builder variant
`keep-grab-control`; it is intentionally defective and not a candidate solution.
Only public-source hashes and fixed synthetic aggregates are published. Evidence:
`data/fcitx-keep-grab-001-result.json`.

## Candidate behavior and scope

`keep-grab-lab.patch` retains the complete grab and signal handlers across
activation changes, preserves repeat metadata and stops client repeat at handoff.
Active input still uses the original Core path. Optional inactive physical keys
use the same current seat grab and ready persistent VK directly, then return
before Core, focus wrappers, preedit or text conversion. This is not delivery
from a retired wrapper or textual replay. Invalid prerequisites reject the lab.
Keymap notifications first invalidate VK readiness; unavailable rejects the lab.

`keep-grab-cancellation.patch` records only key releases actually marshalled to VK
by deactivation cleanup, after the marshal. It consumes that key's later physical
release before either input path, and clears the record on fresh press. Unknown
releases survive. Records are limited to64 and scoped to the context/retained
resources; a keymap event while cancellation is pending rejects the lab. Tracking
inherits the upstream repeatable-key set; it does not prove every-key cancellation.

Both standalone patches modify LGPL-2.1-or-later Fcitx5.1.23 code, retain upstream
notices, and are separate from the older retirement-diagnostic patches. Builder
verifies953 original source files, applies pinned patches only to a temporary
copy, mounts that copy read-only, and never installs the module. Module startup
requires explicit private markers, fixed lab HOME/runtime/display and persistent
VK/raw-key config. No regular-session use is authorized by these markers alone.

## Closed native application preflight

Software-only Sway + native Wayland Chromium + actual Fcitx + rasklad addon:
unmodified rebuilt baseline and normalized keep-grab each pass18 checks plus
all4 owned DOM focus scenarios. Password passthrough, correction, native Undo,
focus isolation and input after killing only the private Fcitx process pass.
Module mapping and artifact hashes are recorded; CDP is not used for typing.

Observed Fcitx RSS was57,848KiB baseline and57,796KiB candidate; CPU0.19s and0.20s
for these finite runs. These are whole-daemon observations, not idle/long-run
benchmarks, latency guarantees or proof of a percentage performance difference.
Compositor/browser resources are separate. Evidence:
`data/native-private-frontend-001-result.json`.

Selected persistent-VK setup initially crashed Chromium before typing twice.
System-default setup passed. Establishing a silent synthetic seat/keymap before
starting selected Fcitx allowed the selected tests to progress and complete.
The seed sends no keys; the actual driver retains its own input helper. This is
a controlled setup dependency, not a symbolized explanation of Chrome internals.
RLIMIT_CORE=0; no unrelated core/journal was read. Private stderr remained ignored
and was stripped from public output. No DRM, sysfs or host bus access was added.
The one failure-only diagnostic line changed the driver hash; its source review
was explicitly refreshed after inspection, without changing assertions/events.

## Remaining release gates

No installed Hyprland or live Slack trial has run with this candidate. The Sway
focus tests are controlled DOM transitions within an owned surface, not arbitrary
application/window identity. Real seat/modifier/keymap replacement, shortcuts,
feedback-loop avoidance, long typing, startup without a keyboard and all crash /
cancellation cases remain open. Finite checks do not establish universal safety.

The exact nested Hyprland runner now has a reviewed, inert optional selector;
105 pure guard tests pass. Its parent FD/render-node/75-second/private-profile
bounds remain unchanged. A live run still needs a fresh scoped permission;
previous approvals were consumed. No automatic fallback or host install.

Authorship: Claude Opus5.5 proposed retained grab/listener/repeat changes; Codex
implemented/hardened the source patches, raw path, cancellation and peer/build
controls. A fresh helper implemented selected frontend admission/native fixture
plumbing and tests, reviewed by Codex. Independent agents criticized architecture,
reviewed patch and exact-runner boundaries; they did not execute runtime tests.

## Modifier notification control

A subsequent closed retained-grab probe preserves Shift's keymap-derived mask
across deactivate/reactivate: both Space events reached VK while Shift was set;
the later physical modifier notification returned VK state to zero. Exactly1/1,
onegrab/zero releases, matching commit and explicit wait are required. Eleven
peer-launcher controls pass. This tests modifier notifications, not a physical
Shift key pair, arbitrary keyboard/keymap replacement or native shortcut behavior.
Evidence: `data/fcitx-keep-grab-modifier-001-result.json`. Implemented by a fresh
helper, independently inspected and executed by Codex; helper ran no Fcitx.
