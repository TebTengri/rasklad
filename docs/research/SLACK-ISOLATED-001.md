# Isolated installed Slack: UI readiness not reached

SYS-246. Installed Slack4.53.287-1, empty HOME/profile, private Xvfb/session/AT-SPI
buses, no user home or host sockets mounted. This does not inspect the working
Slack instance or account. No project dictionary/model/IME involved.

Offline status false/true controls both had0visible windows and0a11y peers.
Thus the control cannot establish the cause of missing accessibility in working
Slack. Do not report the result as evidence that Slack lacks accessibility.

The owner rejected shared networking. The prepared --share-net option was removed
before any execution. A dedicated Unix socket now joins a loopback relay inside
the unchanged private network namespace to a short-lived outside broker.
Broker accepts only CONNECT to slack.com/slack-edge.com/slack-imgs.com domains
and subdomains, public IPv4 port443, DNS timeout2s and pinned resolved IP.
No TLS interception, certificate bypass, payload log or arbitrary proxy target.
Bounds:8active,64admitted requests,64MiB forwarded data,30s per broker; cleanup
closes sockets/joins workers and reports incomplete teardown as failure. TLS
ClientHello framing/visible SNI checked before DNS or target connection; SNI must
match CONNECT. ECH denied by default. HTTPS encrypted backend routing is not
verified. Protocol layout references only (no code copied): RFC6066§3 and
RFC8446§4.1.2. Python implementation/tests are own MIT code.

--check-bridge passed using rejected.invalid:0outbound connections,0forwarded
bytes, cleanupComplete. The default strict policy blocked4ECH requests per mode
and opened0connections. A diagnostic TLS1.2 command-line cap in an ignored copy
did not remove the ECH extension; no negotiated TLS version was observed and no
TLS downgrade was installed or made a default.

The owner separately allowed ECH for ONE paired invocation. CONNECT/publicIP/
visible outer-SNI guards and namespace isolation remained; encrypted inner name
and HTTP/backend routing were explicitly unverified. Disabled/enabled modes each
opened2connections, transferred18513/18486bytes, blocked2requests, and cleaned up
fully. Both still had0visible windows and0a11y peers. This permission is CONSUMED.
No further --allow-ech-outer run without fresh explicit consent. The option is
default-off and not part of the actual keyboard corrector.

Subsequent OFFLINE diagnostics output only fixed stderr categories and private
process counts: busUnavailable=true; no matching fatal/display-init category.
These warnings do not prove causality. Early argv-based role classification was
not reliable for renamed processes; no '8 main processes' conclusion is retained.
The reporter now calls only the tracked root main and labels unidentified child
roles unknown. Raw stderr, arguments, addresses and application text are not
exported. No Text/Value/Name getters, actions or input injection are performed.

24local guards pass: target/env isolation, default offline, allowlist bypasses,
private/multicast DNS, real OpenSSL ClientHello, SNI mismatch before DNS, ECH
opt-in/default deny, offline resolver spy, local denial/cleanup and diagnostic
redaction. Core/compositor tests were not repeated for this Python-only phase.
Broker and optional-ECH changes received focused independent reviews; additional
target-only review was interrupted and not counted. Root validated target scope.

Next: diagnose minimal private desktop/app bootstrap using offline evidence and
primary sources, rather than widening networking or enabling global accessibility.
Node proxy support is a hypothesis only; verify runtime/API support before adding
flags or more trials. Do not copy a real profile or read working-app journals.
Actual editor identity, native Undo and safe correction in Slack remain unproved.
