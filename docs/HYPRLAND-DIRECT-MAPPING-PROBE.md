# Route B: host-parent nested Hyprland mapping probe (prepared, NOT run)

Status: prepared only. No host Wayland/GPU execution authorized or performed.
Plan (inert): `python3 -I tools/hyprland_mapping_probe.py`
After separate fresh owner approval only: `python3 -I tools/hyprland_mapping_probe.py --run-approved`

Why route B: two independent critiques (build/claude-handoff/route-review-{grok,gemini}.txt)
prefer reusing the known-working host-parent nested recipe (exact011 PASS) over
patching/building Aquamarine (route A, blocked by missing build deps; kept as fallback).

Recipe reused, not reimplemented:
- endpoint ownership `hyprland_nested_probe.endpoint` (refuses inherited WAYLAND_SOCKET);
  kernel SO_PEERCRED uid + `/proc/<pid>/exe == /usr/bin/Hyprland` via
  `host_registry_probe.peer_is_hyprland`; ONE preopened host FD, parent copy closed
  right after launch, so it lives only in own nested Hyprland.
- `host_registry_probe.validated_binary`: build manifest/source/XML inputs, sealed
  snapshot of build/window-provenance/native-windows, RPATH/RUNPATH refusal.
- `drm_metadata_snapshot` + `closed_hyprland_mapping.sandbox_args`: renderD128 only,
  snapshot /sys, primary card RO+NODEV checked inside before launch.
- `closed_hyprland_mapping.scope_command/cleanup_scope`: Memory1536M, swap0, Tasks96,
  CPU100%, Runtime27s, effective-limit guard, own unique scope must end inactive.
- installed `/usr/bin/Hyprland` with nested CONFIG; nested client `--mapping` alarm6s,
  compositor socket wait7s, wrapper30s (+cleanup ≈40s). No retries.

Client: two OWN synthetic windows, two observers, destroy/recreate lifecycle
(EXT + WLR + Hyprland mapping). It connects only to the new PRIVATE socket.
Output: bounded booleans/counts. Success requires client exit0, mappingMode,
mappingVerified, observerCleanup, actualHyprland, hostParent, then inactive scope.
Socket readiness is never success. Failure forwards fixed phase/reason/startup
enums and bounded client-stage counts; private logs stay in the namespace.

Not done: host window enumeration, apps, Fcitx/IME, profiles, bus, network, config,
keyboard injection or host focus actions. The nested window MAY take host focus:
the owner must not type during the approved run. fieldBindingVerified and
userWindowAuthority stay false: success proves own-window mapping agreement on
real installed Hyprland as nested compositor only, not host-session window or
field authority. Shared GPU driver risk remains.

Guards: `python3 -m unittest discover -s tests/closed_mapping` (adds
test_direct_mapping.py: sandbox shape, success/failure, inert default).

## Independent critique and acceptance

Grok reviewed the new public source. ParentFD inheritance concern was tested
through the exact new sandbox/scope using an owned socketpair, noGPU grant,
synthetic empty sysfs and harmless NODEV /dev/null primary substitution:
fdInheritedtrue/exit0/scopeInactivetrue. No host display was connected.

The report now accurately states physicalInputMayReachNested/ownerMustNotType;
keyboardInjection remainsfalse. This mechanical acceptance correction was made
by Codex after Claude hit its5hour quota. Gemini code-generation followup timed
out and is not credited as an implementation.

The apparent forced-success concern was rejected: inside validates the native
client via verified_report before shaping its output, and outer validates again.
Negative success/shape guards pass. Helper Python files remain trusted local
project code, not signed/authenticated code against active sameUID tampering.
fcitx_lab_runner.py is an import dependency of the reused mapping helper, not
an IME process or service. No extra helper framework introduced.

Twelve closed_mapping guards passed. Fresh host/GPU approval requested; no
direct mapping trial has yet run. No patched system library or new build tools.
