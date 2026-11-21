# Host registry query — one approved measurement completed

Purpose: determine whether installed Hyprland advertises the interfaces needed
for a candidate WLR↔stable-window mapping. Primary Hyprland protocol defines
get_window_for_toplevel(ext_foreign handle) and get_window_for_toplevel_wlr(WLR
handle), yielding a common64bit window address or failure:
https://raw.githubusercontent.com/hyprwm/hyprland-protocols/main/protocols/hyprland-toplevel-mapping-v1.xml
Protocol source uses BSD3-style conditions; retain its notice if later vendored
or distributed. No protocol file was copied into own source in this checkpoint.

Command prepared: `python3 -I tools/host_registry_probe.py --run-approved`
Default invocation is inert plan. Requires a separate explicit approval: earlier
host Hyprland/GPU approvals are consumed and unrelated Slack approvals do not
cover a new Wayland connection.

Exactly1AF_UNIX connection, existing XDG_RUNTIME_DIR/strict basename, owner/socket
metadata checks. Kernel SO_PEERCRED must match current UID and expected root-owned
Hyprland executable. Child source/binary/XML provenance checked; sealed executable
receives only that preopened socket and its own code fd, minimal environment.
Registry-only branch does get_registry + sync, binds0globals, then disconnects.
It returns ONLY3fixed protocol version numbers; no toplevel enumeration, title,
text, clipboard, pixels, focus actions, input, IME restart, GPU or setting change.
Peer PID/socket paths are not output. Outer8s/own6s watchdog, CPU2s/core0/FD64,
file64KiB/output4KiB. Child is a constrained-source ordinary process, NOT claimed
to be a bwrap namespace; host socket is the explicitly requested exception.

Closed-control evidence already exists using same compiled reader on owned Sway:
stable1,WLR3,Hyprlandmapping0,globalsBound0. Native default mode still passes its
2windows/2observers/lifecycle test after the mode addition. Guard tests reject
wrong-peer UID/exe, guessed/path-traversal display values and runtime symlinks.
This query only proves advertised interface presence at measurement time, never
mapping correctness or Fcitx/AT-SPI field authority. Future bindings must handle
missing/removed globals and stale handles. One explicitly approved host measurement completed; that permission is consumed.

## Supervisor review and limits

Claude reviewed the prepared supervisor and registry branch. Accepted: reject
RPATH/RUNPATH on the sealed executable, strip pkg-config environment during
build, whitelist only three bounded numeric versions, and describe kernel UID
and executable matching precisely. This does not authenticate the active desktop
session or defend against a compromised same-UID process. No edit authority is
granted by a positive result.

The local repository and its Python helpers are trusted reviewed code. Mutable
source manifests detect stale builds; they are not signatures or a defense
against repository tampering. Use Python isolated mode as shown above. The
sealed snapshot helper was inspected: it copies verified bytes and applies
WRITE/GROW/SHRINK/SEAL seals. Child codeScope fields describe reviewed code
behavior, not an independent system-wide observation. Eight local guards pass;
no host connection has been made.

## Actual result

One approved connection succeeded: stable list v1, WLR manager v3, Hyprland
mapping manager v1. No globals bound. Aggregate: data/host-registry-001-result.json.
This is interface availability only. Next: implement and test the mapping client
on owned windows in a closed compositor; do not enumerate live desktop windows
under this consumed registry-only permission.
