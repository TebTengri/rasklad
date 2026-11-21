# Next closed mapping experiment

Status: prepared acceptance plan, implementation not complete.

Use an actual isolated Hyprland, two owned native same-app-id windows, and two
independent observer connections. Match each EXT stable identifier to a mapping
address and the WLR handle to the same address. Never compare bare Wayland object
IDs across connections. Export booleans/counts only, no addresses/titles/IDs.

Acceptance: two distinct live windows; per-window EXT/WLR address agreement;
observer agreement; closing one invalidates only its binding; recreation uses a
fresh stable ID. A reused address must not revive the old binding: associate it
with compositor-session generation and stable ID, and reject stale/failed mapping
responses. Restart clears all state. Missing/removed globals fail closed. No
Fcitx field authority or production lease follows from window-only success.

Runtime gate: first inspect the installed-version renderer/backend sources and
existing isolated-launch evidence. Headless output is not proof of GPU-free
rendering. Do not claim Sway or a fake server validates the Hyprland mapping.
No host Wayland/GPU access is currently authorized; the single registry query is
consumed. A needed new resource must be concretely scoped and separately approved.

Primary references (inspection only; not vendored):
- https://raw.githubusercontent.com/hyprwm/hyprland-protocols/main/protocols/hyprland-toplevel-mapping-v1.xml
- https://raw.githubusercontent.com/hyprwm/Hyprland/main/src/render/OpenGL.cpp
- https://raw.githubusercontent.com/hyprwm/aquamarine/main/src/backend/Headless.cpp

Claude next-block request failed authentication; no review or implementation
credited. Existing earlier Claude supervisor review remains separately recorded.
