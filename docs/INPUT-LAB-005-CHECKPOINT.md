# INPUT-LAB-005: checkpoint, 2026-11-13

Developer-only composition experiment; no production input backend enabled.

## Observed

- The composer buffers up to 32 letters as preedit and invokes the existing SafeConfirmed router at Space. Unsupported characters and oversized words are preserved literally. Deterministic synthetic evidence only; no dictionary/model files read.
- Unit suite: 10 passed, 0 failed. Includes backspace, missing evidence, long words, Unicode scalars and invalid input.
- Private Qt Wayland fixture: baseline, fast tail, known word, backspace and overflow passed (5 cases). No retroactive deletion or custom editor ACK.
- Focus-to-password case fails acceptance: the old field retains pending preedit rather than committed original text. This is not proof of observed text deletion, but pending text is not safely finalized. No production admission.
- Own Fcitx5 adapter builds against installed 5.1.23 public APIs (C++20 for this optional target; composer/core C++17). Compilation is not a runtime validation. Private-bus launcher and framework scenarios remain unfinished.
- Native assertion client builds. New Fcitx scenario assertions have not been run.
- Caramba is the owner's macOS UX reference. No proprietary code/assets copied and no claim about its internal implementation.

## Resume

1. Build the private D-Bus/Fcitx launcher inside the existing closed bwrap fixture, with a fixed addon allowlist and synthetic native keyboard events.
2. Run baseline and focus-change first; then fast tail, known word, backspace and overflow. Preserve failures; do not patch editors to force success.
3. Check password/sensitive exclusion and lifecycle/shortcut edge cases before considering any real desktop input.
4. Add reproducible developer instructions, independent review and a draft PR. Keep SYS-223 In Progress.

## Boundaries and location

Working branch: SYS-223/composition-prototype, worktree /tmp/rasklad-compose-20261112, based on 222c6d7 (PR #13). Private GPL-dependent compositor and aggregate attempts remain local in ignored build/; they are not distributed. A local checkpoint archive preserves these fixture files for continuation. No host input, clipboard, GPU, network, home-directory access or desktop configuration changes were enabled. No autostart, installation or service added. This work is not merged or released.

Other pending work: PR #13 input lab, PR #14 lookup optimization, PR #15 retro sound. Do not mark them merged or hardware-validated without checking their current state.

## Resumed: integration result

Superseded by INPUT-LAB-005.md and INPUT-LAB-005-results.json: private Fcitx integration now ran, all six cases passed, and direct focus negative control still failed as expected. Unit tests remain 10/10. Next: lifecycle/navigation/shortcuts and a second ordinary field, then independent review; production input remains disabled.

## Navigation and performance follow-up

Commits after 5af653c include 9/9 navigation integration cases and 11 composer units, then five alternating performance pairs plus four private daemon stop/kill continuation/focus cases. See INPUT-LAB-005-performance.md for limits: synthetic evidence, editor-state timing only, daemon-only RSS/CPU, pending preedit persists until subsequent action. Next: real detector measurement requires separate data authorization; independent review and remaining lifecycle/shortcut/client gates. Global input remains disabled.

## Save/Close finding and candidate

Read INPUT-LAB-006.md before continuation: external composition fails Close and Save after kill despite prior typing/focus passes. New own in-process component passes six fixed-field integration cases and ten reported offscreen unit passes; it requires supported-client integration and is not universal. No production input enabled.

## Joint decision and exact next task

Read ARCHITECTURE-DECISION-002.md. Claude Opus 5.5 and Codex proposals compared: primary direction is commit-first Fcitx, not editor-by-editor plugins. The minimal candidate preserves native typing/Save/Close but refuses correction because Qt/Fcitx drops SurroundingText capability after valid queries. No delete/commit order proven. Next SYS-241: one minimal regression/candidate fix for context-update semantics in workspace only, preserving focus/password invalidation; Claude prepares, Codex reviews. Do not expand matrices, force refresh or install in the live session.

## Latest product clarification

Read PRODUCT-DIRECTION-003.md first. Priorities: email, messengers, editors and terminals; AI chat is not the only/primary reason. Terminal support required. Shared local core + trusted field adapters proposed as primary; device grab/setgid/password heuristics rejected; SYS-241 deferred. No live installs/permissions or new model reads. Next: reviewable message-field integration and a genuine terminal buffer contract, no standalone composer substituted for native TUI.
