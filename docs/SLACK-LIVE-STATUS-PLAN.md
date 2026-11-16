# Prepared live accessibility status trial — not executed

Command: python3 tools/slack_status_trial.py --run-approved
Default invocation only prints a plan. Existing service required; same-UID unique
owner pinned. Baseline must be exactly false. Set IsEnabled true, run existing
10-second metadata-only Slack probe, then restore false with readback (3 bounded
attempts). SIGINT/SIGTERM are ignored during restoration only. No app text,
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
measurement completion checks. Seven mock tests cover these paths. Existing
metadata reader remains read-only and restricted to Slack-owned objects.
