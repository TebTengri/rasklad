# INPUT-LAB-005: latency, daemon resources and disconnection

**Later finding:** [INPUT-LAB-006](INPUT-LAB-006.md) shows Save/Close failures for this composition path. Earlier passes and performance numbers do not admit it as a universal backend. The new in-process candidate is a separate, unmeasured path.

## Scope and method

Closed synthetic fixture, installed Qt 6.12 and Fcitx5 5.1.23; no desktop input,
network, host bus, clipboard, model or dictionary access. Baseline is the same
Qt Fcitx plugin and private daemon using inactive keyboard-us, without loading
our addon. The other condition activates the own experimental addon. This is
not a measurement of the OS's native input path without an IM framework.

Five pairs, alternating their order, 60 keys per run at 15 ms nominal intervals
(300 observations per condition). The fixed synthetic workload has eight
correctable words and a short unchanged final word. The real SafeConfirmed
router runs over deterministic synthetic evidence; no actual statistical model
or dictionary is queried. Each client's final text is independently asserted:
original for baseline, eight corrections for addon. No observations are silently
dropped: failed assertions, missing/reordered/negative timestamp pairs cause the
reporter to reject the measurement.

A monotonic timestamp is recorded immediately before the parent sends each key.
A developer-only QObject observes text/preedit signals and records the matching
length after queued signal updates settle. Neither editor is modified; the
observer neither changes text nor ACKs keys to the producer. It runs only for
timing scenarios and has fixed 60-element storage. This measures **editor-state
change**, not physical device latency, rendered frames or display presentation.
Both conditions include observer overhead and uncontrolled host scheduling.

The launcher reads /proc for its own private daemon only. CPU counters have
10 ms resolution. Phase-marker sampling adds about 20 ms polling granularity
outside the input path. Idle windows are about one second, typing windows about
1.15 seconds. Counter logging is disabled in performance runs; no text is logged.

## Observed results

Ranges across five runs (per-run nearest-rank quantiles):

| Metric | Private Fcitx baseline | Own addon, synthetic evidence |
| --- | --- | --- |
| Editor-state p50 | 1.70–1.76 ms | 1.44–1.50 ms |
| Editor-state p95 | 1.96–2.14 ms | 1.76–1.94 ms |
| Editor-state p99 / maximum | 2.18–2.61 ms | 4.49–5.11 ms |
| Daemon idle CPU time | 0 ms counted | 0–10 ms counted |
| Daemon typing CPU time | 20 ms counted | 10–30 ms counted |
| Final daemon RSS | 15.6–15.7 MiB | 21.6–21.8 MiB |

At n=60, p99 is the maximum, not a robust tail estimate. No claim of a faster
median or statistical superiority follows from these small samples. Zero counted
idle CPU is not proof of zero activity. RSS includes shared mappings and does not
measure PSS/USS or the whole application; its difference is not a proven marginal
physical-memory cost. No long-duration leak or real-detector measurement occurred.

In all five addon runs the slowest event was event 7, the **first correction at
Space**. Later ordinary input was faster. This identifies a useful investigation
point, not its cause: the router, framework, client text layout/font first use
and scheduling have not been separately timed. Do not attribute the delay to
model inference (no real model was loaded) or optimize the converter based solely
on this observation.

[Per-run aggregate results](INPUT-LAB-005-performance-results.json) omit text and
raw timestamps. `tools/input_latency_report.py` reproduces aggregates from the
local attempt metadata; four unit tests check quantiles/units and rejection of
failed, incomplete and invalid observations.

## Stop and forced-termination results

All four closed cases passed:

| Private daemon action | Subsequent editor action | Observed result |
| --- | --- | --- |
| Graceful remote exit | Continue typing in original field | Original word and new suffix retained |
| SIGKILL | Continue typing in original field | Original word and new suffix retained |
| Graceful remote exit | Focus another ordinary field and type there | Original word retained in old field, new text only in new field |
| SIGKILL | Focus another ordinary field and type there | Original word retained in old field, new text only in new field |

Before that subsequent editor action, the old word was still pending preedit,
not committed text. Thus these passes do **not** establish unconditional
persistence after disconnection. They establish preservation for the tested
continuation/focus actions. All cases also assert unchanged fictional password
input. A private stop-request marker synchronizes failure injection only; it is
not an editor ACK or part of the correction mechanism.

The private daemon exits about 2.3 seconds after surface creation. Continuation
starts at 3.0 seconds, or focus changes at 2.85 seconds followed by typing. This
does not cover an immediate next key during the disconnection race, editor
close/save without focus change, a hung daemon, corrupted memory or OS shutdown.
Forced termination targets only the PID created inside this private fixture.
[Aggregate stop assertions](INPUT-LAB-005-stop-results.json).

## Remaining admission gates

These results support continuing the prototype, not enabling desktop input.
Real detector accuracy/latency and total resource usage, immediate failure races,
pending preedit on save/close, Ctrl/Alt/Super, mouse selection, arbitrary cursor
fragments, lock/capability loss and browser/editor/terminal compatibility remain
unverified. Independent review is pending. Numerical acceptance budgets should
be agreed against representative measurements before a desktop trial.

No runtime application source or system configuration changed. The GPL-dependent
private compositor/launcher and raw attempts remain local in the checkpoint;
integration is not reproducible from checkout alone. Public unit and report tools
are reproducible without connecting to any input framework.
