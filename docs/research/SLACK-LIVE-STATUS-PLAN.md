# Prepared live accessibility status trial — not executed

Command: python3 tools/slack_status_trial.py --run-approved
Default invocation only prints a plan. Existing service required; same-UID unique
owner pinned. Baseline must be exactly false. Set IsEnabled true, run existing
10-second metadata-only Slack probe, then restore false with readback (3 bounded
attempts). SIGINT/SIGTERM are intentionally ignored throughout the bounded mutation/probe/restoration interval, before any mutation. Original handlers are restored afterward. No app text,
editing, restart, profile copy, channel navigation, input or message sending.
No persistent configuration files are edited by this tool; the system service
may itself store the status property. Restoring the property does not prove that
clients activated during the trial return to their original accessibility state.
SIGKILL, process crash, or service replacement can prevent restoration. A
replacement owner is never modified. Failure must be reported, not called clean.

Owner authorized tests in one working Slack channel, privately recorded in
ignored build/slack-live-test-scope.json. This is NOT permission for this wider
session-status change. Explicit additional approval is pending. Do not publish
channel identity in this public repository or cloud review context.

Independent review identified restoration/reporting weaknesses; fixed bounded
retry/readback, atomic blocking of handled signals at cleanup entry, termination handling, enable-attempt uncertainty and nested
measurement completion checks. Ten mock tests cover these paths. Existing
metadata reader remains read-only and restricted to Slack-owned objects.

Claude supplied the main-thread preflight implementation and two new tests;
Codex reviewed and applied the patch. Refusal occurs before control/bus calls.
The added tests also verify an unconfirmed enable never runs the metadata probe
and still restores the original property. Ten status tests pass locally.
Gemini returned a concrete finally-entry signal race. A second Claude request
for its implementation timed out after 150 seconds; Codex implemented the
whole-trial signal wrapper and added a signal-during-probe regression test. Grok timed out after 120 seconds and is NOT counted as review.
No automatic retries to burn further quota. The live trial has still NOT run.

## Executed owner-approved trial (2026-11-17)

Explicit permission received. Original temporary worktree had disappeared;
restored committed code into <repo>. Ten status
mock tests passed before execution. One actual false→true→false trial completed
with restoration verified on first attempt, no app text/writes/restart. Slack
was not running (0 target processes), so this cannot test compatibility.
Owner then opened Slack: read-only preflight found 8 target processes, 0 peers,
IsEnabled false. Initial missing-process prerequisite was an experiment flaw;
add rejection before status mutation. Procedure passed does NOT mean Slack ready.
One-shot permission consumed; no repeated enable inferred from opening Slack.
Evidence: data/slack-live-status-001-result.json.

## Approved repeat and instrumentation correction

Owner explicitly approved ONE repeat after opening Slack. External same-process
PID preflight ran before runner entry. Repeat:8processes/0peers in the immediate
snapshot; false→true→false confirmed and first-attempt restoration verified.
Both one-shot permissions are consumed. No text, edits, sending or restart.
These were snapshots, NOT full 10-second observations; the old ten-second limit
was a deadline rather than a wait. Do not claim dynamic enable is ineffective.

Claude's readiness patch request timed out75s with no patch. Codex added
pre-mutation checks and targetReady separate from procedure passed. Added
optional observe mode under the existing deadline, caching unique-name PIDs,
<=256lookups and no app getter before ownership. Six fake-clock tests include
delayed registration, bounded wait, snapshot behavior, missing processes,
lookup-volume and slow PID discovery. Thirteen status tests pass.

Independent peer_observation_review found no ownership/privacy regressions but
flagged the separate PID lookup timeout. Accepted: PID discovery now receives
remaining budget and no late scan can query the bus. No new live observation
was run after these changes. Future mutation still requires explicit consent.

Context7 /electron/electron app.md documents setAccessibilitySupportEnabled and
setAccessibilitySupportFeatures for macOS/Windows, not Linux. Do not propose
patching Slack or its app.asar as a substitute. Source:
https://github.com/electron/electron/blob/main/docs/api/app.md

## Approved bounded observation

Owner separately approved ONE corrected observation. Result:8Slack processes,
36discovery scans over9.999seconds,0owned accessibility peers, no truncation.
False→true→false restoration verified firstattempt. This third one-shot approval
is consumed. It tests absence of registration in the observed running instance
during that window, not all Slack builds or cold-start accessibility.

## Next prepared boundary — per-process launch, NOT executed

Chromium primary source, blob59a94cf148d8d0ae2c44bbfb65b899bf31c7d6b0:
https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/accessibility/platform/atk_util_auralinux.cc
ShouldEnableAccessibility first checks ACCESSIBILITY_ENABLED, GNOME_ACCESSIBILITY,
QT_ACCESSIBILITY (1→enable,0→disable), before D-Bus IsEnabled/GSettings.
InitializeAsync gates platform initialization with that decision. This supports
a per-process startup hypothesis, not a guarantee for the installed Slack build.
An ASCII NUL-terminated ACCESSIBILITY_ENABLED token exists in the installed
Slack executable; static presence alone is not behavior proof.

Prepared tools/slack_launch_probe.py: plan by default; explicit-approved action
refuses if any current-user fixed-executable Slack is running. Owner must quit
Slack manually, protecting unsaved work rather than automated termination.
Fixed executable, force-renderer flag, process-local ACCESSIBILITY_ENABLED=1 and
NO_AT_BRIDGE=0; normal profile, no copies. Does NOT set global IsEnabled. Normal
Slack startup can update its own profile and connect to services, as usual.
Only metadata is read; no messages/text/edits. Slack remains running with those
process settings until the owner exits; subsequent ordinary startup reverts the
environment. No launch or further status mutation authorized yet.

Independent launch review found no additional privacy/read-boundary issues;
accepted its reporting fix: toolTerminatedSlack=false expresses the actual
guarantee, while targetPresentAtProbe reports observed process presence. No
unconditional claim that Slack remains alive. Startup may sync, notify and write
its own ordinary profile; disclose this in owner approval.

## Process-scoped launch succeeded

Owner approved launch, then separately approved one checked SIGTERM after manual
window closure left 8 background processes. First tool escalation timed out
before process creation; its one permitted retry sent exactly one pidfd-bound
SIGTERM. Slack fully exited; no SIGKILL or child sweep. Approved launcher then
started the normal profile with process-only accessibility. 1a11ypeer/23nodes
observed after0.571s, 7target processes, globalIsEnabledfalse. No app text read,
writes, sends, profile copy or global status mutation. This confirms access to
Slack's accessibility bridge in this run, not editable-field safety/correction.
FocusedEditableObjects was empty during startup. Owner focus of EMPTY test-channel
editor requested; next probe stays metadata-only. Both permissions consumed.
Evidence: data/slack-process-launch-001-result.json.
