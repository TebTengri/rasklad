# Step 0: real-profile Slack bootstrap feasibility (prepared, NOT run)

Question: does the owner's real desktop Slack map a Wayland window inside our
closed sandbox? Every later step of docs/SLACK-SUPERVISED-SESSION-PLAN.md depends
on it; earlier empty-profile offline runs never produced a window.

Tool: `python3 -I tools/slack_bootstrap_probe.py` (plan only).
Dry runs without owner data or network: `--dry-run-empty-profile`,
`--dry-run-chromium-control`. Live: `--run-approved` only after a separate
one-shot owner approval, with host Slack closed by the owner.

Live run boundary:
- real `~/.config/Slack` bound read-write at the same path (no copy; paths
  written by Slack stay valid); refuses a symlinked/group-writable profile or
  any `SingletonLock`; refuses if any Slack process is running;
- closed headless Sway (pixman), no host display, GPU or input devices;
- private netns; TCP only through the unix-socket relay to the CONNECT+SNI
  bridge (slack.com, slack-edge.com, slack-imgs.com), ECH outer-SNI allowed;
  bounds 105 s, 384 requests, 256 MiB, 16 active; rejected targets counted by
  last two labels only;
- host session bus only through `xdg-dbus-proxy --filter
  --talk=org.freedesktop.secrets` (cookie key in gnome-keyring); no portals,
  xdg-open, notifications or IME;
- no input, no text reading, no screenshots; WAYLAND_DEBUG is scanned in memory
  for fixed markers and discarded;
- up to 75 s, stop 15 s after the first buffer attach; SIGTERM with 8 s for a
  clean profile flush before SIGKILL; zero remaining Slack processes checked.

Owner accepts: Slack connects to its servers with the real session (presence,
token refresh), and a small risk to the profile since no copy is made.
Login state is not observed; windowMapped is the only feasibility answer.

Dry-run evidence: data/slack-bootstrap-dryrun-001-result.json. Positive
control maps a window at 0.44 s; empty-profile offline Slack binds the registry
at 0.4 s and maps nothing in 75 s. Tests: tests/slack_probe/test_bootstrap_probe.py.

## Independent review 2026-11-20

Grok (cursor-grok-4.5-high-fast, ask mode, code inlined): FIX-FIRST. Earlier
attempts with grok-4.7-high (480 s) and gemini-3.8-flash-high/medium (timeouts,
headless read permission) returned nothing and are not credited.

Accepted and fixed: rejected domains reported only for a fixed list of public
infrastructure suffixes, everything else `other` (no company/SSO names);
windowMapped now needs toplevel + acked configure + >=10 buffer attaches and
no fatal/display marker, and is documented as "renders a window", not "UI or
login ready"; graceful quit 15 s, outer budget 125 s, broker 120 s; a leftover
SingletonLock fails the run with an owner-recovery note (never deleted
automatically); leftover Slack processes are SIGKILLed only when they are in a
foreign mount namespace; secretServiceError uses specific failure strings;
host-Slack and profile checks repeated immediately before launch.

Rejected: (1) "Secret Service filter exposes the whole keyring" — normal
host Slack already has exactly this access; withholding the key risks Chromium
re-encrypting cookies and logging the owner's desktop Slack out, which is the
greater profile risk. (2) "XDG_CURRENT_DESKTOP=Hyprland under Sway" — the
value deliberately matches the host session so Slack picks the same backends.

Known limit: an outer timeout tears down bwrap (die-with-parent), which can
SIGKILL Slack without the inner graceful path; budgets are sized so the normal
path finishes first.

After fixes: 67 slack_probe tests PASS; Chromium control windowMapped true
(21 attaches, graceful); empty-profile Slack windowMapped false (0 attaches,
graceful), 0 remaining processes.

## Live run 001 (owner one-shot approval, consumed)

First attempt refused at preflight (stale SingletonLock from an earlier
experiment, pid dead); nothing launched. Owner cleared it by starting and
quitting Slack normally. Run: registry bound at 0.4 s, no toplevel in 75 s,
graceful stop, no remaining processes, no lock left, no Slack error markers.
Bridge: 8 accepted, 0 opened, 8 errors, 0 bytes — Slack effectively had no
network, so the result is inconclusive. Offline hello capture
(`--dry-run-hello-capture`) then parsed 8/8 Chromium ClientHellos to Slack
domains with 0 errors, so parsing is not the cause. The bridge now records
fixed error categories by stage (header/hello/resolve/connect/pump). Evidence:
data/slack-bootstrap-live-001-result.json.

## Bridge self-test (owner one-shot approval, consumed) and fix

Own Python ssl client through the online bridge, no Slack/profile/keyring:
slack.com direct and via Relay, plus app/edgeapi/wss-primary.slack.com,
a.slack-edge.com, slack-imgs.com with ECH outer-SNI mode: all TLS 1.3, 0
errors. The bridge works for prompt ClientHellos. Likely live failure: Relay
allowed only 8 concurrent tunnels for 30 s, and the broker waited only 3 s for
a ClientHello after `200` while Chromium preconnects tunnels. Fixed: hello wait
20 s, Relay 115 s / 16 active (configurable, bounded), `relayRefused` and
per-stage error categories reported. Regression test covers a 4 s late hello.
Hypothesis is unverified until a live repeat (separate one-shot approval).

## Live run 002 (owner one-shot approval, consumed)

Bridge fix confirmed: 2 tunnels opened, 23 949 bytes, 0 errors, 0 relay
refusals. Still no toplevel in 75 s; registry bound at 0.41 s; graceful stop,
no leftovers, no lock. Only 2 connections in 75 s means Slack stalls before its
UI regardless of network. The identical Wayland pattern with an empty offline
profile allows diagnosis offline. Evidence: data/slack-bootstrap-live-002-result.json.

## Root cause of the stall (offline diagnosis, no owner data)

Dry-run diagnostics (`--dry-run-diag <variant>`, empty offline profile only)
read Slack's own `logs/default/browser.log`: boot throws "No unique release
file found!" because the sandbox lacked `/etc/os-release` and
`/etc/arch-release`, and stops before creating a window. `--no-sandbox` and
`--no-zygote` changed nothing; live run 002 had working network. After binding
both files read-only, empty-profile Slack created a toplevel at 0.62 s and
attached 6 buffers (offline error page), then closed the window ~14 s later.
Docs (Context7, Electron): Electron 38 defaults to native Wayland when
XDG_SESSION_TYPE=wayland; ELECTRON_OZONE_PLATFORM_HINT was removed. Slack's
official Linux help covers installation only.

## Live run 003 — feasibility YES (owner one-shot approval, consumed)

Real profile: toplevel at 0.65 s, 141 buffer attaches, 16 tunnels, 906 KB,
0 errors; only slackb.com (telemetry) rejected. Graceful stop, no remaining
processes. Issue: SingletonLock left as `<host>-7` — the sandbox PID, which on
the host is a kernel worker, so normal Slack could think the profile is in use.
With owner approval the three stale links were removed after verifying no Slack
process and the exact target. The probe now releases singleton links itself,
only when the lock names exactly its in-namespace Slack PID (tests added).
Evidence: data/slack-bootstrap-live-003-result.json.
