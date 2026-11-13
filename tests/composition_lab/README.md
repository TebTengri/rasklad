# Isolated composition prototype (SYS-223)

Developer-only; not linked into the application, installed or enabled at login.
Run `bin/test-composition-lab` from the repository. This builds the pure composer
and synthetic unit suite only, with Qt Core/Test and C++17. It does not open an
input connection or load dictionaries/models.

The composer owns an unfinished word (at most 32 letters), emits preedit, and
runs the existing SafeConfirmed router at Space. Unsupported characters and
oversized words are committed literally. Backspace edits only owned preedit. Navigation/modified keys first finalize
owned preedit literally, then pass through without being filtered.
The adapter must pass through unhandled keys and manage focus/lifecycle safely.

`fcitx_engine.cpp` is an experimental adapter to the **already installed**
Fcitx5 5.1.23 public APIs; its optional qmake target requires C++20 because of
installed headers. Build the core first, then:

```
qmake6 -o build/fcitx-engine/Makefile tests/composition_lab/fcitx_engine.pro
make -C build/fcitx-engine -j2
```

Do not install this plugin or point a desktop Fcitx instance at it. Its factory
checks fixture marker/runtime paths, but these checks are not a security sandbox.
It has fixed US-layout and synthetic evidence, not a real detector. It requires
Preedit and ClientUnfocusCommit, rejects protected flags before inspecting keys,
and has per-input-context state. Counter output contains aggregate metadata,
never keys or text; it is intended only for the closed synthetic fixture.
The installed public Fcitx headers identify LGPL-2.1-or-later; no upstream source
or binary has been vendored. This does not constitute a distribution license audit.

The private compositor runner is not distributed (GPL-dependent developer
fixture). See `docs/INPUT-LAB-005.md` for observed integration results. These
runs use ordinary Qt Quick fields and the installed Fcitx Qt plugin on a private
D-Bus, not editor-specific acknowledgements. Private-bus setup, child process
limits and a closed bwrap environment are essential to the observations.

Unfinished: arbitrary layouts, cursor/selection changes, shortcuts with pending
preedit, shutdown/crash/lock, application compatibility and real detector
integration. A passing fixed-field experiment does not admit global input.
