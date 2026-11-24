# Targeted parallel source review

2026-11-17. User requested Grok via Cursor and Gemini via Antigravity in parallel,
with Claude implementing and Codex accepting. Only public source snapshots were
supplied; no user input, profiles, channels or credentials. No system/GPU trial.
This is a limited source review, not a whole-project security audit or runtime proof.

Completed: Grok4.5 high-fast through Cursor (applier + closed wrapper), Gemini3.8
Flash Medium through Antigravity (offline decision router). Earlier larger
requests timed out and are not credited. CLI Antigravity authenticated normally;
GUI sign-in alone was not treated as a completed CLI/model review.

| Finding | Disposition and evidence |
| --- | --- |
| Grok: intermediate symlink/hardlink can escape applier scope | Accepted. Also found independently at Codex acceptance. Claude implemented nofollow dir-FD walk, single-link target refusal and atomic replacement.8local regression tests passed. Grok reviewed the earlier source snapshot. |
| Grok: interrupted write may truncate original | Accepted. Same Claude fix writes unique temp, fsyncs and renames; original inode never truncated. |
| Grok: malformed callback array could dereference null | Claude added a tiny null/data guard defensively. A wire-triggered null-array vulnerability was not established: libwayland allocates the array slot. |
| Grok: raw-this callback may outlive refusal/teardown | Rejected after Claude source check: backend-owned feedback proxy/closures destroyed before display disconnect; proxy destruction prevents later dispatch to freed closures. No extra weak_ptr machinery added. |
| Gemini: remove status recheck after exhaustive canonical switch | Declined for this patch. The check is a small fail-closed guard if the enum grows without a corresponding switch return. No measured cost justifies removing it. |
| Gemini: merge dictionary-only and model-only identical action assignment | Valid small readability candidate; deferred from the transport fix. No unused function or significant runtime overhead demonstrated. Preserve distinct reasons and validate existing router parity if later changed. |

Current own code remains offline/default-deny. Reviewer agreement is not a test.
The patched Aquamarine build, symbol/SONAME compatibility and immediate/deferred/
never control matrix remain separate pending gates; no patched runtime success
is claimed by this review. Aggregate: data/parallel-review-001-result.json.

Claude implementation changes and critique dispositions: docs/research/AQUAMARINE-READINESS-EXPERIMENT.md §3.1. Codex reran the changed applier regression suite after the defensive guard:8/8passed.
