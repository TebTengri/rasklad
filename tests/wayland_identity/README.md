# Core object-ID namespace counterexample

Plan: `python3 tests/wayland_identity/run.py`
Build: `python3 tests/wayland_identity/run.py --build`
Run: `python3 tests/wayland_identity/run.py --run`
Guards: `python3 -m unittest discover -s tests/wayland_identity`

Real libwayland client/server1.26.0 over2private socketpairs, no filesystem
listener. Two connections allocate4wl_surface objects each, with shifted proxy
allocation. Actual server records validate IDs and distinct owning wl_clients.
Observed2numeric-ID collisions with different per-client surface ordinals.
This disproves cross-connection numeric-ID equality as object identity.

No native toplevels, Wlr/xdg/Fcitx, fields, input, host display, GPU or network.
It is a namespace counterexample, not production adapter/compatibility proof.
Runner seals the binary after checking source/binary hash record; closed bwrap,
CPU6s, core0, FD64, file64KiB, output4KiB, outer10s and own alarm6s. Compile logs
use fresh mkstemp fd + atomic entry replacement; never truncate a linked target.
Three guards cover nohost mounts/env, stale/invalid build record and log symlink.
Claude authored C++; Codex reviewed/fixed registry listener, cleanup and patch
truncation; independent static review found and accepted log safety correction.
Evidence: data/wayland-object-id-scope-001-result.json.
