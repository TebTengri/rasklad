# Native Slack editor metadata — no text or edits

Owner confirmed the designated test channel's empty editor was selected.
Original128node breadth-first search truncated without reaching the editor.
An explicit512node search completed268nodes, untruncated, in about0.09seconds.
A delayed metadata snapshot gave the same field properties. All calls remain
GetChildren/GetInterfaces/GetRole/GetState on verified Slack peers only; no
Text/Name/Description/Attributes/Properties getters, focus actions or writes.

Observed a focused multiline `entry`, EDITABLE state true, password role false.
Its org.a11y.atspi.EditableText interface is ABSENT. ACTIVE ancestor was not
observed; that alone does not prove which desktop window is active or whether
Slack ever reports ACTIVE correctly. Therefore focused state is insufficient for
safe correction. Channel identity remains owner-designated, not authenticated
from the accessibility metadata. The other candidate roles are metadata only.

This proves editor discovery for this running Slack instance. It does NOT
establish supported text mutation, active-window binding, sensitive-field safety,
Fcitx transport readiness in this live process, or correction/undo behavior.
Do not call unsupported EditableText methods or guess rollback. Keep
safeAutoCorrection=false and real-app policy default-deny.

Implementation: default128node budget preserved; only explicit512 alternative,
10s total deadline, child limit256 for larger search, bounded queue and candidate
summary16. Queue omissions now mark truncation. Tree walking extracted for a
synthetic225node test proving deep focused editable-state discovery even without
EditableText, and retaining default128truncation. No live polling daemon added.
Evidence: data/slack-editor-metadata-001-result.json.

Next architecture seam: couple trusted field metadata to the actual Fcitx context
and active surface before granting a production app lease. Existing fcitxlab.cpp
intentionally accepts only owned fixture identities; do not add Slack by label
or treat an accessible app as authenticated authority. Native input delivery
remains a separate gate; no global IME changes or further Slack restart approved.

## Independent review and architecture decision

peer_observation_review found a summary completeness issue: the16candidate cap
could omit a later focused field. Fixed: mark candidateSummaryTruncated and
prefer focused candidates over nonfocused summaries. Added17candidate regression.
This does not change the raw metadata-method allowlist or grant edit permission.

Claude returned a closed correlation-plan suggestion (public source only, no
private app context). Accepted: two same-app windows and focus-away/back must be
covered; geometry/app claims do not establish authenticity. Rejected: adding a
CorrelatedSurface enum/surfaceBinding without a new trusted source (duplicates
identityBinding), or making AT-SPI EditableText a prerequisite for native Fcitx
commit. Cursor/extents/caret agreement is consistency evidence, not authority.

Installed SDK inputcontext.h exposes uuid/program/display/cursorRect; its comment
explicitly permits one IC per window OR field depending on the application. No
PID/field-to-AT-SPI binding appears in that inspected public context API. This is
a scoped API finding, not proof that no other compositor/frontend path exists.

Restored the already-pinned Fcitx5 5.1.23 source archive to ignored build/ for
inspection: exact7837170bytes/SHA256adeeafdba468111573233fb09e5cabf2e2c240cdc85e74ead498c2d702c795b8
verified before reading; no source execution or system install. In this pinned
version appmonitor.h is under src/frontend/virtualinputcontext, not the current
master location. WlrAppMonitor builds a window-key→app-id map and focused key;
that interface does not provide the AT-SPI PID/field binding. No upstream code
was copied into own source. Existing LGPL notices unchanged.

Next closed experiment specification: two owned toplevels sharing app-id, with
multiple fields. Record only opaque compositor/window/IC identifiers and epoch
transitions. First establish which trusted source can map those identifiers; if
no shared authority exists, report that absence rather than issuing a production
VerifiedAdapter lease from coincident geometry. Only then add the binding adapter
and its stale/reentry/Off tests. Existing DOM focus tests are intra-surface and
do not prove this cross-window binding. Live Slack remains observe-only.
