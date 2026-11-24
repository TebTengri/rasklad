# Aquamarine DMA-BUF readiness experiment (SYS-246)

Status: PREPARED, NOT BUILT, NOT RUN. Author: Claude (implementation). Review/run: Codex.
No GPU, host display, installation or system change. Patched tree/library only in ignored `build/`.

## 1. Review of control 001 (`build/hyprland-feedback-parent-001.json`)

Accepted facts: with installed Hyprland 0.56.2/Aquamarine 0.15.1, *immediate* feedback is
dispatched (main_device, done) inside `initDmabuf`; *deferred* feedback (sent after the next
sync's callback `done`) is not, and startup ends in the same `no allocator available`/SIGABRT.

Interpretation limits found:

1. **Terminal symptom does not discriminate.** Both variants end `allocatorUnavailable`/`-6`.
   Immediate fails because `drmGetDeviceFromDevId` cannot resolve the fake dev_t (and no `/sys`
   exists in the sandbox, so even a real dev_t would fail). Only `mainDeviceSeen`/`feedbackDone`
   discriminate.
2. **Deferred mode is legal but not wlroots-like.** linux-dmabuf says parameters are "sent once
   when this object is created" and are atomic only at `done`; no latency bound
   (wayland.app/protocols/linux-dmabuf-v1). wlroots is *expected* to send the batch
   synchronously inside `get_default_feedback`. NOT verified here: gitlab.freedesktop.org blocked
   the fetch. Until verified, 001 shows an Aquamarine readiness weakness, not trial003's cause.
3. **Trial003 vs 001 gap.** Trial003 had no `Got main device` while our own Sway client received
   feedback. If wlroots sends synchronously, a single nested roundtrip should have seen it.
   Unexamined alternatives: a difference between our client's connection and Hyprland's (global
   filter, bind version, global order — the synthetic advertises dmabuf last), or the Wayland
   backend failing earlier. Trial003 markers lacked `Wayland backend cannot start`,
   `could not start` and `Got done`. Their absence is therefore not evidence.
4. Upstream nit, out of scope: the `format_table` fd is never closed in `setFormatTable`.

## 2. Source evidence (Aquamarine v0.15.1, BSD-3-Clause, © 2024 Hypr Development)

- `CWaylandBackend::start()` binds globals in the registry callback; binding
  `zwp_linux_dmabuf_v1` calls `initDmabuf()` *inside* the `start()` roundtrip (nested).
- `initDmabuf()`: sets done/main_device/format_table listeners, one `wl_display_roundtrip`, then
  `if (!drmState.nodeName.empty())` opens the node; otherwise returns **true** silently.
  `setDone` is a no-op log. `setMainDevice` ASSERTs the array size; on resolve failure it only logs.
- `Backend.cpp` `CBackend::start()`: allocator only from an implementation with `drmFD() >= 0`,
  otherwise `Cannot open backend: no allocator available`.

## 3. Patch (`tools/aquamarine_readiness_patch.py`, MIT; embedded upstream anchors BSD-3-Clause, full notice in file)

The tool edits only `src/backend/Wayland.cpp`, with 4 exact anchors. Each anchor must match once,
otherwise nothing is written. The tool refuses a checkout outside `build/` and refuses a tree that is
already patched. Every component below `build/` is opened with `O_NOFOLLOW` relative to its parent
(symlinked ancestor refused); the target must be a regular single-link file and is replaced by an
atomic rename of a unique sibling temp file, never truncated in place. Regression:
`tests/aquamarine_readiness/test_patch_confinement.py`. Headers are unchanged, so the ABI is unchanged.

- Shared `SRaskladFeedbackReadiness{done, mainDevice, invalid}` is captured by the listeners.
- A malformed main_device size sets `invalid` instead of hitting `ASSERT`.
- After the existing roundtrip, if `done` is missing: a `prepare_read`/`poll`/`read_events`/
  `dispatch_pending` loop on the display fd runs until `done` or a 2000 ms monotonic deadline.
  It never sleeps and handles EINTR.
- Fail closed (`return false` → `zwp_linux_dmabuf_v1 init failed`) with explicit `[readiness]
  refused: …` on: display error, no `done` by the deadline, malformed or missing main_device,
  unresolved node.
- An unconditional `[readiness] checking initial feedback` log proves the patched library loaded.

### 3.1 Source review follow-up (Grok, findings 3–4; source only, not run)

- **Null `wl_array` (#3): not a vulnerability.** libwayland `wl_connection_demarshal` always sets
  `args[i].a` to a preallocated `closure->extra` slot (`data = p`). Array args have no nullable path.
  The hyprwayland-scanner trampoline passes it unchanged. The compositor controls only `size`, which
  was already checked. A tiny `!deviceArr || !deviceArr->data` guard was added as defence in depth.
- **Raw `this` after refusal (#4): rejected, no change.** The closures live inside
  `waylandState.dmabufFeedback`, which is owned by `CWaylandBackend`. `~CWaylandBackend` resets it
  before `wl_display_disconnect`. The generated destructor sends the `destroy` destructor request
  (marshal flag 1 = destroy proxy), so no later event reaches it. Events are dispatched only on the
  backend's own display. Upstream already captured `[this]` and returned false on the same path
  (node open failure). The patch adds a by-value `shared_ptr` only; it adds no new lifetime.
  A `weak_ptr` mechanism would not fix any reachable state.

## 4. Control extension (`tools/hyprland_feedback_parent.py`, schema 2)

- Three modes: `immediate`, `deferred`, `never` (batch withheld → must hit the deadline).
- `--aquamarine-lib` read-only binds the fixed `build/aquamarine-readiness/lib`, which must hold
  exactly one regular `libaquamarine.so*` plus local symlinks. `LD_LIBRARY_PATH` applies to the
  Hyprland child only.
- New markers: `patchLoaded`, `readinessComplete`, `refused*`, `waylandCannotStart`, and
  `refusalBeforeAllocator` (byte offset of the first refusal < offset of the allocator abort).

## 5. Exact commands for Codex (bounded; stop at first failure)

```sh
D=build/aquamarine-readiness            # must not exist yet
mkdir "$D"
git clone --depth 1 --branch v0.15.1 https://github.com/hyprwm/aquamarine.git "$D/src"
git -C "$D/src" rev-parse HEAD > "$D/upstream-commit.txt"   # compare with Arch 0.15.1-1 PKGBUILD pin/patches
python3 -I tools/aquamarine_readiness_patch.py "$D/src" --check
python3 -I tools/aquamarine_readiness_patch.py "$D/src"
git -C "$D/src" diff --stat            # expect: only src/backend/Wayland.cpp
git -C "$D/src" diff > "$D/readiness.patch"
# Build runs upstream CMake/codegen: confine it (no network, ro /usr, rw only $D).
timeout 900 bwrap --unshare-all --die-with-parent --new-session --cap-drop ALL --clearenv \
  --ro-bind /usr /usr --symlink usr/bin /bin --symlink usr/lib /lib --symlink usr/lib /lib64 \
  --ro-bind-try /etc/ld.so.cache /etc/ld.so.cache --proc /proc --dev /dev --tmpfs /tmp \
  --bind "$PWD/$D" /w --chdir /w --setenv PATH /usr/bin --setenv HOME /tmp -- \
  sh -c 'cmake -S src -B cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build cmake --target aquamarine -j4' \
  > "$D/build.log" 2>&1; echo "build=$?"
mkdir "$D/lib" && cp -P "$D"/cmake/libaquamarine.so* "$D/lib/"
readelf -d "$D"/lib/libaquamarine.so.*.* | grep SONAME; readelf -d /usr/lib/libaquamarine.so | grep SONAME
diff <(nm -D --defined-only "$D"/lib/libaquamarine.so.*.* | awk '{print $3}' | sort) \
     <(nm -D --defined-only /usr/lib/libaquamarine.so | awk '{print $3}' | sort) && echo abi-symbols-equal
# Same supervisor/cgroup limits + inactive-cleanup check as run 001:
python3 -I tools/hyprland_feedback_parent.py --run                  > build/hyprland-feedback-parent-002-baseline.json
python3 -I tools/hyprland_feedback_parent.py --run --aquamarine-lib > build/hyprland-feedback-parent-003-patched.json
```

If a build dependency is missing (e.g. `hyprwayland-scanner`, `wayland-protocols`): STOP and report
blocked. Installing is not permitted. Three variants take ≈3× the runtime of 001; widen the
supervisor's time bound to match, not the per-child bounds.

## 6. Acceptance

All variants, both runs: `serverError=false`, `logBoundExceeded=false`, `nodeOpened=false`,
`readinessComplete=false`, `childExit != -9` (no kill on timeout), `getDefaultFeedback=1`.

| run / mode | patchLoaded | mainDeviceSeen | feedbackDone | deviceResolutionFailed | expected refusal | refusalBeforeAllocator |
|---|---|---|---|---|---|---|
| baseline immediate | F | T | T | T | none (allocatorUnavailable T) | F |
| baseline deferred | F | F | F | F | none (allocatorUnavailable T) | F |
| baseline never | F | F | F | F | none (allocatorUnavailable T) | F |
| patched immediate | T | T | T | T | `refusedUnresolved` only | T |
| patched deferred | T | **T** | **T** | T | `refusedUnresolved` only | T |
| patched never | T | F | F | F | `refusedDeadline` only, `feedbackWithheld=1` | T |

Patched runs must also show `waylandCannotStart=T` and `aquamarineOverride=T`. If a baseline
row differs from this table, the control is invalid; stop before interpreting the patched run.
If the patched run is all-F on `patchLoaded`, the override did not load, so the run is invalid.

## 7. What a pass does and does not show

A pass shows that the bounded wait processes a batch arriving after one roundtrip, refuses explicitly
when the batch never arrives or main_device cannot be resolved, and does all of this before the generic
allocator abort. It does **not** show that trial003's failure was a timing problem, that wlroots
delays feedback, or that the patch starts Hyprland on a GPU. No GPU trial is authorized.
Next no-GPU discriminator if needed: a synthetic mode advertising dmabuf first (wlroots order) and
binding-version checks (§1.3). Any upstream submission or GitHub comment needs an owner decision.
