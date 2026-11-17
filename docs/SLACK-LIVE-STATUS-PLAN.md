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
restored committed code into /home/mvn/Work/rasklad-message-adapter. Ten status
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
