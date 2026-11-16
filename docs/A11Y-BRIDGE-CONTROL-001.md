# Private AT-SPI bridge control

SYS-246, 2026-11-16. Python helper and tests authored for rasklad (MIT), no
third-party code copied or dependency installed. Chromium155.0.8059.84-1 and
at-spi2-core2.62.1-1 are existing system packages; private Xvfb uses prior pin.

One A/B control, two fresh namespaces/private session buses and empty profiles.
Only IsEnabled differs; --force-renderer-accessibility and NO_AT_BRIDGE=0 are
constant. Private launcher owns org.a11y.Bus and its bus PID is checked before
the sole property write. Host /run, /tmp, HOME, display and network are absent.
No Slack launch, account, app action, text/name/value getter or host setting.

| Private status | Chromium peers | inspected nodes | web document nodes |
|---|---:|---:|---:|
| false | 0 | 0 | 0 |
| true | 1 | 64 | 2 |

The enabled trial reached the64-node bound; no full traversal is claimed.
passed means the measurement completed, not general app compatibility. Separate
comparisonOutcome=enabled-only-peers and supportsStatusHypothesis=true state the
observed result. This establishes the flag's effect in this controlled X11
Chromium setup; it does not establish the cause for installed Slack/Wayland.

15 guards pass, including outside-sandbox --inside rejection, namespace/env
constraints and absence of inherited host-bus configuration. One independent
safety review approved a closed run; no broad package/source audit implied.
One measurement only, not a performance/idle/long-run benchmark. Aggregate:
data/a11y-bridge-control-001-result.json.

Default command prints a plan: python3 tools/a11y_bridge_lab.py. Explicit --run
executes only the two private controls. Existing project run limits bound CPU,
file output and cores;30-second parent deadline per trial bounds stalled calls,
and process groups/namespaces are cleaned up. No project Fcitx addon or dictionaries are mounted; no Fcitx process is launched.

Next: prepare a reviewable per-process Slack trial with a private bus, keeping
its launcher's HOME/XDG state private as well. Do not turn on host global
accessibility: it can expose unrelated apps. Real Slack restart/normal profile
launch still needs new explicit consent; old temporary-launch approval is spent.
