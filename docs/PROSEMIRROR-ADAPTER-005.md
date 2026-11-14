# Transactional editor adapter

2026-11-13, SYS-242 / draft PR17. `browser/prosemirror.js` implements the existing
session adapter interface for an actual ProseMirror EditorView. It is owner-side
integration, not automatic injection into arbitrary applications. Native core and
model/policy thresholds are unchanged.

The owner supplies the installed history Plugin instance, the public
Plugin/PluginKey/closeHistory/isHistoryTransaction APIs and a semantic range
classifier. Once the shared local session is ready, the owner constructs its editor
with `adapter.plugin` and explicitly registers the view as US prose. Unregistered,
unknown, secret, command, code, read-only and composing contexts do not query the
detector. Before bounded text extraction (33 prefix characters and one lookahead),
the adapter checks owner classification, document node/mark metadata, code/link
protection and credential-form hints. Stored code/link marks also decline.
Near a protected inline/atom span in that small window it conservatively declines.

The snapshot is the actual immutable EditorState identity, including selection.
Changing state or revoking registration during detection prevents the pending edit.
Only the latest authorized state gets one native editor transaction. Replacement
runs preserve their individual marks, including mixed styles/noninclusive
annotations; Space receives the original cursor's native insertion marks. Exactly
matching text positions are used; no DOM text rewriting, clipboard, replay,
execCommand, private view internals or custom input IPC.

Correction is separated from previous and following typing by the editor's own
history boundaries. Undo returns the original word, Redo restores correction;
the first Space after a history operation is not immediately corrected again.
No typed token/document is cached by the adapter. ProseMirror's normal editor-owned
Undo history remains unchanged in purpose.

A proven no-op/throw before state changes allows ordinary Space and disables the
adapter. An unexpected owner write or stale state is preserved without guessed
rollback. That exceptional Space can be consumed to avoid the framework applying
stale positions; subsequent ordinary typing continues. This is a documented safety
tradeoff, not a guarantee of lossless interaction with arbitrary custom dispatchers.
Reconfigure of the plugin list retains registration. Destroyed views are recognized
through public isDestroyed; WeakMap entries do not keep them alive. Explicit
unregister/dispose revokes pending edits and leaves ordinary editor behavior active.

## Sources and licensing

Pinned fixture versions are in `tools/prosemirror/lock.json`: six ProseMirror
packages plus OrderedMap/RopeSequence. Npm archives are locked by SHA-512 and
SHA-256; actual ESM code and complete LICENSE bytes have separate pins. Registry
reported gitHead is recorded as metadata, not claimed as independently attested.
All eight LICENSE files were read before execution. Three byte-distinct full MIT
notices are retained in data/notices and per-package copies beside ignored fixture
code. Model, dictionary and Hunspell/runtime licensing remains separate; real data
and engine distribution are not approved by these editor-code notices.

`python3 bin/prepare-prosemirror` verifies cached code only. Explicit `--fetch`
acquires the pinned archives with bounds/no links and never executes npm lifecycle
scripts. The fixture rewrites ESM specifiers into deterministic data URLs and removes
source-map URL comments for offline closed loading; MODIFICATIONS and sources are
kept beside every generated module map. The 5 MB nested module-map encoding is a
developer transport, not a shipping overhead claim. The adapter itself is 7,640
unminified source bytes; applications already owning ProseMirror provide their own
instances/modules. No third-party implementation code is vendored into Git.

## Verification

```sh
python3 bin/prepare-prosemirror
node tests/browser/prosemirror.test.mjs
python3 bin/test-reviewed-dictionaries --prosemirror
# Explicitly scoped real integration only, no normal build/test auto-run:
python3 bin/test-reviewed-dictionaries --real --prosemirror --input-root /home/mvn/Work/rasklad
```

The genuine upstream editor runs only in nested networkless Chromium, inside the
existing outer bwrap with six exact raw/model/stem inputs and notices. No account,
workspace, private text, host profile, seat or GPU is accessed.

Real-profile result: 12 fixed base cases and 25 additional checks passed. They cover
native Undo/Redo and following typing, mixed/stored/noninclusive formatting, owner
role/protected ranges, IME flag/read-only/selection guards, stale or unexpected owner
transactions, revocation, plugin reconfigure, document serialize/destroy/recreate and
dispose. ExecCommand calls and network attempts were zero. Ten Node adapter privacy/
lifecycle tests, eleven session tests and eighteen Python package/oracle tests pass.
The real core's hash/init was 192.4 ms in one run after editor imports; WASM heap
19.25 MiB plus JS model/stems/editor/browser overhead. This is neither keystroke
latency nor an independent linguistic accuracy benchmark.

Aggregate/source hashes: data/prosemirror-editor-001-result.json. Slack was selected
as the first external application afterwards, with native desktop as the target.
This result does NOT establish current Slack composer engine/version or integration.
See SLACK-DESKTOP-001.md for the separate compatibility gate.

Public API references: https://prosemirror.net/docs/ref/ and the pinned package
TypeScript declarations/source in the verified local archives.
