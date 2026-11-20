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
