# Architecture decision 002: Fcitx first, with committed native letters

**Product roadmap update:** [Direction 003](PRODUCT-DIRECTION-003.md) supersedes Fcitx as the sole primary path after explicit terminal/message requirements. SYS-241 is deferred; its evidence is retained.

2026-11-13. Direction selected; no runtime admission or installation.

## Independent proposals and choice

At the owner's explicit request Claude Opus 5.5 reviewed public code/reports,
without tools, customizations, MCP, private projects or user input. The first
response used high effort; a short follow-up used medium effort after new facts.
Codex wrote its initial proposal before receiving Claude's response. Full raw
responses and that proposal remain in the local checkpoint, not as verified facts.

| Proposal | Benefit | Material limitation |
| --- | --- | --- |
| Codex: in-process correction in one supported real editor | Committed native text; synchronous correction works in the trusted client demo, including Save/Close | Requires application integration and shares editor failure domain; does not deliver the requested system utility |
| Claude: Fcitx addon that passes letters through and corrects at Space | Fits a system utility with one framework integration; no held word in preedit | Requires trustworthy context and correct real frontend ordering; external edits still lack atomic compare-and-swap |

**Selected primary direction: Claude's Fcitx approach**, with stricter admission
gates below. In-process integration stays a fallback candidate, not the main
roadmap. Whole-word preedit stays rejected for the demonstrated Save/Close
failure. No blanket conclusion that every existing IME-aware app is broken.

The choice is a product direction, not a claim that either advisor proved a
universal safe backend. Supported clients only; unknown or protected contexts get
no correction. Automatic correction remains the required primary feature. A
hotkey is not silently substituted for it.

## Corrections to Claude's first recommendation

- Fcitx is installed; its role in the working session has not been established.
- INPUT-LAB-004 also tested native keyboard paths. It did not test this specific
  Fcitx D-Bus deletion/commit path, so neither a blanket rejection nor a guarantee
  of keyboard ordering follows.
- A check after a synchronous model call exceeds 5 ms cannot undo that stall.
  A real time budget requires a separate design and measurement.
- A hotkey does not remove races and does not satisfy the automatic feature by
  itself. Backspace undo is also an edit that needs validation.
- Claude's proposed live installation and three-app working-session trial were
  not accepted. No host seat, global configuration, new permissions or profile
  access is authorized by this review.
- API-advisor output is a hypothesis source, not an independent security audit.

## Concrete work already performed

Own prototype `tests/commit_first_lab` uses installed Fcitx public APIs and the
existing router over deterministic synthetic evidence. It passes letters through,
keeps at most 32 ASCII token characters, checks a fixed fixture program/frontend,
protected/disabled flags and SurroundingText capability, and verifies a matching
cached token at an unselected end cursor before considering delete+commit. It has
no preedit, devices, clipboard, network, history, model or dictionary reads. The
factory refuses outside the private fixture marker/runtime. Program identity is
a narrowing veto, not proof of trusted field identity or authorization.

The existing private fixture and standard Qt/Fcitx D-Bus plugin were reused.
Save/Close and Save/Close after private daemon kill passed: native letters remain
in the document. Normal and burst correction **did not pass**. The gate found a
tracked six-character token but SurroundingText=false at Space, made zero cache
queries and zero edits. The unchanged original word and burst text were retained.
This is successful refusal, **not successful autocorrection**. Ordering of an
actual external deletion/commit remains untested.

A bounded read-only diagnostic then observed only masks and validity of completed
queries in the approved normal fixture field (at most 32 entries), plus Fcitx
capability transitions. No surrounding strings were logged and no refresh was
forced. Both normal and burst diagnostics show valid text/cursor responses when
requested, and a transition of SurroundingText from true to false before Space.
Thus the fixture does answer the standard queries; the initial hypothesis that it
simply lacks surrounding-text support was too broad.

The installed fcitx5-qt package is 5.1.16-2. The pinned upstream 5.1.16 shared
Qt5/Qt6 implementation removes the capability when an update does not produce
surrounding state, including partial queries. This matches the observed sequence;
exact attribution to the downstream binary still needs a minimal regression.
Source references: [update implementation](https://raw.githubusercontent.com/fcitx/fcitx5-qt/5.1.16/qt5/platforminputcontext/qfcitxplatforminputcontext.cpp),
[Qt6 shared source link](https://raw.githubusercontent.com/fcitx/fcitx5-qt/5.1.16/qt6/platforminputcontext/qfcitxplatforminputcontext.cpp).
No upstream code was copied, no installed library was patched.

The diagnostic budget is closed for this step: two instrumented runs, no further
matrix expansion or forced capability. [Aggregate evidence](ARCHITECTURE-DECISION-002-results.json).

## Exactly one next deliverable

**[SYS-241](https://linear.app/systemic-kz/issue/SYS-241/ispravit-poteryu-konteksta-qtfcitx-bez-oslableniya-zashity): a minimal regression and candidate fix for the Qt/Fcitx context-update boundary,
built only in the workspace.** Claude prepares the change; Codex reviews it and
checks privacy/lifecycle regressions. This is the next task, not completed work.

Acceptance:

1. Prove, with a small pinned-source regression, whether an unrelated cursor-rect
   update removes an otherwise valid same-field context capability.
2. A candidate fix must preserve invalidation on protected/unknown hints, actual
   text/cursor invalidation and focus/field change. Merely keeping a stale flag or
   enabling it globally is prohibited.
3. Build any dependency variant under ignored build/, with license/notice review;
   do not install or replace the system plugin. Reuse the existing correction and
   immediate-tail case. PASS requires an actual correction and intact next input,
   not a reordered client ACK or a forced refresh.
4. If the regression cannot establish a valid safe fix, stop this task with that
   concrete finding. Do not silently switch to another architecture or add a new
   broad laboratory. No more than one candidate change and its focused checks.

Only after this gate is passed do we attempt one real supported application
(Chromium is installed), with a disposable isolated profile and a verified IM
route. The current private compositor lacks external input-method-v2 for its
Wayland path; a viable isolated route is a prerequisite, not assumed available.
No real-client compatibility or useful daily automatic mode is promised today.
No Neovim plugin is presented as a universal replacement.

## Trace and limits

Review artifacts: local build/architecture-review. Prototype and reports are on
the draft branch; not merged or installed. Existing manual runtime is unchanged.
The in-process suite's earlier 10 passes and composition performance figures do
not validate this new external candidate. No real profile/model reads occurred.

Operational note: the installed mise wrapper automatically downloaded Claude CLI
2.1.323 while invoking the requested review. This was tooling cache activity,
not a rasklad runtime installation; no system configuration or services were
changed. Future review invocation should prefer the already installed version.
