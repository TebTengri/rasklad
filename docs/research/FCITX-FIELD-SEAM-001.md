# Existing Fcitx field lifecycle seam

2026-11-17. After actual installedHyprland native window-mapping PASS, Grok via
Cursor and Gemini via Antigravity independently inspected pinned Fcitx5v5.1.23
(commit27cca6e0239938b2d53937061d0980c087856b22, archive SHA-256 verified).
Source was read from ignored build only; no upstream implementation vendored.

## Source facts and corrected interpretation

WlrAppMonitor window keys are connection-local proxy IDs; VirtualIC manager
multiplexes windows, not individual widgets. This alone says nothing about
missing field lifecycle: the first source subset lacked waylandimserverv2.cpp.
After supplying the actual V2 code, both critics identified existing seams:
activate/deactivate are staged and processed deactivate-first at done; wrappers
produce focus transitions; content_type updates password/sensitive capabilities.
Compositor/client delivery still requires runtime evidence.

focusedVirtualIC() is already public in its class declaration; this does not
make its internal header a supported public SDK. Earlier recommendation to
expose an already public method was not accepted. No new frontend field patch
or synthetic field identifiers were introduced. Existing src/fcitxlab.cpp
watchers already invalidate state/lease on focus-in/out/reset/IM-switch and
capability changes. Presence of resetCallback does not prove a V2 reset event
is delivered; the observed reference case has resetdelta0.

Primary source paths at the pinned Fcitx commit:
- src/frontend/waylandim/wlrappmonitor.cpp
- src/frontend/virtualinputcontext/virtualinputcontext.{h,cpp}
- src/frontend/waylandim/waylandimserverv2.cpp (activate/deactivate/done160-246;
  content_type258-261 and340ff)

## Current runtime verification

Restored two existing SDK/VK artifacts from current own source in a network-free
private build, no GPU/hostHOME/system installation. Rebuilt the existing private
LGPL keep-grab frontend from953verified pinned files and committed patches.
Public test-model/dictionary artifacts reused read-only from own rasklad cache;
not copied into Git or approved for release. No new dependencies installed.

Current software Sway/nativeChromium fixture:18correction checks plus4intra-window
field scenarios PASS. Reference environment ONLY, not realSlack or an actual
Hyprland field-binding proof. Case word_other_back_prose_space: focusIn/Out2/2,
capabilityChange16, passEmpty1, commitSpace0, commitString0; packetvalid/complete.
Password transition, escape/other field and compositionend-focus hook also pass.
The callback counters plus source discard logic support reusing the existing
epoch seam, not inventing a new field-ID protocol.

The first restored-browser attempt hit pthread/fork EAGAIN under TasksMax96 and
failed startup. A changed-constraint control with TasksMax192 passed; memory
1.5GiB and CPU100% remained enforced. No OOM event observed in own scope journal.
This tuning affects only the closed browser acceptance scope; host/GPU trial
limits are unchanged. Cleanup inactive/empty verified for both.

Evidence: data/field-lifecycle-current-001-result.json and
data/field-lifecycle-build-001-result.json. Source/model criticism is not runtime
proof; neither nativewindow PASS nor this separate fixture authorizes ordinary
Slack. Production registry/unknown-field behavior remains default-deny.

## Next gate

Prepare one explicit supervised real-app session rather than enabling global
hostIME. App identity must come from trusted launcher/compositor process
evidence, not its declared app-id. A human-approved focused prose field must
be bound to ICid/epoch/windowbinding and revoked on lifecycle change; protected
capability gates remain non-overridable. First real Slack connection/profile/
input test requires a concrete reviewed helper and fresh explicit approval.
No host/profile/Slack action is authorized by this source/runtime analysis.
