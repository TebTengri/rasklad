# Input must remain usable (SYS-223)

Owner requirement: rasklad should speed up typing and must not impede the user
or the operating system. This is an acceptance condition, not a verified claim.

- Original text must survive navigation, selection, focus change and refusal.
  An unfinished word is finalized literally before passing through an unhandled
  navigation/shortcut key in the current experimental normal-field adapter.
- Standard shortcuts and native key events must reach the editor. No artificial
  delay, hotkey wait window, replay of Backspace, clipboard substitution or
  system-wide keyboard grab is permitted as a workaround.
- Unsupported context, protected input or unavailable capability means refusal
  of correction. Refusal alone does not prove safety of pending preedit; its
  lifecycle needs separate tests.
- Expensive work must not block the input event loop. Keep queues and buffers
  bounded, cancel obsolete work, and do not apply late results to another field.
  Current composer owns at most 32 letters; this is not a bound on all framework
  allocations or on a future detector provider's execution time.
- Measure input latency with and without the adapter, including p50/p95/p99,
  worst observed delay, idle CPU, typing CPU, memory and process failure behavior.
  Reject a candidate with visible lag or unexplained regression; agree numerical
  budgets against the measured baseline before enabling desktop input.
- Disabled, crashed or disconnected correction must leave ordinary typing
  functional. Pending composition at process failure is a separate acceptance
  gate and is not yet passed.
- No polling, telemetry, dictionary downloads or network requests are needed
  for the product's input path. The private lab's timers are synthetic drivers,
  not proposed runtime polling.

The closed fixture and synthetic evidence do not establish real detector latency,
normal desktop resource usage or compatibility with all applications. None of
these measurements is replaced by the unit suite's total execution time.
