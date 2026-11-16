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
