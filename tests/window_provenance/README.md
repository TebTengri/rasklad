# Actual native-window identity and lifetime on owned Sway

Plan: `python3 tests/window_provenance/run.py`
Restore pinned runtime if missing: `bin/prepare-wayland-lab --fetch` (never installs).
Build: `python3 tests/window_provenance/run.py --build`
Run: `python3 tests/window_provenance/run.py --run`
Guards: `python3 -m unittest discover -s tests/window_provenance`

Two real xdg_toplevels have the SAME synthetic app-id and immutable SHM buffers.
Two independent Wayland observer CONNECTIONS (same test process) enumerate their
ext_foreign_toplevel_list identifiers. Each must see2distinct valid IDs; both
sets must agree. Destroy A: both observe1closed handle and the same survivor.
Recreate A with same app-id/title: both see2matching IDs, exactly1new identifier,
and the retired ID absent. Passed in5bounded iterations. Titles are synthetic
and ignored by observers; no identifiers/PIDs/text are exported.

Own signed Sway1.12/wlroots0.20.2/pixman in nohost/noGPU/nonetwork bwrap. Runtime
libraries are hash-sealed; XML/source/binary inputs pinned in build record. Child
CPU6s/core0/FD128/file2MiB; outer15s, probe8s/ownalarm6s, outputread4KiB. The file
limit permits bwrap to materialize the largest pinned library (~1.4MiB); initial
64KiB limit failed BEFORE compositor start and was corrected, not counted as a
passing test. Cleanup affects only own namespaced tracked children.

This is real native-window identity/lifetime evidence on reference Sway ONLY.
Fcitx is not loaded; fieldBindingVerified=false. No text-input/keyboard/device
injection, AT-SPI, installed IME, Slack, system config, autostart or host display.
It proves neither Hyprland support nor PID/AT-SPI field/Fcitx context mapping.

Claude implemented client/lifecycle; Codex reviewed and wrote runner/boundaries,
protocol C compilation, bounded handle summary and listener initialization.
Gemini reviewed lifecycle code. Accepted immediate closed-handle destruction and
orderly stop/finished cleanup; rerun passes observerCleanup=true. Rejected its
ID-reuse objection: installed protocol explicitly forbids reuse. Its early-close
scenario is outside this controlled test (initial done observed before destroy);
handle-cap failure is intentional fail-closed, not silent success. Independent
wrapper reviewer hit usage limit: no review credited for this new wrapper.
Evidence: data/window-provenance-001-result.json.

Registry-only positive/negative control: `--run --registry-only` on own Sway
advertised stableListVersion1/wlrManagerVersion3/hyprlandMappingVersion0; binds
NO window globals. This verifies the separate reader mode without a host socket.
No host probe permission implied. data/registry-private-control-001-result.json.
