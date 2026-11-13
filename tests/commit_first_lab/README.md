# Commit-first Fcitx candidate

Developer-only, not installed or enabled in the working session. Build the own
core, then:

```
qmake6 -o build/commit-first-engine/Makefile tests/commit_first_lab/fcitx_commit_first.pro
make -C build/commit-first-engine -j2
```
 Installed
Fcitx headers require C++20; the own core remains C++17. No foreign source/data
was copied. The adapter only admits the private native_client/dbus fixture.

Native letters are never filtered or held in preedit. A bounded tail is cleared
on interruption/reset. On unmodified Space it refuses protected/disabled/terminal
or unapproved contexts before inspecting the surrounding cache. A matching token,
end cursor, empty selection and second snapshot check are required. This is not
an atomic editor protocol: the external delete+commit path remains experimental.

Current result: Save/Close preservation passes; actual correction has NOT passed.
The tested Qt/Fcitx context flag is false at Space, so zero cache reads and edits
occur. Read-only diagnostic metadata is bounded; no text/keys are logged. See
 docs/ARCHITECTURE-DECISION-002.md for the selected direction, limitations and
exactly one next task. Do not bypass the missing capability to get passing tests.
