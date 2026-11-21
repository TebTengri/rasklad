# Private Hyprland mapping experiment

The native client accepts `--mapping`. Each of its two independent observer
connections binds EXT list v1, wlr manager v3, and Hyprland mapping manager v1.
It requests an address for every owned, complete EXT and wlr handle and requires
a one-to-one address match within each observer, then equal stable-identifier to
address maps across observers. Addresses remain in memory and are never printed.

The initial two windows, destruction of A, and recreation of A must converge.
Both streams must report exactly one closed owned handle, B must retain its
address, and recreated A must receive a fresh stable identifier. A recycled
compositor address for recreated A is permitted; it is not a stable identifier.
Protocol handles are observer-local and never compared across connections.
Missing required globals, removed required globals, mapping failure, duplicate
addresses, or incomplete lifecycle observations prevent success.

`python tests/window_provenance/run.py --build --mapping` builds the client with
all protocol dependencies. `run.py --mapping --run` deliberately refuses with
`mapping-private-hyprland-runner-required`: the existing private Sway launcher
cannot prove Hyprland mapping support. The private Hyprland launcher must invoke
`/lab/probe --mapping` with its private socket at `/run/lab/wayland-lab`.

A strict compile is only build evidence. Mapping correctness requires an actual
private Hyprland run completing every phase. This experiment does not load
Fcitx or establish input-field identity or authority.

Optional --feedback-fd=N uses only an already opened own-parent connection. It checks default DMA-BUFv4 feedback and resolves main_device metadata to the fixed admitted render node, without GPU open or raw output. This is a diagnostic control, not mapping proof.
