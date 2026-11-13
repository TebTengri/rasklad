# INPUT-LAB-005: composition through an existing input framework

## Result

Six closed synthetic integration cases passed with the installed Fcitx5 5.1.23
and Qt 6.12 Qt Quick fields. This is a developer prototype, not a production
backend or a language-detection accuracy measurement.

| Case | Observed assertion |
| --- | --- |
| baseline | Unfinished word visible as preedit; Space commits corrected word |
| fast tail | Next letter remains preedit after correction; second Space preserves it |
| known word | Original spelling retained |
| Backspace | Deletes only owned unfinished character; corrected word then committed |
| overflow | 33-letter word preserved literally, without truncation |
| focus to password | Original unfinished word committed in old field; protected marker unchanged |

Every case also types fictional text into a password field using the synthetic
native keyboard path and verifies that it remains unchanged. Engine counters
show protected refusals; they do not prove that arbitrary applications report
sensitivity correctly. No special editor ACK, retro-delete or clipboard fallback
is involved. See [aggregate results](INPUT-LAB-005-results.json).

The previous direct Wayland composition path left pending preedit on focus loss.
The Fcitx Qt plugin path with ClientUnfocusCommit preserved the original word in
this test. The engine itself made zero commitString calls for the focus case;
the field assertions, not the daemon counter alone, establish preservation.
This does not prove the same lifecycle behavior in other clients. The direct
`compose-focus` negative control was rerun: it still fails acceptance with the
original word pending only and nonempty preedit. Thus the client assertions
continue to detect the earlier failure.

## Mechanism and scope

Own composer, existing production SafeConfirmed router, own fixed synthetic
evidence. No model/dictionary reads. Whole-word preedit avoids deleting already
committed text in these cases. Per-input-context state and explicit Preedit /
ClientUnfocusCommit capability checks replace a surface-only assumption. Unknown
capabilities pass through. Protected flags are checked before inspecting the key.
The current adapter fixes the layout to US and supports only this fixture's
plain-key workflow; shortcuts/cursor movement with pending preedit remain unsafe
or unverified and block production admission.

Qt/Fcitx handle input via a **private** D-Bus session inside bwrap. The isolated
Fcitx allowlist is keyboard, dbus, dbusfrontend and the own test addon; other
addons are disabled. HOME, runtime, passwd/group and machine-id are synthetic.
No host bus, display, input devices, GPU, clipboard, network or home directories
are provided. The private parent emits fixed XKB US keycodes through wl_keyboard,
not physical input. Timers start at client surface creation (no editor ACK).
Starting them before client initialization previously omitted initial letters;
those failed attempts are retained locally and are not counted as passing cases.

The runner retains its 10-second outer timeout, bounded output, process-group
cleanup and resource limits. Fcitx is stopped through its private remote control
before the enclosing namespace ends. Startup originally needed synthetic passwd
and machine-id entries for D-Bus; host identity files were not mounted.

These are fixed-timing experiments, not a proof against arbitrary scheduling.
The compositor itself is trusted developer code. Wayland counters do not observe
all D-Bus traffic. Ordinary application/IME processes outside this closed fixture
would have normal user permissions; the factory marker is not a sandbox.

## Validation and reproduction limits

`bin/test-composition-lab`: 10 passed, 0 failed. Core/composer C++17; optional
Fcitx adapter C++20 due to installed public headers. Native assertion client,
private parent and addon built successfully with two build jobs. No broader app
suite rerun: application runtime code has not changed in this branch.

Integration commands in the local worktree:

```
python3 build/native-field-probe/run_probe.py fcitx-baseline
python3 build/native-field-probe/run_probe.py fcitx-focus
python3 build/native-field-probe/run_probe.py fcitx-fast-tail
python3 build/native-field-probe/run_probe.py fcitx-known
python3 build/native-field-probe/run_probe.py fcitx-backspace
python3 build/native-field-probe/run_probe.py fcitx-overflow
```

This private compositor fixture is GPL-dependent and not distributed in Git;
therefore the integration experiment is not reproducible from a checkout alone.
Own launcher/config/source and attempts are preserved in the local checkpoint
archive. Public unit tests are reproducible without the private server.
Installed Fcitx public headers carry LGPL-2.1-or-later; no foreign source, binary,
dictionary or Caramba asset is vendored. Distribution review remains necessary
before bundling any dependencies. Caramba is a UX reference only.

## Next acceptance gates

Two ordinary fields, selection/cursor navigation, shortcuts/modifiers, switching
layout/IME, shutdown/crash, missing capability, focus churn/lock and process
failure must be tested. Then verify compatibility in isolated real browser,
terminal and editor clients before considering a separately authorized desktop
trial. Independent code review is still pending. No installation, autostart,
service or production hook has been enabled.

## Navigation follow-up

The owner explicitly requires correction to help rather than obstruct typing or
OS operation. See [non-interference acceptance](INPUT-NONINTERFERENCE.md).

Added ordinary second-field, Left-arrow and Shift+Left-selection cases. The
adapter now finalizes owned preedit **literally** before returning an unhandled
navigation or modified key to the editor. `Composer::finishLiteral()` does not
consult evidence, duplicate already committed overflow or mark text corrected.
The original event is not filtered; no artificial delay or replay is added.
The unit suite now has 11 passing cases, including interrupted-word preservation.

Initial follow-up attempts failed and remain in local build reports. Besides the
adapter's missing literal-finalization path, the fixture required fixes: coarse
single-shot timers were inappropriate for nearby synthetic key deadlines, and
modifier combinations needed complete key events. PreciseTimer is now used for
framework script deadlines and client assertions. All synthetic framework keys
use sendFullKeyEvent with fixed native codes and explicit modifier state; the
selection script includes Shift press/release. This follows the public
[Qt seat API](https://doc.qt.io/qt-6/qwaylandseat.html#sendFullKeyEvent).
Fixture fixes are not evidence that real desktop scheduling is safe, and early
failed attempts must not be presented as isolated proof of an adapter-only bug.

Additional assertions: Left then Space preserves original spelling and inserts
Space at the new cursor; Shift+Left actually selects the last letter, then input
replaces that selection; switching to the second normal field preserves the
first word and commits a distinct known word in the second. Every scenario also
checks unchanged fictional password input. Neither editor has custom IME logic.

These checks do not yet cover Ctrl/Super/Alt shortcuts, mouse selection, cursor
movement into an existing word, capability loss or process failure. In particular,
fragment detection after arbitrary navigation is not admitted. Pending input at
crash/disconnect and real input latency remain release blockers. The adapter
still uses synthetic evidence and a fixed US layout; it is not enabled globally.

Final follow-up matrix: **9/9 passed**, with the corrected final generator and
adapter. [Aggregate assertions](INPUT-LAB-005-navigation-results.json). This is
not a latency benchmark or independent review.

Latency/resource and private daemon disconnection follow-up: [performance report](INPUT-LAB-005-performance.md). These measurements still use synthetic evidence; they do not establish real detector cost.
