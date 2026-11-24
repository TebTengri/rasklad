# Window provenance: namespace failure and native stable-ID evidence

The pinned Fcitx5.1.23 WlrAppMonitor key uses wl_proxy_get_id on its own foreign
handle. That key is local to its connection, not a shared cross-observer token.
A real core-Wayland counterexample confirmed2same-number/different-object matches
between2clients, validating server-owned resources. It does not itself map native
windows. See tests/wayland_identity/README.md and its committed result.

Next actual native test on owned Sway created2windows sharing app-id, with two
observer connections. Stable identifier sets matched; after closing/recreating A,
old identifier was absent and exactly1new identifier appeared. Both saw the close.
See tests/window_provenance/README.md and data/window-provenance-001-result.json.
The installed ext-foreign-toplevel-list-v1.xml specifies identifier uniqueness,
nonreuse while exchanging identifiers and a bounded opaque string; object IDs and
app-id do not substitute for it. Sway reference evidence does not prove the
current host Hyprland implementation exposes the same path.

## Remaining join is explicit, not solved by this test

A production adapter still must bind the trusted compositor window identity to
the native Fcitx IC and the exact focused accessible field. Current installed
Fcitx public IC API/inspected monitor state has not established that mapping.
Do not add a production Slack registry entry or infer it from geometry/labels.

Connection loss invalidates a binding, even if a later compositor session sends
an equal string. Stable window identity also does not identify one of several
fields within it. Field focus/lifecycle must advance the existing context epoch;
changes between validation and commit must deny or finish original through the
existing adapter controller. Candidate future key is (trusted compositor-session
generation, stable toplevel identifier, trusted field identity, IC lifetime).
These are requirements, not a new implemented bridge or authenticated permission.

Avoid hashing a32byte opaque identifier into identityBinding and calling collisions
impossible. A bounded adapter-owned mapping may assign fresh nonreused numeric
lease tokens; source trust and lifecycle invalidation must be proven before use.
The existing policy's identityBinding/epoch seam remains; no duplicate enum or
geometry-based authority was added. Real Slack remains observe-only/default-deny.

Next concrete source/runtime audit: find a trustworthy shared mapping from the
Fcitx monitor's connection-local handle to the compositor-issued stable window,
and then exact field focus. If the installed frontend lacks it, document the
required adapter/frontend contract rather than deploying guessed correlation.
No extra user permissions are implied by either closed test.

## Candidate common mapping found in upstream Hyprland protocol

Primary hyprland-toplevel-mapping-v1.xml supplies separate requests for EXT and
WLR foreign handles that report a common window address or failure. This is a
source-backed candidate for joining the monitor paths, not yet proven on the
installed host or bound to a field. Address reuse/lifetime still requires the
stable identifier + compositor-session generation; never make a raw address a
permanent grant. Registry-only closed control prepared; see
HOST-REGISTRY-PROBE-PLAN.md. No host connection authorized/executed at checkpoint.
