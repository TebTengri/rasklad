# Supervised real-Slack correction session — design (NOT implemented, NOT run)

Status 2026-11-20: design for critique and owner decision. No host, GPU, Slack,
profile, network or IME action is authorized by this document.

## Goal

One bounded supervised session proves end-to-end synthetic correction
(`ghbdtn ` → `привет `) in the owner-designated test-channel editor of the
real authenticated Slack, with fail-closed behavior on every lifecycle change.
Success does not enable Slack or any application for ordinary use.

## Route (recommended: A)

A. Real Slack inside an OWN nested Hyprland with a PRIVATE Fcitx instance that
   loads the rasklad LAB addon. Host IME, host Fcitx config and host session
   are untouched. Reuses proven pieces:
   - nested Hyprland on host parent (direct mapping trial001 PASS, exact011);
   - private Fcitx + LGPL keep-grab frontend + LAB addon (closed Sway 18+focus4,
     one nested Hyprland PASS);
   - existing epoch/lease watchers (FCITX-FIELD-SEAM-001), shared Off/On,
     default-deny ApplicationPermissions;
   - Slack egress only through tools/slack_https_bridge.py (CONNECT + SNI
     allowlist), no general network namespace sharing.
B. Load the addon into the host Fcitx of the real session. Rejected for the
   first session: it changes the desktop IME and exposes every host window.

## Identity and field binding

- App identity = process tree spawned by our launcher (pidfd held for the whole
  session) AND Hyprland window mapping of a client whose PID is in that tree.
  Declared app-id/title are never authority.
- Field binding is granted only by the owner: owner clicks into the empty
  test-channel editor, then presses the existing LAB arm key (F10). Binding =
  (Fcitx IC id, epoch, stable window id, launcher pidfd). One binding at a time.
- Revoke immediately, without callbacks, on focus-out, reset, IM switch,
  capability change, window close/recreate, pid exit, LAB Off (F12) or deadline.
- Password/sensitive/no-predict capabilities deny even with an active binding.

## Synthetic input

- Only a private virtual keyboard inside the nested compositor. Fixed script of
  fictional words. Keycode allowlist: letters, Space, BackSpace, Ctrl+Z for undo.
  Return, KP_Enter, Tab, Escape and all Ctrl/Alt/Super combos except Ctrl+Z are
  rejected by the injector itself, so nothing can be sent.
- Script cases: corrected word; English word unchanged; undo restores original;
  focus to another Slack field mid-word → no correction; Off/On → no stale
  correction; deadline expiry → clean revoke.
- Physical input can reach the nested window: owner only clicks and presses
  F10/F12 when prompted, never types text.
- Verification without reading Slack text: addon receipts/counters plus owner
  visual confirmation per case (fixed yes/no prompts). No field text readback,
  no accessibility tree, no logging of text.
- End: injector clears the editor (select-all is NOT used; BackSpace count equal
  to injected length), owner confirms the field is empty.

## Side effects the owner accepts explicitly

- Host Slack must be closed first (owner closes it, or the existing approved
  pidfd quit helper); real profile is used in place, never copied.
- Slack may show a typing indicator to test-channel members and may sync a
  transient draft of fictional text to its servers.
- Shared GPU driver risk (renderD128) as in previous nested trials.

## Limits

One run, no retries. Session deadline 10 min, unique systemd scope with memory,
tasks and CPU limits sized for Slack (to be measured offline first), inactive
scope verified at end. Bridge allowlist and byte counters only; no URLs, cookies
or payloads persisted. Results: fixed booleans/counts in data/, no channel id.

## Implementation pieces (after owner accepts the route)

1. Nested launcher variant: reuse hyprland_mapping_probe sandbox/scope, add
   private Fcitx start and Slack spawn with pidfd; inert default prints plan.
2. Binding gate in the LAB addon: owner-arm + pidfd/window check.
3. Injector with keycode allowlist and fixed script; unit tests for rejection.
4. Offline dry-run on closed Sway with the native Chromium fixture in place of
   Slack (same launcher, same gate, same injector).
5. Independent review (Grok security, Gemini simplification), then one-shot
   owner approval.

## Acceptance

All script cases pass with owner confirmation; zero Enter/Tab/send events
emitted; every revoke trigger observed at least once in the dry run; host
IME/config unchanged; scope inactive; Slack exits cleanly and host Slack starts
normally afterwards.

## Independent critique 2026-11-20 and dispositions

Grok (cursor-agent, grok-4.7-high, plan mode, this document only): verdict
PROCEED-WITH-FIXES, 4 blockers. Gemini (agy, gemini-3.8-flash-high, plan mode):
simplification. A Grok review of PR #14 timed out and is not credited; Codex
hit its usage limit (until 2026-11-22) and gave no review.

Accepted (design changes, supersede the sections above where they conflict):
- Seat: after F10 the supervisor grabs the nested seat; physical keys/pointer
  never reach Slack except F10/F12; owner makes no clicks after arming.
- Supervisor owns the only inject token, checked before every key. Revoke
  destroys the virtual keyboard, discards preedit without commit, drops queued
  commits, then ends the scope. Deadline and F12 live in the supervisor.
- Binding = SO_PEERCRED pidfd of the connected Wayland client + monotonic
  surface generation + IC epoch, checked on every commit. Any new toplevel,
  popup, focus change or IC reset drops it; a new F10 is required. Arm binds the
  surface focused at F10 time, after the purpose check and owner confirmation.
- Commit only for an explicit normal-text purpose; unknown/empty/password/PIN/
  sensitive/digits/no-predict deny on every commit.
- Addon rejects commits containing controls or newlines and never issues
  text-input enter/submit. Ctrl+Z is removed from the live script.
- Environment scrubbed: no xdg-open, host D-Bus, portals or host browser.
- Network: Slack in a netns where only TCP to the bridge is possible; UDP/QUIC,
  raw IPv6 and host unix sockets denied. Bridge is an egress restriction, not a
  send filter.
- Cleanup: BackSpace by committed grapheme count; KillMode=control-group and
  zero remaining PIDs; host Fcitx config hash unchanged; supervisor releases the
  grab if Fcitx or Hyprland exits.
- Acceptance adds three owner booleans: no send control activated, no new
  channel message, editor empty. Any "no" fails the run; the draft is cleared
  manually before host Slack reopens.
- Side-effect list adds presence, mark-as-read, notification clearing,
  untrusted workspace content rendering, keyring prompts, token refresh and
  profile (LevelDB) corruption risk.
- Hard gate (Grok): in the closed dry run, composer→search, composer→edit and
  composer→thread must each produce an IC reset or purpose change. If not,
  stop: the live run is not attempted.
- Gemini: extend hyprland_mapping_probe with flags instead of a new launcher;
  static scope limits; live script reduced to correction + English unchanged +
  one focus revoke; undo, Off/On and deadline stay offline.

Rejected: Gemini "drop the bridge" (Grok shows egress is a real risk); "drop
window/PID binding" (Grok shows it is insufficient, not excessive); ephemeral
user-data-dir (loses authentication; profile copying is outside the owner's
earlier scope); dry run in nested Hyprland (each needs host GPU approval;
closed Sway stays the dry-run target).

Owner decisions 2026-11-20:
1. No profile snapshot. The real profile is used in place; the owner accepts
   the profile-corruption risk. Grok's snapshot fix is declined by the owner.
2. Desktop Slack (Electron) in the owner-designated real test channel; the
   Slack Web alternative is not pursued for the first session.
3. Grok's IC-reset gate is also checked live as a zero-injection pre-check:
   after launch the owner moves composer→search→composer; if no IC reset or
   purpose change is observed, the run aborts before any key is injected.
These decisions are not approval to run; the live run needs a separate
one-shot approval after the dry run and review.

## Documentation findings 2026-11-20 (Context7 via ecc:docs-lookup)

- Electron 38+: `--ozone-platform` defaults to `auto` (native Wayland when
  XDG_SESSION_TYPE=wayland); it must be on the command line. Wayland IME flags
  are not documented in Context7; use the flags already proven in our Chromium
  fixture: `--enable-wayland-ime --wayland-text-input-version=3`
  (tests/chromium_ime/driver.mjs).
- safeStorage on Linux selects the backend from the desktop session;
  unrecognised desktops (Hyprland) get `basic_text` unless `--password-store`
  is given. Host Slack is launched without flags, so it most likely does not use
  the keyring at all. Candidate simplification: drop the Secret Service proxy
  (no host D-Bus in the sandbox) after confirming the selected backend in a
  dry run; this also resolves Grok's keyring-exposure finding.
- Chromium ProcessSingleton internals are not documented in Context7; our
  observed failure mode (lock `<host>-<namespace pid>` surviving exit) is
  handled by the probe's own-lock release.

## Consolidated current design (2026-11-20, supersedes conflicting text above)

Gemini (gemini-3.8-flash-high, design-only prompt) critique dispositions:
accepted — one supervisor process that also injects (allowlist stays a pure
module); linear states IDLE → ARMED(F10) → SEAT_LOCKED → INJECT →
REVOKED/CLEANUP (F12, deadline, any lifecycle change); identity inside the
nested compositor = every connected client is ours or in the Slack process
tree (no window-mapping authority needed); live script reduced to two cases
(correction, English unchanged) because focus revocation is proven by the
dry-run IC-reset gate; contradictions removed. Rejected — ephemeral profile
(owner chose the real profile; login requires it).

Binding rules in force: no clicks or typing by the owner after F10; no Ctrl+Z
live; BackSpace by committed grapheme count; protected purposes deny; commits
with controls/newlines rejected; Slack sees no host bus if the dry run confirms
`basic_text`. Implementation follows docs/research/SLACK-SESSION-BLUEPRINT.md.
