# In-process client adapter experiment (INPUT-LAB-006)

Run `bin/test-client-adapter-lab`. It builds C++17 with Qt Core/Gui/Qml/Quick/Test
and the own core, then runs ordinary Qt Quick fields offscreen, without desktop
input or Fcitx addon activation. Eight cases plus QtTest init/cleanup pass (10
reported passes). This lab is not linked into the application or installed.

`ClientAdapter` is a QObject event filter **inside the fixture client**. Only two
explicit object pointers are approved. Ordinary keys remain native committed
input. On an unmodified, non-repeating Space, it checks hints before querying
text, rejects selection, arbitrary cursor fragments and unsupported/long tokens,
then invokes the existing SafeConfirmed router with a borrowed evidence provider.
Before editing it rechecks focus, hints, preedit and the snapshot. The edit is one
synchronous QInputMethodEvent on the same GUI dispatch, including Space. The
postcondition is checked; an unexpected editor response disables further edits,
without guessing a rollback. Unhandled keys pass through. There is no preedit
buffer, global hook, IPC, clipboard or device access in this adapter.

This is **not a universal Wayland backend**. It requires integration inside a
cooperating application; it must not be injected into unrelated applications.
The approved fields are fixed trusted Qt Quick editors with known behavior. No
claim of atomicity or safety in arbitrary reentrant handlers is made. Testing
uses one-line synthetic text and a fixed US-layout assumption. The 4096 limit
checks a received surrounding-text snapshot; it is not a hard bound on memory
needed to retrieve a large document. Real provider blocking, arbitrary large
fields, multiline query coordinates, real application lifecycle and layout
changes remain unverified.

Tests cover committed input before Space, correction, password/unapproved
exclusion before text/evidence access, cursor/selection refusal, native Ctrl+A,
missing evidence, focus change and a new sensitive hint during evidence lookup,
and overlong token refusal. Only synthetic evidence is used. The independent
private compositor cases and preedit failure controls are in docs/INPUT-LAB-006.md.

The new adapter/test translation unit also passed the same suite under ASan,
UBSan and LeakSanitizer in a closed namespace. System Qt and the prebuilt core
were not instrumented by that run. This is not an audit of arbitrary clients.
