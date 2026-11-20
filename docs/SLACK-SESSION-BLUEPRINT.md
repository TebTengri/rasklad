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
