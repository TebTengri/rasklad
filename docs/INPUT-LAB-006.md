# INPUT-LAB-006: Save/Close boundary and an in-process candidate

## Finding that blocks universal composition input

A visible unfinished word is not necessarily part of an editor's document.
Using ordinary Qt Quick fields and an application-level standard Save shortcut,
we captured the plain-text document at Save and in the window's closing signal.
The Save action only reads a snapshot: it does not call inputMethod.commit,
change focus or modify the editor, and writes no text to disk. For Close, the
fixture keeps the process/model alive only to inspect post-close state.

| External composition case | Result |
| --- | --- |
| Fcitx keyboard-us baseline, Save | PASS, document contains original word |
| Own active composition addon, Save | PASS |
| Own addon, daemon killed, Save | **FAIL**, Save snapshot empty, original pending |
| Fcitx keyboard-us baseline, Close | PASS |
| Own active composition addon, Close | **FAIL**, closing snapshot empty, original still pending after window closes |
| Own addon, daemon killed, Close | **FAIL**, same pending/empty document result |

This is an observed document-snapshot failure, not an observed loss of an actual
saved file or user data. No real text/files were used. It is sufficient to block
universal deployment under the requirement that rasklad must not impede users.
Passing earlier continuation/focus tests did not establish Save/Close safety.
The failure is retained; the editor is not patched to make the old path pass.

Four additional controlled ordering cases passed: graceful exit / SIGKILL,
combined with X sent before the stop request or X+Space sent as a burst at the
first directory notification after the launcher reaped its private daemon.
Original text, suffix and password assertions passed. No extra waiting period or
editor ACK is used after process exit. These cases bracket specific orderings;
they do not prove arbitrary in-flight scheduling safe. In the before case, X may
already have been processed before the daemon dies. The after case starts after
process exit, not at an exact physical-key/disconnect simultaneity.

## Separate in-process candidate

An own developer-only adapter inside the client leaves ordinary letters committed
from the start. On Space it validates the current approved field and snapshot,
runs the existing router with synthetic evidence and applies replacement plus
Space synchronously in that client's GUI dispatch. Subsequent queued native input
is handled afterward. This removes the external pending-word dependency in these
fixed fields; it is a different integration mechanism, not a fix to the external
IME's lifecycle or proof about arbitrary applications.

The Save/Close observers and ordinary field implementations are unchanged between
conditions. Only this explicitly installed client component changes the correction
path. No preedit or word history is kept by it. Unknown/protected fields, modifiers,
selection, cursor fragments and unavailable evidence produce ordinary native input.
Hints and focus are rechecked before a second text query and application.

Closed synthetic client-adapter cases: **6/6 PASS**:

- Native letters are already committed before Space; Space corrects the word.
- The next letter sent immediately after Space is preserved; a second Space works.
- Save and Close capture the complete original word before any correction trigger.
- Save and Close also retain that word after the private Fcitx daemon is killed.

The baseline and fast-tail cases additionally exercise unchanged password input;
Save/Close cases finish early and do not claim password coverage. All six were
rerun after strengthening the sensitive-hint recheck. [Aggregate assertions](INPUT-LAB-006-results.json).

Closed ASan/UBSan/LeakSanitizer run also completed with exit 0 and **10 passes**,
with no diagnostics. This instruments the new adapter/test translation unit, not
the system Qt libraries or prebuilt core library. An earlier run could not finish
LeakSanitizer under ptrace; an initial closed retry hit an overly small file-size
limit for Qt buffers. The final closed run uses no host HOME/bus/network/devices,
20-second timeout/CPU limit, zero core dumps and a 16 MiB file-size cap, with QML
disk cache disabled. Those harness failures are not counted as successful runs.

Public offscreen client tests: **10 passes**, consisting of eight cases and
init/cleanup. They verify committed input, real correction, protected/unapproved
exclusion with zero text/evidence queries, cursor/selection refusal, native
Ctrl+A, absent evidence, focus changes, sensitive-hint changes during lookup and
overlong token refusal. Run `bin/test-client-adapter-lab` from a checkout; unlike
the private compositor experiment this suite does not need the undistributed
server. Initial test harness fixes (generic offscreen theme and lowercase test
key text) were needed; they are not application fixes.

## Architecture disposition

- External whole-word composition remains a **rejected universal backend
  candidate** for now because Save/Close admission fails. Its earlier latency
  numbers belong to that path, not this new adapter.
- In-process correction is a **candidate only for explicitly integrated,
  supported clients**, not a replacement universal OS implementation. This lab
  does not install plugins into other applications or offer an automatic fallback.
- No production runtime/backend, global configuration, autostart, new system
  permission, clipboard or host input path has been enabled. Existing manual
  application behavior is unchanged.

An in-process C++ component also shares the client's failure boundary; it is not process-isolated from that editor. This tradeoff must be reviewed before choosing a production integration.

Next work is independent review and a concrete supported-client integration path,
plus real-client lifecycle and resource/latency tests. The current adapter fixes
US layout, one-line trusted Qt Quick fields and synthetic evidence. Large document
query allocation, multiline coordinates, arbitrary reentrant handlers, model
blocking, layout changes, undo/redo and real editors/terminals/browsers remain
unverified. A synchronous event in this fixture is not a general atomic-edit
protocol for unrelated applications. No additional data/model reads are authorized
by this experiment. All foreign libraries are already installed; no foreign code
or data was copied into the repository.

## Subsequent joint decision

After independent Codex and Claude Opus proposals, [Architecture decision 002](ARCHITECTURE-DECISION-002.md) selects a commit-first Fcitx candidate for product coverage. The in-process demo is a fallback, not the main system-utility plan. This changes direction, not the recorded Save/Close failures. The new external candidate currently refuses missing context and has not demonstrated correction.
