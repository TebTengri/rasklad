# Offline native Wayland bootstrap control

The private Sway compositor starts successfully with pinned sealed runtime files,
headless/pixman rendering, no host display, GPU, network, or real Slack profile.
The existing private accessibility disabled/enabled A/B control is reused.

Slack: no accessibility peers in either mode. Chromium: zero peers disabled,
one peer and 64 inspected nodes enabled. No document role was observed within
the bounded traversal. `visibleWindows` is null for Wayland: window visibility
was not measured. We cannot conclude that Slack has no window or lacks support.
Both apps report the same fixed busUnavailable stderr category, so that category
alone does not explain the Slack result. Seven Slack child process roles remain
unknown; no renderer-absence inference is justified.

Evidence: data/slack-wayland-bootstrap-001-result.json. Measurement success is
not application readiness or successful correction. No text/input/edits were used.
An initial private fixture failed with PermissionError because the sealed Sway
mount lacked executable permission; the mount now explicitly sets 0755 for Sway
and 0444 for libraries. The corrected run above completed.

Validation: 25 Slack probe/bridge guards pass. Independent static review by
wayland_control_review found no blocking isolation issues; no runtime action by
reviewer. Existing X11 behavior remains the default. Wayland plus any bridge is
rejected; the consumed ECH exception remains unavailable.

Next: diagnose Slack bootstrap using fixed, bounded startup-stage evidence.
Do not expand network access or copy the working profile. Avoid additional
status A/B repeats without a new falsifiable hypothesis.
