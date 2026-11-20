# rasklad — current handoff

- Worktree /home/mvn/Work/rasklad-message-adapter, branch SYS-246/slack-status-observation.
- Publish draft PR17 via HEAD:SYS-242/message-adapter; Linear SYS-246 In Progress.
- Main /home/mvn/Work/rasklad and neighboring/private projects untouched.
- Roles since 2026-11-20 (docs/AGENT_SETUP.md): Claude leads/implements/accepts;
  ecc subagents execute scoped tasks; Grok (Cursor) and Gemini (Antigravity) give
  independent criticism; Codex is reserve reviewer. Author never self-approves.
  Compact prompts, one executor, no repeated green tests without changed paths.
- Normal Claude CLI authenticated and working. --bare cannot use OAuth; do not
  infer account logout from bare mode. Antigravity CLI authenticated normally.

## Proven / boundaries

- Offline detector + policy/context leases and owned Fcitx/Sway fixtures passed
  earlier gates; Submitted means local completion, not delivery (ADR003).
- Real Slack remains observe-only/default-deny: focused multiline editable state
  found, EditableText absent, active window/channel/field authority unproven.
- Native2same-appid windows /2observers stable-ID lifecycle passed on ownedSway.
- One approved host registry-only query found EXT1/WLR3/Hyprlandmapping1.
- ALL host/GPU/liveSlack approvals consumed. No new host Wayland/GPU/IME/config,
  Slack edit/send/restart/termination or ECH permission. Own closed noGPU fixtures
  allowed. No text/profile/history logging or cloud transfer.

## Current block

- Three approved closed GPU-only trials consumed. OwnSway feedback resolves
  admittedrender; Hyprland aborts noallocator before observed main_device.
- Actual installedHyprland noGPU synthetic parent control001: immediate feedback
  processed; after-sync feedback missed. This proves a readiness weakness, NOT
  the real wlroots cause. data/hyprland-feedback-parent-001-result.json.
- Claude prepared tools/aquamarine_readiness_patch.py + experiment doc; patched
  Aquamarine not built/run. Helper now schema2 immediate/deferred/never; old001
  evidence was schema1 two variants. No automatic reinterpretation.
- Grok4.5 via Cursor security review + Gemini3.8FlashMedium via Antigravity router
  simplification review completed. Larger request timeouts not credited.
- Claude fixed applier symlink/hardlink/atomicity;8tests PASS after finalguard.
  Raw-this UAF claim rejected with ownership evidence; router unchanged.
- Next (preferred, route B; Grok+Gemini critiques agree): Claude prepared
  tools/hyprland_mapping_probe.py + tests/closed_mapping/test_direct_mapping.py,
  docs/HYPRLAND-DIRECT-MAPPING-PROBE.md. Codex: run guards, one independent
  boundary review, THEN ask owner for fresh approval of ONE ~40s
  `python3 -I tools/hyprland_mapping_probe.py --run-approved` (one host FD,
  renderD128, owner must not type). Success = own-window mapping only.
- Fallback (route A, only if B cannot start or mapping incoherent): Aquamarine
  patch per docs/AQUAMARINE-READINESS-EXPERIMENT.md; missing cmake/
  hyprwayland-scanner => stop, no installation/fetch.

## Workflow

- Owner alerts: bin/notify-owner --needs-user only for required handoff, --done
  only actual completion. Alternating private ignored audio; DND respected.
- No checkpoint notification spam. Save compact handoff and targeted evidence.

- RouteB direct helper prepared byClaude;12guards PASS, exact ownFD/noGPU scope control PASS. Grok critique resolved/limited with evidence; physicalinputexposure declared. Claude5hourquota until19:30Almaty, Gemini followupcode timeout notcredited; Codex only acceptancefieldrename+noGPUcontrol. Fresh hostWayland+GPUapproval requested; direct trial NOTrun.

- Direct trial001 separately approved+PASS: actualHyprland2windows/2observers/close-recreate mappingVerifiedtrue,7iterations,scopeinactive. Approval consumed; owner typing safe after teardown. data/direct-hyprland-mapping-001-result.json. NEXT offline pinnedFcitx appmonitor/context join analysis; fieldbinding stillfalse, Slack remainsobserveonly.

- Current noGPU field-lifecycle fixture rebuilt+PASS18checks/4cases. ExistingFcitxV2 callbacks+ourlease watchers reused; no newfieldfrontendpatch. Case2Focus2/2,cap16,commit0. Tasks96causedthread/forkEAGAIN; closedChrome acceptance192 passedsameRAM1.5GiB/CPU1,scopeinactive. See FCITX-FIELD-SEAM-001.md. Next supervisedrealSlack plan under peercritique; NO liveSlack/profile/hostIME approval.
