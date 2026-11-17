# Prepared closed Hyprland mapping trial

Status: implementation built and statically reviewed; GPU trial NOT executed.
Command after a separate explicit permission:
`python3 -I tools/closed_hyprland_mapping.py --run-approved`
Default invocation prints a plan only.

Own headless GLES Sway is the parent of own nested Hyprland. Both run inside one
bwrap user/PID/network/mount namespace with private HOME/runtime/config. No host
Wayland connection, desktop window, host bus, network, input device or user profile
is exposed. Own synthetic two-window client checks EXT/WLR mapping and lifecycle
through two independent observers. No identifiers, addresses, titles or text are
printed; Fcitx is not loaded and fieldBindingVerified stays false.

Requested exception: dev-bind only renderD128. One small validated PCI metadata
snapshot substitutes for /sys; the primary card is RO+NODEV/stat-only and checked
before compositor launch. No KMS/VT, primary open, full sysfs or host Wayland.
Shared GPU driver/reset/VRAM risks remain; namespaces/cgroups do not eliminate
those risks or forcibly time-bound kernel GPU operations.

A unique transient user scope applies MemoryMax1536M, MemorySwapMax0, TasksMax96,
CPUQuota100%, RuntimeMaxSec27 and TimeoutStopSec1. Bootstrap checks effective
controller values before bwrap. Outer timeout30s and cleanup add a wrapper budget
of roughly40s. A successful report additionally requires scope inactive with
empty ControlGroup; failed cleanup cannot accompany claimed success. No persistent
service, autostart or system configuration is created.

Evidence: strict native build;8existing local guards+4new boundary guards; normal
private software Sway lifecycle regression PASS5iterations. Exact scope bootstrap
synthetic test passed effective limits, inherited FD, inactive cleanup; no GPU or
Wayland used by that control. Independent Codex static review found initial
resource/success/cleanup blockers; all addressed and re-reviewed. Claude CLI is
not logged in, so no new Claude review is claimed.

Vendored protocol XML retains complete upstream notices and immutable source
commits/hashes in vendor/wayland. Own code MIT does not relicense them. Runtime
bytes are checked against the local lock; signatures were verified during earlier
acquisition, not by treating editable signatureVerified fields as signatures.
Host /usr dependencies remain trusted installed code, not a reproducible release.

Installed versions inspected: Hyprland0.56.2-4 / Aquamarine0.15.1-1. Corresponding
upstream release OpenGL.cpp selects EGL via DRM/device or GBM, and headless backend
returns no DRM FD. This motivates the GPU-only exception; no patched-package or
GPU-free execution proof is claimed.
- https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/render/OpenGL.cpp
- https://github.com/hyprwm/aquamarine/blob/v0.15.1/src/backend/Headless.cpp

Success establishes window mapping agreement only, not input-field identity,
Fcitx context authentication or permission to edit Slack. Failure is recorded
without automatic expansion to host Wayland or more devices.
