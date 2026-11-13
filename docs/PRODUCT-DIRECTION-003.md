# Product direction 003: messages first, terminal support required

2026-11-13. Proposed implementation direction after the owner's clarification;
not a claim of completed compatibility or a permission change.

## Goal and priorities

Deliver useful automatic RU/EN wrong-layout correction while typing, better than
analogs in measurable supported workflows. Primary surfaces: email, messengers,
editors and terminals. Codex/Claude often already understand wrong-layout messages;
fixing AI prompts alone is not the product. Terminal support remains required,
including TUI prose fields, rather than an unexplained blanket terminal exclusion.
No selection/Apply or post-Enter correction is substituted for live correction.

Keep local processing, licensed data, predictable Undo, normal input on failure,
no secret-field processing and low measured overhead. Existing experiments are
useful evidence, not a substitute for a daily workflow. No universal zero-risk
claim follows from passing a fixture or an advisor's confidence.

## Reassessment with Claude Opus 5.5

Two public-only reviews were run with tools/MCP/customizations/session persistence
disabled: a cold product reset and a terminal-specific review with criticisms.
Raw briefs/responses remain locally in the checkpoint.

The cold review proposed an exclusive evdev/uinput broker, a privileged device
setup and password heuristics. These were **not accepted**: a hung grabbed-input
broker can impede ordinary typing, device access is powerful, and Enter-vs-Space
is not a valid secret classifier. No setgid/group/udev/grab configuration was
created. That proposal is retained as a rejected hypothesis, not an instruction.

The terminal review instead recommended trusted buffer adapters. Codex adopts
that principle, adjusted to the latest message/email priorities: one shared local
engine, a small number of integration layers, and correction inside the actual
field owner. It does not mean every terminal or closed app is already covered.
Claude's claims of zero risk, universal absence of races, and Enter macros at a
shell command prompt are not accepted as established guarantees.

## Options and recommendation

| Option | Useful reach | Unresolved limitation | Disposition |
| --- | --- | --- | --- |
| Global device/compositor broker | Observes keyboard activity across many apps | Does not inherently know terminal buffer/secret semantics; privileges or session-failure risk | Not selected as default |
| A single IME/Fcitx backend | Existing framework integration for supporting clients | Context availability and external edit ordering; arbitrary raw TUI semantics absent | Optional future GUI route, not the whole product |
| Shared engine + trusted field adapters | Actual editor state, native edits/undo, no whole-system keyboard interception needed | Integration work and unsupported noncooperating applications | Recommended primary direction |

Engine: retain the own converter/router and evaluated detector approach. Prefer
an embeddable library (and a browser-compatible build if practical), so adapter
calls need not send words to another process. Browser packaging/portability is
not implemented or benchmarked yet. No model/corpus is reloaded under the old
one-shot authorization. No foreign code/data is copied without license review.

Adapter contract (proposed, not an existing OS standard): approved field role
`prose/command/secret/unknown`, field generation, document revision, cursor/range,
expected token, and the host's replacement/undo operation. Only prose reaches the
engine. The owner validates freshness and applies the change in its editing
transaction; ordinary letters are committed rather than held in external preedit.
A stale result is discarded. Unexpected behavior disables correction in that
field without guessed rollback. This contract itself still requires testing.

Common layers reduce duplicated work: a browser integration can cover standard
message inputs across sites; native toolkits/editor APIs are separate routes;
shell/TUI buffer adapters cover terminal programs that cooperate. Rich/custom
editors need explicit compatibility tests. A generic DOM value assignment is not
assumed compatible with React, rich text, Undo or native IME composition.

The desktop controller keeps status, settings and, later, limited layout control.
Any native layout bridge should pass only constrained metadata, not typed text.
Its focus/lifecycle and installation permissions still need design and review.
No arbitrary-command endpoint, root daemon or automatic input-device grant.

## Terminal model

A terminal emulator is not the editable document of every child program. Bash
owns a Readline command line; a full-screen chat TUI owns a different buffer.
Raw/noecho modes do not by themselves declare a password. Shell prompt markers
identify prompt/execution phases, not every child's secret/prose mode. Untrusted
OSC output cannot authorize correction.

- Shell: commands, paths and executable syntax remain unchanged by default.
  An explicitly owned prose input field can use the shell's editing API. Do not
  implement prose mode by an Enter macro that might execute the original line if
  a helper fails; a dedicated read operation must never evaluate the text.
- Editors in terminals: use native buffer/version/undo APIs and allowed prose
  contexts. Code and command-line/secret prompts are excluded.
- Chat TUIs: live correction needs access to the actual prompt buffer before
  submission. Existing submit hooks or an external-editor shortcut do not meet
  that requirement. A real extension point or reviewed upstream integration is
  needed; Codex support does not imply Claude support.
- Remote/closed/uncooperative TUIs remain a limitation until a trusted adapter or
  protocol exists. Do not silently fall back to blind deletion or password guesses.

This removes some analog limitations by providing real terminal integrations;
it cannot honestly promise every arbitrary program without cooperation.

## One next product slice

[SYS-242](https://linear.app/systemic-kz/issue/SYS-242/sdelat-skvoznuyu-avtozamenu-v-pole-soobsheniya-s-lokalnym-yadrom): build a **reviewable message-field adapter** around the common engine, first for
browser message/email fields, with tests in a disposable profile and no real
message sending. The browser route is a proposal; no extension is installed yet.
Native mailbox/messenger clients are not automatically covered by this result.

Acceptance: genuine automatic correction in the editable message field, intact
next keystrokes, native Undo, unchanged code/URLs/commands, no processing of
password/credential/unknown fields, and no lost text at save/close or adapter
failure. Measure real path latency/memory; old Fcitx timings do not apply. Keep
permissions narrowly scoped and prepare installation/removal for review before
requesting any live-profile change. Real detector/data access needs a concrete
separate authorization; a hardcoded demo is not language-accuracy acceptance.

In parallel design the terminal adapter contract against one actual shell/TUI
buffer API; do not label a standalone composer or Ctrl+G editor as native TUI
support. The terminal milestone stays in the product criteria, not indefinitely
excluded. No new broad compositor laboratory or Qt plugin fork is the critical
path of this slice.

SYS-241 remains a deferred technical issue; Architecture decision 002 no longer
sets the sole product roadmap. Earlier experiments/negative results remain in
PR16 as research, not deployable code. No production runtime or system changes
were made during this reassessment.

## Primary references and what they establish

- [WaySwitch](https://github.com/Wetoshkin/WaySwitch): its README documents evdev/
  uinput replay, partial-edit risks, password metadata limits and default terminal
  exclusion. This review did not run it or prove security/compatibility locally.
- [goswitch](https://github.com/Djarvur/goswitch): IBus/GNOME focus, and the author
  documents auto-correction limitations; not a verified Hyprland drop-in.
- [kitty shell integration](https://sw.kovidgoyal.net/kitty/shell-integration/):
  prompt/execution markers and shell cooperation exist, not a universal editable
  buffer or secret declaration.
- [Linux termios](https://man7.org/linux/man-pages/man3/termios.3.html): echo/raw
  controls are transport modes; interpreting them as field semantics is an
  additional, insufficient heuristic.
- [Bash bind/Readline](https://www.gnu.org/software/bash/manual/html_node/Bash-Builtins.html):
  buffer/key-binding APIs exist; built-in help also checked on the installed Bash.
- [Official Codex hooks](https://learn.chatgpt.com/docs/hooks): UserPromptSubmit
  exposes a prompt at submission, not a documented per-keystroke edit callback.
  [CLI editor integration](https://learn.chatgpt.com/docs/cli-customization)
  documents Ctrl+G; that workflow is not accepted as live TUI correction.
