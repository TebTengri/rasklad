# Step 1–3 blueprint: real Slack in own nested Hyprland with private Fcitx

2026-11-20. Source: ecc:code-architect read-only analysis (file:line citations
in the agent report), accepted by Claude with the decisions marked DECISION.
Supersedes nothing; implements docs/SLACK-SUPERVISED-SESSION-PLAN.md.

## Composition

New `tools/slack_hyprland_session.py`, inert plan by default; thin composer
that loads existing modules.

Outer: root/host-Slack/profile/lock preflight (slack_bootstrap_probe), host
parent FD + renderD128 + binary validation (hyprland_mapping_probe
host_parent/validated_binary/drm snapshot), parameterized systemd scope
(closed_hyprland_mapping.scope_command gains a limits argument; mapping probe
keeps its 27 s values), CONNECT broker, optional secrets proxy, cleanup_scope,
own-singleton release, reap_sandboxed, fixed report.

Inner order: pop/close parent FD → Hyprland → private dbus-daemon (start_bus)
→ Fcitx with keep-grab frontend + LAB addon (native_ime_clients Fcitx start,
refactored into start_fcitx) → Relay → Slack → supervisor (step 2) → reverse
teardown.

Slack: `--ozone-platform=wayland --enable-wayland-ime
--wayland-text-input-version=3 --proxy-server=http://127.0.0.1:<port>
--disable-gpu`, no GTK_IM_MODULE. Fallback if v3 does not bind:
`--enable-features=WaylandTextInputV3` (dry-run question).

## Buses

Fcitx uses the private dbus-daemon. Slack gets its own env only:
DBUS_SESSION_BUS_ADDRESS = the filtered secrets proxy socket. DECISION:
Electron docs say Hyprland sessions use `basic_text`; the first dry run checks
the selected backend and, if basic_text, the secrets proxy is removed and Slack
gets no host bus at all.

## Paths

DECISION: the Slack child keeps HOME and the profile bind at the real home
path (proven in step 0, avoids writing foreign paths into prefs); the lab
processes keep HOME=/home/lab. Architect's /home/lab profile path rejected.

## Steps 2 and 3

Step 2: `tools/slack_supervisor.py` (pidfd, seat grab after F10, single inject
token checked per key, deadline/F12 revoke) and pure `tools/slack_injector.py`
(keycode allowlist, BackSpace by committed grapheme count) wrapping the
existing /lab/rasklad-wayland-keys helper.
Step 3: LAB addon gate build: per-commit SO_PEERCRED pid ∈ Slack tree, surface
generation, IC epoch, explicit normal-text purpose, no controls/newlines; arm
state from the supervisor over a private unix socket under /run/lab.

## Milestone 1 (offline, no owner data, no approval)

`compositor=sway` mode of the same launcher (Hyprland needs host FD/GPU →
`--run-approved` only). Private bus + private Fcitx + empty-profile Slack or
Chromium control with IME flags, offline broker. Report booleans: fcitxUp,
windowMapped, textInputV3Bound (WAYLAND_DEBUG `zwp_text_input_v3`),
safeStorage backend, scope/processes clean, singletonReleased.

## Build order and tests

1 scope parameterization (+limits test) → 2 outer + inert plan (+sandbox shape,
bus isolation, inert plan tests) → 3 inner Sway mode + dry run (+launch order,
cleanup tests) → 4 injector/supervisor (+allowlist, revoke tests) → 5 addon
gates (+binding gate, dry-run IC-reset gate tests) → 6 reviews (Grok ask mode,
ecc:cpp-reviewer, ecc:security-reviewer) → owner approval for nested Hyprland.

## Milestone 1 result (2026-11-20, offline)

`python3 -I tools/slack_bootstrap_probe.py --dry-run-ime {chromium,slack}`:
own Fcitx up on the private bus; both clients bind zwp_text_input_manager_v3
and create a text input; Chromium's autofocus field gets `enter` and
`enable()`; offline Slack gets `enter` (no field on its error page, so no
`enable()` until login). Scope parameterization done (rasklad-slack- prefix,
capped ceilings). The virtual-keyboard helper was not needed (Sway grants focus) and was removed. Open: safeStorage backend
not named in logs; LAB addon and nested Hyprland next.
Evidence: data/slack-ime-milestone1-dryrun-result.json.

### Milestone 1 review dispositions (ecc:security-reviewer, Grok ask mode)

Both confirmed: `--dry-run-ime` cannot reach the real profile or the online
broker; mapping probe limits unchanged. Fixed: scope guard now checks CPU
against the requested percent (200% Slack scope was refused by its own guard;
guard is now evaluated in tests against fake cgroup files); ceilings bound to
the unit prefix (mapping can never exceed 1536M/96/100%/27s); non-dict limits
refused and copied; inside() requires an explicit RASKLAD_BOOTSTRAP_DRY marker
for ime/diag/control paths; ime cannot combine with other dry modes; Fcitx
probe errors handled and excluded from the observe window; gdbus gets the
explicit env; text-input markers scoped to the zwp_text_input_v3 object; error
codes added. Moot: helper hash pin, helper stdin pipe (helper removed).
Deferred: tighter private-bus policy (bus is private, profile empty); safeStorage
hint tokens (dry-only, informational).

### Step 2 review dispositions (ecc:python-reviewer BLOCK, Grok FIX-FIRST)

Fixed: strict `type(x) is int/str` on binding, purpose, command, token (no
bool/float/str-subclass confusion); every exception and bad input revokes
(fail closed); one RLock around all methods and the helper write happens
inside `send` under the lock, so a revoke cannot interleave (tested with a
blocking writer); token bound to the next scripted command; fixed per-case
script including the exact cleanup BackSpace count, wire bytes from a fixed
table; binding and purpose re-checked at issue and at send; commit receipt must
equal the expected text or the session revokes (`commit-mismatch`); grapheme
count by whitelist (ASCII/Cyrillic letters, plain space); allowlist parity test
against the helper's `parse()`. Rejected: deadline from F10 instead of session
start — 600 s is the owner-accepted total session cap; expiry during typing
revokes (fail closed).

Re-review of 027f5d8 (ecc:python-reviewer FIX-FIRST; all earlier BLOCK items
verified fixed). Closed: Supervisor takes only a case name and builds the
script and wire bytes from slack_injector itself (no caller tables); a revoke
flag is set without the lock and checked right before every write, and the
launcher's write must be non-blocking (documented contract); BackSpace is
refused until the addon receipt equals the case's own expected text, only once
and only after the typing commands; seat_locked fail-closed; done() checks the
deadline; non-finite or non-numeric clock revokes; `attempted` counted before
each write. Accepted as intended: send before issue revokes the session.
Open for step 3: the launcher must keep the helper stdin private to `write`
(the helper itself still parses forbidden commands from any writer).

## Step 3: addon gates (separate artifact `tools/fcitx_slack_lab.pro`)

`RASKLAD_SLACK_SESSION_LAB` (mutually exclusive with Chromium/profile/disable
variants): program `slack` (measured via own Fcitx DebugInfo in the dry run);
scope requires `RASKLAD_SLACK_SESSION_LAB=1` plus the lab HOME/runtime;
`src/sessionarm.h` arm pipe — the supervisor holds the write end, the addon the
read end (`RASKLAD_SESSION_ARM_FD`, must be a read-only FIFO, fd ≥ 3); armed
only while the writer is open and silent; close or any byte latches off for the
process lifetime; checked in `eligible()` and immediately before every
`commitString()`; commit text must be valid UTF-8 without C0/C1, DEL or line
separators (≤ 256 bytes). Existing gates kept: protected capability flags,
single synthetic rule, focus/epoch discard. Tests: `g++ -std=c++20
-fsanitize=address,undefined tests/slack_session/sessionarm_tests.cpp`.
Pre-existing unrelated failure: tests/fcitx_transport test_runner
`test_mounts_only_fixed_project_files` assumes the checkout is not under
/home/<user>.

### Step 3 review dispositions (ecc:cpp-reviewer FIX-FIRST, Grok FIX-FIRST)

Fixed: after revoke this private Fcitx swallows every key (discard +
filterAndAccept), so no engine pass-through/compose/forward can type; an
event-loop IO watch on the arm fd discards every lab context (clears shown
preedit) at the moment the pipe closes or gets data; on a refused commit the
client still gets the empty preedit; commitAndClear reports refusal and the
Off path returns Unknown, not Submitted; arm fd re-validated by dev/ino on
every check (no fd reuse) and set CLOEXEC; commit text is an allowlist (one
ASCII/Cyrillic U+0400–U+045F word, optional single trailing space, ≤ 256
bytes); #error also excludes witness/application-off/preedit-reentry builds;
distinct artifact name raskladslacklab (own conf
tests/slack_session/raskladslacklab.conf). Contract recorded: the supervisor
holds the write end O_CLOEXEC; Fcitx gets only the read end; single-threaded
event loop. Still to verify in the composed dry run: Slack does not commit its
own preedit on focus loss; revoke HUP reaches the real Fcitx process.

Re-review of b1ee1b6 (ecc:cpp-reviewer FIX-FIRST; earlier fixes verified:
watcher lifetime, level-triggered HUP, re-entrancy, default builds
unaffected). Closed: unarmed swallows presses only, releases pass (no stuck
keys/autorepeat); revokeAll collects context references first, discards in a
second pass; a missing revoke watcher fails closed; #error also excludes the
disable-reentry test build; stopping() early return documented as unreachable.
Launcher requirement (composition step): revoke writes one byte and closes the
write end; test that the write end is absent from /proc/<fcitx>/fd.

## Offline gate test (2026-11-20, closed Sway, owned Chromium fixture)

`python3 -I tools/wayland_ime_lab.py --run --slack-gate` with the gate-test
artifact `tools/fcitx_slack_gate_test.pro` (same gates, program
`chromium-browser`, because Chromium in Sway reports no Slack program and the
policy rejects empty ids). 5/5: armed correction; preedit shown mid-word;
revoke (byte + close) clears the preedit at once; no keystroke reaches the
field afterwards; field still focused. Fcitx holds only the read end (fdinfo
access mode check). Negative control without revoke fails the two revoke
checks. Evidence: data/slack-gate-offline-001-result.json.

Review of 7a45a54 (ecc:security-reviewer FIX-FIRST). Closed: the arm write end
never leaves the supervising Python process (driver asks for revoke over
stdout, ack on stdin) and at revoke time no other process in the sandbox holds
it (all-pid fdinfo scan); the gate-test library loads from the sealed snapshot
(Library=raskladlab, extra plain bind removed); `#error` for the test-program
macro without the session build; committed control mode `--slack-gate-control`
(same steps without revoke must type: 42 input events, text appears) and the
gate run now also requires 0 input events after revoke and Fcitx alive;
`--slack-gate*` only with plain `--run`; fds closed on all driver paths.
Result: gate 6/6, control 5/5. Evidence schema 2 in
data/slack-gate-offline-001-result.json.

Supervised run: `--slack-supervised` — the real `Supervisor('correction')`
owns the helper stdin (non-blocking) and the arm write end; the driver only
observes. 9 commands sent (word, space, 7 BackSpace); receipt "привет "
matched before cleanup; field empty after; F12 revokes, next command refused,
nothing reaches the field; no other process held the helper stdin or the arm
write end. Remaining before live: compositor-sourced binding and seat grab in
nested Hyprland, then the owner-approved session.

Grok review of 7e4aeda (FIX-FIRST; confirmed no key path outside
Supervisor.send). Closed: helper stdin closed and helper stopped on F12 and on
every exit path; `supervisedScriptCompleted` requires sent == script length and
an explicit receipt step; post-revoke check also requires the helper stdin
closed and the helper gone; non-blocking write waits ≤ 1 s for room, then
refuses; observer evaluation bounded to 2 s. LIVE REQUIREMENTS (not lab): the
commit receipt must come from the addon/compositor, never from an observer; the
binding (pid, surface generation, IC epoch, purpose) must be compositor-issued.

## Addon commit receipt (2026-11-20)

`ReceiptSink` (src/sessionarm.h): after every commit the Slack-session addon
sends one 4-byte SEQPACKET record (now 6 bytes with a sequence, see below) — `c07\n` (rule correction, 7 code points)
or `o06\n` (original) — never the text; MSG_NOSIGNAL|MSG_DONTWAIT; any failure
latches the arm gate off. The addon requires a valid receipt socket to activate.
`Supervisor.commit_receipt(record)` accepts only the exact expected bytes from
`slack_injector.receipt(case)`; the observer-text receipt is gone. Offline:
supervised, gate and control modes all pass; cleanup waited for `c07`.

Grok review of 05e5fa2 (FIX-FIRST; confirmed no text on the receipt wire).
Closed: records carry a 1-based commit sequence (`c0701\n`), so a stale or
replayed record cannot match; the supervisor requires an empty receipt queue
before typing, exactly one record before cleanup and none after; a mismatched
or orphaned commit revokes the session instead of cleaning up blindly; the
addon takes the receipt end (CLOEXEC) when it loads, not on first commit.
Accepted: an exhausted queue (EAGAIN) latches the gate permanently — fail
closed. Trust boundary: the addon is trusted; the record proves "the addon
committed this kind and length", not the field contents.

## Nested Hyprland supervised run (2026-11-20, owner-approved, one shot)

`tools/hyprland_ime_lab.py --run-approved --slack-supervised` (commit 4e771bf):
the Supervisor binds to the compositor's focused window (`hyprctl activewindow`:
pid + window address, checked to descend from the fixture Chromium) and re-reads
it before every key; 9 keys sent; cleanup after addon receipt `c0701`; F12
revokes; a changed window generation is refused; Hyprland and Fcitx stay alive.
PASS. Evidence: data/hyprland-slack-supervised-001-result.json.
Still open before live Slack: host-keyboard isolation while injecting (seat
grab), IC epoch and content purpose from Fcitx instead of lab values.

## Field context from the addon (2026-11-20)

The addon reports every lifecycle event of the lab client's contexts as a
6-byte record: event (`i` focus-in, `u` focus-out, `r` reset, `k` capability
change, `s` input-method switch), purpose class and context id — never text.
Purpose is fail-closed: `n`/`a` only for a preedit-capable field without
Password/Sensitive/NoSpellCheck/Terminal/Disable/Email/Digit/Url/Dialable/
Number/Name; otherwise `p` or `x`. The supervisor binds to the last focus-in
record and revokes (and stops the keyboard helper) on any later context record;
an undeliverable record latches the gate and hangs up the socket, so the
supervisor stops at its next read. Offline: focusing the password field is
reported (`p`) and the live check alone refuses the next key.

Grok review of ccb9b09 (FIX-FIRST) closed: fail-open purpose default; supervisor
blind to a failed send (now hang-up → stop); missing records for input-method
switch and capability-about-to-change; refusal proven via the live check, not a
manual revoke; hyprctl failure revokes explicitly. Wording: the record id is a
context label, not the epoch — any record after arming revokes, so wrap-around
cannot alias. RESIDUAL (documented, not closed): a key already written to the
helper before a focus change can still be delivered to the newly focused
field; the window is one key (≤ one 50 ms step). Closing it needs the seat
grab in the compositor — the next owner-approved Hyprland step. Not yet tested:
a focus change in the middle of the script.

## Seat probe in nested Hyprland (2026-11-20, owner-approved)

Measurement, not a fix (data/hyprland-seat-probe-001-result.json). A catch-all
submap (`bind = , catchall, submap, rasklad_lock`, F12 leaves) blocks every
keyboard, virtual ones included: supervisor helper and stray helper 20 → 0
input events; `submap reset` restores 20. So keybinds run before the IM grab,
and a plain submap would also block the injection. Hyprland names devices
`wl_keyboard` (physical seat), `hl-virtual-keyboard-fcitx5`,
`hl-virtual-keyboard-unknown[-N]` (helpers). Candidate isolation: submap +
per-device `keybinds = false` for the supervisor helper only — to be measured
next. The first approved run did not measure (seat_probe not forwarded; fixed
in 1b1fa9b with an AST test).

Seat probe 2 (owner-approved, data/hyprland-seat-probe-002-result.json):
catch-all submap + `keybinds = false` for the supervisor helper device only —
helper 20 events under lock, stray keyboard 0. The same exemption applied at
runtime via `hyprctl keyword device[...]:keybinds false` takes effect at once
(stray 0 → 20). Design: on arm the supervisor (1) lists devices and requires
exactly one `hl-virtual-keyboard-unknown*` — its own, spawned just before —
(2) sets keybinds=false for it only, (3) enters the lock submap (F12 leaves);
on revoke: submap reset, exemption removed, helper destroyed. Physical
`wl_keyboard` is not measured directly (no host input by policy).
Name-keyed exemption is the weak point: any other virtual keyboard created
with the exempt name would pass — hence the device count check before arming
and on every key.

Grok review of fb4d883 (seat lock, FIX-FIRST). Closed: the exemption is bound
to the live device (name + address, re-listed after the keyword; a recreated
device with the same name breaks `intact()`); every revoke runs exemption off →
helper gone → addon arm closed → submap off; `lock()` unwinds on any failure;
`unlock()` keeps state until each compositor step answers ok; focus-unchanged
is required again. Unit tests with a fake hyprctl cover these paths.
ACCEPTED RESIDUALS (documented): F12 in the lock submap is the owner's kill
switch, so any non-exempt keyboard can also leave the submap; the supervisor
sees it at the next key (≤ one key step) and revokes. Pointer input is not
locked: a click that moves focus is caught by the addon focus records and
compositor binding, a click inside the same field is not. Both need a fixed
owner rule for the live run: no mouse use after arming.

Nested Hyprland supervised run 002 with the seat lock (owner-approved, commit
70f7eff): PASS — data/hyprland-slack-supervised-002-result.json. All offline
and nested-compositor prerequisites for the live Slack session are now
measured; remaining before live: the Slack launch inside this harness (real
profile, bridge) and the owner rules (no mouse after arming, three owner
booleans after the run).

## Slack launch trial in nested Hyprland (prepared 2026-11-20, not run)

`python3 -I tools/hyprland_ime_lab.py --run-approved --slack-launch`
(tools/slack_launch_lab.py). Needs its own one-shot owner approval; host Slack
must be closed. Same outer preconditions as step 0 (host-Slack/profile/bus
checks repeated right before the sandbox, secrets-only xdg-dbus-proxy, online
CONNECT broker, step-0 rlimits), inside the nested Hyprland lab with the
Slack-session addon (`build/chromium-ime/slack-session-addon`, program
`slack`). The arm pipe is written and closed before Slack starts, so the addon
never intercepts a key; no helper keyboard, no driver, no injection.
Slack's env is built from scratch (owner HOME/user, `/run/lab/slack-bus`,
proxy); lab processes keep HOME=/home/lab.
Observation 90 s: window mapped by a Slack-tree PID (hyprctl; class only, never
titles), foreign windows, Fcitx program names, addon context records (event +
purpose). During it the owner may do the zero-injection check
composer → search → composer with the mouse, without typing. Pass: window
mapped, no commit record, no foreign window, graceful stop, own singleton
released or absent, no leftover Slack process, no SingletonLock, bridge
cleanup complete.

### Launch trial review dispositions (ecc:security-reviewer FIX-FIRST, Grok FIX-FIRST)

Accepted and fixed: `finish()` runs every cleanup step and never raises
(errors recorded as `finishErrors`, which fail `outerClean`); snapshot FDs and
the work directory are closed/removed in a nested finally; pass now requires
`addonSawSlack` (at least one context record, so the addon loaded and Slack's
program matched) and `relayRefused == 0`; broker deadline 170 s (bound raised
to 180) so it outlives Hyprland/Fcitx start + observe + quit; relay, records
and child env set up inside the try so receipts/relay always close; the own
singleton is released whenever our Slack main process is dead (also after a
kill and on the exception path), still only if the lock names its
in-namespace PID; the latch flag is renamed `armLatchWrittenBeforeLaunch`.
Rejected: domain names in the report (only the fixed public-suffix allowlist,
everything else `other`, step-0 disposition); `ownerRecovery` is a fixed
constant, not data; an addon latch ACK (write + close is unconditional and the
addon re-polls on every key; commit records must stay 0); lab machine-id and
passwd for Slack (step-0 decision: Slack sees the host identity).
Residual: Slack runs as the same uid in the same mount namespace as the lab, so
it could reach `/run/lab` sockets (private Fcitx bus, hyprctl). Accepted for a
no-input trial of the owner's own Slack; revisit before stage 2.

### Launch trial results (owner-approved, one shot each)

001 (data/hyprland-slack-launch-001-result.json): window mapped in 2.2 s
(class `slack`), Fcitx program `slack`, addon records delivered, 0 commits,
clean stop and profile; failed only on relayRefused=4 (concurrency 16 → 32).
The owner did not click, so the focus check was not done.
002 (data/hyprland-slack-launch-002-result.json): PASS. Each mouse move
composer → search → composer produced focus-out, capability and focus-in
records (context id 1 throughout: one text-input per window). Composer and
search both report purpose `normal`, so purpose cannot distinguish them: the
supervisor's revoke on any context record is the binding guard, and the
zero-injection IC-reset gate is satisfied. Before 001 the owner's host Slack
was still running in the tray; it was stopped with SIGTERM and its stale
SingletonLock (dead PID, verified) removed with owner approval.

## Live session mode (prepared 2026-11-21, not run)

`python3 -I tools/hyprland_ime_lab.py --run-approved --slack-launch --slack-live`
(tools/slack_launch_lab.py `run_live_inside`). Before Slack starts, the keys
helper is created and HyprSeat locks the seat (only the helper is exempt), so
the owner's keyboards reach only F10 (go) and F12 (kill). The owner opens the
test channel and clicks an EMPTY composer with the mouse, then presses F10
(wait ≤180 s). Binding = compositor active window (Slack tree) + last addon
focus-in (normal). Two fixed cases under fresh Supervisors: correction
(`ghbdtn`+space → `привет `, receipt c0701) and English (`hello `, receipt
o0602, addon commit sequence 2); BackSpace × code points only after an exact
receipt. No Enter exists. After revoke the owner looks 10 s, then Slack quits.
Owner rules: empty composer, no selection, hands off mouse and keyboard after
F10; afterwards three booleans (nothing sent, no new message, composer empty).

### Live mode review dispositions (ecc:security-reviewer FIX-FIRST, Grok FIX-FIRST)
Accepted and fixed: F12 enters a swallow-all `rasklad_dead` submap instead of
`reset` (only unlock() resets, after helper stop and arm latch); after F10 the
mouse buttons 272/273/274 are bound (consumed) in `rasklad_go` and
`rasklad_dead` (Hyprland binds consume unless `non_consuming`; not yet
measured); seat is re-checked every 0.1 s while committed text waits for the
receipt; revoke_all runs every step even if one raises, retries the exemption
release, records `revokeErrors` (fail the run); foreign windows checked every
wait pass; wait 180 s, relay 280 s, broker 295 s, outer 320 s, CPU 300 s;
`textMayRemain` reported when a case was revoked after typing.
Rejected: composer-vs-search proof (both are purpose normal; covered by the
owner procedure, revoke on any context record and swallowed clicks); receipt
bound to IC id (all records in a Slack window carry one id); Supervisor.send
rewind (helper writes are < PIPE_BUF and length-checked; a failed write
revokes without cleanup and is reported as textMayRemain).
Residuals: mouse swallowing and the dead submap are not measured yet; the
owner's 10 s look happens with the seat already released (do not type).

### Live run 001 and fix (2026-11-21)
Run 001 (data/hyprland-slack-live-001-result.json) aborted right after Slack
started, before F10, with `seat_lost_before_arm`; the owner pressed nothing.
No key was injected, Slack stopped cleanly, profile clean. The keyboard list
changed while Slack started (cause recorded from the next run on:
`seatLostDetail`, `keyboardsAddedBeforeArm`). Fix: before F10 only, a newly
added keyboard whose name does not start with the helper prefix joins the
expected set (it is not exempt, so the catch-all swallows it; measured with a
stray keyboard); a change to the helper's (name, address), a removal or any
prefix-named addition still aborts; after F10 any change revokes. Grok: OK.
The partial live report is now kept on refusal.

### Live run 002 (2026-11-21, owner-approved, consumed)
data/hyprland-slack-live-002-result.json. First injection in the owner's test
channel: the correction case completed (addon receipt c0701, 7 BackSpace, no
extra commit). The English case was revoked with commit-mismatch: after the
BackSpaces the addon bypasses until a boundary (by design: never correct a
suffix of a passed-through token), so `hello ` passed raw and no receipt came;
no cleanup, nothing sent. The owner reported the composer already held a draft
before the run and cleared the composer by hand afterwards.
Fixes: the English case types a raw boundary space first (cleanup 7 =
space + committed `hello `, receipt o0602); offline closed-Sway Chromium run
with both cases back to back: PASS. The addon now sends a field record
(`f` + e/t/u + id: empty / has text / unknown, from the surrounding text the
client already gives Fcitx; never the text) on every lifecycle event and on
SurroundingTextUpdated; field records never revoke. Arm gate: `text` refuses,
no record refuses, `unknown` (the Chromium fixture sends no surrounding text)
only with `--owner-empty-composer`; after a case `text` fails the run, and if
the field was visible at arm it must be empty again. Grok FIX-FIRST items 2–3
fixed; 1 and 4 (extra BackSpace if Slack drops the raw space; a stale `e`)
are bounded by the empty-composer rule and recorded as residuals.

### Live runs 003–005 (2026-11-21, each owner-approved, consumed; no key injected)
003: Fcitx removed its own virtual keyboard before F10 → non-helper removals
are accepted before F10. 004: the owner could not find the test channel
(keyboard search is blocked by design) → owner opens the channel in host Slack
first; Slack reopens on it. 005: Slack sends surrounding text (field state is
known, not `unknown`), but the empty composer was reported as `text` →
`blankText()` treats whitespace / zero-width-only surrounding text as empty;
malformed UTF-8 counts as content.

### Live run 006 (2026-11-21, owner-approved, consumed): PASS
data/hyprland-slack-live-006-result.json. First complete supervised session
in the owner's test channel, without the owner flag: the addon reported the
composer empty at arm; correction (c0701) and English (o0602) committed and
cleaned, addon field empty after each case, no Enter, seat released, Slack
stopped gracefully, profile clean, only slackb.com blocked. Owner booleans:
nothing sent, no new message in the channel, composer empty (all true).
Follow-up fixed in code (not yet run live): Slack sent a context record after
case-1 cleanup, which was consumed as the end of the case; before the next
case the binding is now re-derived from the newest addon record (same window
and context id, normal purpose, not a focus-out, field empty, seat intact),
otherwise the session stops.
