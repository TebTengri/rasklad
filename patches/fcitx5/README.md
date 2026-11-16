# Fcitx 5.1.23 experimental patches

`0001-lab-grab-release-without-local-destroy.patch` is an unused ownership
primitive. It is **not a frontend fix** and is not applied to the installed IME.
It adds an explicit server release without local proxy destruction. The caller
must retain the entire C++ wrapper, listener target and signal connections until
an asynchronous `wl_display.sync` callback on the same event queue. Destruction
then destroys only the local proxy, avoiding a second server release.

No caller or frontend integration is included. This primitive alone does not
forward missing keys, choose the right focus, handle modifier/repeat state or
ensure complete press/release pairs. It must not be enabled in a normal session.

Target provenance is pinned in `data/fcitx-frontend-source-lock.json`:
Fcitx 5.1.23 commit `27cca6e0239938b2d53937061d0980c087856b22`.
The patch modifies LGPL-2.1-or-later upstream code and retains that license;
upstream copyright and REUSE metadata remain applicable. It does not change the
MIT license of rasklad's own core. Upstream license text:
https://github.com/fcitx/fcitx5/blob/27cca6e0239938b2d53937061d0980c087856b22/LICENSES/LGPL-2.1-or-later.txt

Authorship: Codex wrote the patch. An independent fresh-context reviewer found
no concrete blocker under the stated ownership contract, by source inspection
only. Patched wrapper passed C++20 syntax compilation against the installed
matching Fcitx SDK. Backend ownership/queue handling remains unimplemented and
unreviewed. Full private build/runtime remains blocked by environment admission.

Regression command: `python3 tests/fcitx_drain_server/check_wrapper.py --run`.
It checks source/patch hashes, copies the two upstream files into a fresh private
build directory, applies the patch there, and compiles an intercepted C API test.
It never applies patches to the source archive/tree or installed frontend.
Two ownership paths and live-listener dispatch passed; exact limits and author
record: `data/wrapper-ownership-001-result.json`. Claude was asked for this small
test but timed out after 65 seconds without output; Codex implemented the test.
The independent reviewer did not run tests. The compiler and test run locally;
this is not the namespace-isolated full-frontend build or its ABI admission.

## Bounded retirement diagnostic (not a delivery candidate)

Patches `0002` and `0003` add listener-depth tracking and private backend
retirement. Apply after `0001`; hashes: `retirement-lock.json`.
Retired input and reentrant key/repeat handoff deliberately abort the lab125.
The initial raw-forward proposal was rejected on independent review; this
candidate must not be presented as a lossless correction fix.
Current evidence/next decision: `docs/FCITX-RETIREMENT-REVIEW-001.md`.
Checks: `python3 tests/fcitx_drain_server/check_wrapper.py --run --depth` and
`python3 tools/fcitx_retirement_candidate.py --syntax`.

## Separate retained-grab client candidate

`keep-grab-lab.patch` and then `keep-grab-cancellation.patch` apply directly to the
pinned original Fcitx tree, separately from patches0001–0003. They retain upstream
LGPL-2.1-or-later licensing. Source/patch hashes: `keep-grab-lock.json`.
This private candidate passed actual peer and closed native app controls;
`docs/FCITX-KEEP-GRAB-001.md` records scope and open release gates. Build using
`python3 tools/fcitx_frontend_build.py --build --variant keep-grab`.
`keep-grab-control` omits cancellation and intentionally reproduces duplicate up;
it is an experimental negative control, never a release candidate.
