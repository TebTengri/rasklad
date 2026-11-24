# First external target: native Slack

The owner selected Slack and confirmed the installed desktop application, not a
browser tab. Metadata inspection confirmed a native slack-desktop package and
launcher. No account/profile files, workspace, draft or message contents were read.
Executable/protocol matching used only known Slack executable identities, never
another project's source or environment variables. No application was restarted.

## What public evidence establishes

Slack's official https://slack.com/libs/webapp report lists ProseMirror packages
and quill-delta, but explicitly dates itself 2021-09-22. It does not establish the
current composer engine/version, expose its EditorView or prove our new adapter
can be inserted into the installed application. The tested current upstream
ProseMirror adapter is independent groundwork, not evidence of Slack compatibility.

Slack's https://docs.slack.dev/messaging/modifying-messages/ describes updating
published messages; https://docs.slack.dev/reference/types/rich_text-type/
describes app/workflow input data. These are not a verified live-draft hook. No
bot/OAuth scopes, message APIs, workspace access or alternate composer were used.

## Read-only native probe

`tools/slack_accessibility_probe.py` defaults to printing its plan. Explicit --run
queries an existing D-Bus accessibility transport with NO_AUTO_START, scopes peers
to PIDs whose executable is the installed Slack, and reads only GetChildren,
GetInterfaces, GetRole and GetState. It never calls Text/Value, Name/Description,
GetAttributes, GetAll, actions, focus, edit methods or signal subscriptions.
Object references and PID mappings stay internal. Output contains only counts,
local role enums and booleans, never identifiers, text, window/channel names or
credentials. Maximum 128 nodes /10 seconds; cancellation also bounds connection.
Other application peers are used only for broker PID comparison, not UI queries.
No accessibility setting or Slack launch option is changed by the probe.

The observed snapshot found an existing a11y bus, successful broker PID queries
and no matching Slack accessibility connection. No Slack UI node was queried.
This means that path is unavailable in the observed launch, not that Slack can
never support it. Focused editable interface presence would still NOT authorize
correction: code/link/mention/secret roles, native Undo, transaction freshness and
exact limited replace must be established first. safeAutoCorrection stays false.
Aggregate: data/slack-native-001-result.json. Eleven probe/launch/quit guard tests
ensure text/write methods are rejected and only the separate, explicitly approved quit helper can terminate Slack.

Primary interface reference:
https://raw.githubusercontent.com/GNOME/at-spi2-core/main/xml/Accessible.xml

## Reviewed temporary trial

`python3 bin/slack-accessibility-trial` prints this fixed plan:

```text
/usr/lib/slack/slack --force-renderer-accessibility
```

Only separately approved --run launches it; it refuses while Slack is running.
The owner must quit Slack through its normal UI when ready. The helper never
kills/restarts a working process, edits launchers/configuration, patches app.asar,
adds a debugging port or starts our corrector. Stdout/stderr are not collected.
It uses the normal Slack profile as an ordinary launch; the project does not
extract that profile, but Slack itself naturally reads/updates its own files.

Electron documents exposing its accessibility tree at
https://www.electronjs.org/docs/latest/tutorial/accessibility and Chromium documents
the force-renderer-accessibility launch flag at
https://www.chromium.org/developers/design-documents/accessibility/ . Compatibility
of the exact installed Slack with that flag remains an experimental question.

Material effect: this temporarily makes Slack's UI available to local accessibility
clients, which may include message/draft text. Our probe will still query only its
four metadata methods and will not correct/send/type anything. This change of a
working application's visibility and the need to quit it require owner approval.
The flag is not written to configuration; to end the trial, quit Slack and launch
normally without it. No guarantee of undo/format support or actual correction is
implied merely by accessibility becoming available. The approved flag launch occurred on 2026-11-14; results follow.

### Trial status — local date 2026-11-14

After two guarded refusals, the owner confirmed the same Linux/Omarchy machine
and explicitly approved SIGTERM. The identity-checked pidfd helper sent one
SIGTERM and verified complete exit. The temporary launch then succeeded; process
metadata confirmed the force flag present and disable flag absent. No SIGKILL.

The post-launch probe still found zero matching Slack accessibility connections
and queried zero UI nodes. All eight bus-peer PID lookups succeeded; comparing
peer executables also found no Slack peer. A read-only X11 root AT_SPI_BUS query
succeeded but found no alternative accessibility bus property. This trial does
not establish a usable native Slack adapter. No correction or message operation
was attempted. The trial Slack remains running; normal quit and ordinary launch
are still required to end its temporary accessibility flag.

### Separately approved termination request if normal quit leaves background Slack

`python3 bin/slack-request-quit` defaults to a plan. A separate explicit --run
requires approval beyond the launch approval: it can lose unsaved application state.
It identifies exactly one current-user installed-Slack process-tree root, pins it
with a Linux pidfd and rechecks executable/UID/parent/birth-time identity before
sending one SIGTERM. No global signal, killall, SIGKILL, child sweep, forced retry,
profile read or automatic restart. If identity is stale, root selection ambiguous
or the app does not exit within ten seconds, it stops; the launch helper still
refuses remaining processes. Four fake-kernel tests cover root selection, pidfd
identity races, exact signal/cleanup and missing targets; no signal was sent by tests.
The owner confirmed this is the same Linux/Omarchy machine; SIGTERM was explicitly authorized and executed once, followed by the approved
temporary launch. All original text/write restrictions remain.


## Metadata follow-up after Hyprland trial011

The existing metadata-only probe was repeated without changing Slack's launch or
configuration. It found the accessibility bus and8 target Slack processes, but
zero matching a11y connections and zero inspected UI nodes. No text, value,
channel/window name, action, subscription or write was requested. Source guards11
pass. Aggregate: `data/slack-native-002-result.json`.

The keep-grab candidate now passes its own Chromium-field exact nested Hyprland
preflight. That does not establish Slack support. The metadata snapshot reports
no current accessibility route; it neither proves nor disproves Slack's native
input-method behavior. No current Slack restart, native launch-flag change,
profile read, draft read/write or message send was performed. A next actual-app
trial needs a specifically bounded launch/unsent-test-draft scope rather than
reusing the old temporary-launch or trial011 approval.

## Read-only status diagnosis — 2026-11-16

One newly scoped metadata run additionally read only org.a11y.Status.IsEnabled
and ScreenReaderEnabled through fixed system Properties.Get calls, NO_AUTO_START
and the existing deadline. Slack's four node methods remain unchanged; Attributes,
Text/Value/Name/GetAll/actions and arbitrary application Get remain forbidden.
Thirteen guards pass; one independent privacy diff review approved this scope.

Observed existing bus, IsEnabled=false, ScreenReaderEnabled unknown (one sanitized
property error), 8 Slack processes and zero matching a11y connections/nodes.
No restart/configuration/profile/text/write/signal operation. Aggregate:
data/slack-native-003-result.json. This is a snapshot, not proof of the cause.
Static ELF metadata confirms the installed Slack links ATK/AT-SPI libraries; that
does not prove initialization. Current launch context has no NO_AT_BRIDGE setting;
Slack's own process environment was not inspected.

Public upstream Chromium currently enables extended accessibility on certain
ATK attribute/relation requests:
https://raw.githubusercontent.com/chromium/chromium/main/ui/accessibility/platform/ax_platform_node_auralinux.cc
Electron documents automatic accessibility activation and its in-app API:
https://www.electronjs.org/docs/latest/tutorial/accessibility
ATK bridge checks NO_AT_BRIDGE:
https://raw.githubusercontent.com/GNOME/at-spi2-core/main/atk-adaptor/bridge.c
These floating upstream sources are research, not matched Slack-build proof.
No source code copied and no new dependency introduced.

Do not add an automatic tree-wake request to the Slack probe: no matching peer
exists, and current text/attribute restrictions still apply. Next controlled
experiment should compare bridge/tree readiness on a PRIVATE accessibility bus
with owned Chromium and fixed fixture, changing private status only. Host global
accessibility status must not be enabled as a shortcut: it can expose other apps.
A positive closed control would justify a separately scoped Slack trial later.

## Private bridge control result

A new owned Chromium/X11 experiment changed IsEnabled only on its PRIVATE bus:
false→0 Chromium peers; true→1 peer and web documents observed, with the same
force-renderer flag. See A11Y-BRIDGE-CONTROL-001.md. This supports a transport
readiness hypothesis without proving the installed Slack cause. No real app or
global accessibility flag changed. Next real-app trial must be per-process,
private-bus scoped and separately consented; no host-wide workaround.
