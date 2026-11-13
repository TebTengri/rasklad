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
Aggregate: data/slack-native-001-result.json. Seven probe/launch-plan guard tests
ensure text/write methods are rejected and a running Slack is never terminated.

Primary interface reference:
https://raw.githubusercontent.com/GNOME/at-spi2-core/main/xml/Accessible.xml

## Concrete next trial — not executed

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
implied merely by accessibility becoming available. No flag launch has occurred.

### Trial status — local date 2026-11-14

The owner approved the temporary launch. Two invocations stopped before spawn
because matching same-user Slack processes still existed. No process was killed
or restarted and no UI/profile content was read. Target-machine/instance
confirmation is pending; approval remains valid for the reviewed launch after
that is resolved. The launcher intentionally does not turn a closed-window
confirmation into permission to terminate a running app.
