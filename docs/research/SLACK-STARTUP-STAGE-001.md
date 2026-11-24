# Offline startup stage — bounded observation

One 15-second observation in each existing status mode did not produce a Slack
accessibility peer. A second observation enabled WAYLAND_DEBUG=client only in
owned application children; merged stdout/stderr was streamed through fixed
boolean markers, with only 128 bytes retained across chunks. No raw protocol or
application output is stored. No host profile, account or typing history exists
in the fixture.

Slack in both modes: get_registry and create_surface observed; get_toplevel not
observed during 15 seconds. Chromium control: all three markers observed in both
modes; enabled mode registered an accessibility peer after about 0.72 seconds.
This shows the fixture can record top-level creation. It narrows the Slack issue
to before observed xdg top-level creation; it does not identify the cause, prove
that every possible shell/window protocol was absent, or prove incompatibility.

No new missing-resource/permission/singleton/fatal markers were observed. Absence
of these fixed strings is not absence of errors. Network remains unavailable,
so remote/login bootstrap remains a possible confounder. Increasing observation
further without new evidence is not the next step.

Evidence: data/slack-startup-stage-001-result.json. 26 local guards pass, including
rejection of startup-probe with a network bridge. Existing network permissions
are unchanged; ECH exception remains consumed. Input history is neither needed
nor accessed. Correction requires a current editable field, not historical text.

Independent static review (wayland_control_review) found no blockers in offline
guards, streaming privacy, observation timing or interpretation. No reviewer
runtime action occurred. Existing bridge counters cannot establish that all
bootstrap endpoints were reachable: rejected domains were not recorded.
