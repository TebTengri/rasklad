# Actual-Fcitx drain fixture (experimental)

This peer emits only a fixed synthetic Space press/release. It does not connect
to the desktop, Slack, devices or a real user's session. `run.py` is inert by
default. `--build` builds the peer, not Fcitx; `--run --variant baseline|drain`
requires a separately built frontend and admitted, pinned runtime binaries.

The frontend recipe in `tools/fcitx_frontend_build.py` uses the source archive
identified by `data/fcitx-frontend-source-lock.json`, verifies its extracted
files, and builds inside a namespace. It never installs the result. Upstream
Fcitx remains LGPL-2.1-or-later; the project recipe and synthetic peer are MIT.
No modified Fcitx source or drain implementation is supplied by this fixture.

The test deliberately puts a press on the retiring grab and a release on the
replacement grab. It expects the unmodified baseline to lose the press and a
future drain variant to preserve it. **These expectations have not yet been
validated against actual Fcitx.** A toy wire test is separate evidence.
The selected frontend must be mapped in the owned Fcitx child by device/inode;
the server stays alive until that inspection completes. Only the tracked
virtual keyboard may report events, and resource loss invalidates the scenario.
After release, a 100 ms observation window checks for duplicates; this makes
no claim about arbitrarily delayed events. Outer process deadlines and cleanup
bound the complete test. No GUI focus, password-field or real-app guarantee.

Checks performed on 2026-11-15:

- Peer compilation succeeded; actual Fcitx was not started.
- Five report-reader/private-entry tests passed:
  `python3 tests/fcitx_drain_server/test_runner.py`.
- Independent source review identified mapping lifetime, resource lifetime and
  event attribution defects; Codex fixed them and a second review found no
  unresolved blocker in those changes. The reviewer ran no tests.
- Full build/run remains blocked in this environment: installed runtime
  binaries and bwrap appear owned by uid65534, rejected by existing admission.
  Keep that admission intact and resume on the ordinary admitted machine.
