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
