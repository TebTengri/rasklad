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
