// SPDX-License-Identifier: MIT
// Rasklad bar widget: the Fcitx layout as EN or RU, and whether automatic
// correction is on, paused or off. It asks Fcitx itself (Controller1 over busctl)
// rather than through the shell's shared input-method indicator, so it keeps up
// across Fcitx restarts. Only the method name and two booleans are read, never text.
// Left click switches us <-> ru (the same rasklad-layout-toggle that double Alt
// runs); middle click pauses or resumes automatic correction; right click opens
// the Rasklad settings window.
import QtQuick
import Quickshell
import Quickshell.Io
import qs.Ui
import qs.Commons
import qs.Commons as Commons

BarWidget {
  id: root
  moduleName: "rasklad.layout"

  Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }

  // "keyboard-us", "keyboard-ru", another method, or "" when Fcitx is not answering.
  property string method: ""
  // From fcitx://config/addon/rasklad/status: "" (unknown), "on", "paused", "broken".
  property string correction: ""

  // Fixed strings only: nothing from Fcitx reaches the label or tooltip as text.
  readonly property string code: method === "keyboard-us" ? "EN"
                               : method === "keyboard-ru" ? "RU"
                               : method === "" ? "" : "··"
  readonly property string label: code === "" ? "" : correction === "broken" ? code + "!" : code

  function parseMethod(output) {
    const match = /^s "([A-Za-z0-9_.:-]{1,64})"/.exec(String(output || "").trim())
    root.method = match ? match[1] : ""
  }

  // busctl --json=short reply to GetConfig: {"data":[{"data":{"Key":{"data":"True"}}}, ...]}.
  function parseStatus(output) {
    let values = null
    try {
      const reply = JSON.parse(String(output || ""))
      values = reply && reply.data && reply.data[0] && reply.data[0].data
    } catch (e) {
      values = null
    }
    const flag = key => values && values[key] && values[key].data === "True"
    if (!values || !values.ProfileReady || !values.AutoCorrect) root.correction = ""
    else if (!flag("ProfileReady")) root.correction = "broken"
    else root.correction = flag("AutoCorrect") ? "on" : "paused"
  }

  function call(args) {
    return ["busctl", "--user", "--timeout=1"].concat(args)
  }
  readonly property var controller: ["org.fcitx.Fcitx5", "/controller", "org.fcitx.Fcitx.Controller1"]

  Process {
    id: methodQuery
    command: root.call(["call"].concat(root.controller, ["CurrentInputMethod"]))
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: root.parseMethod(text)
    }
    onExited: (code) => { if (code !== 0) root.method = "" }
  }

  Process {
    id: statusQuery
    command: root.call(["--json=short", "call"].concat(root.controller,
                       ["GetConfig", "s", "fcitx://config/addon/rasklad/status"]))
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: root.parseStatus(text)
    }
    onExited: (code) => { if (code !== 0) root.correction = "" }
  }

  // The layout every 400 ms; the correction status every 2 s and after a change.
  property int ticks: 0
  Timer {
    interval: 400
    running: true
    repeat: true
    triggeredOnStart: true
    onTriggered: {
      if (!methodQuery.running) methodQuery.running = true
      if (root.ticks++ % 5 === 0 && !statusQuery.running) statusQuery.running = true
    }
  }

  Process {
    id: toggle
    command: ["rasklad-layout-toggle"]
    onExited: if (!methodQuery.running) methodQuery.running = true
  }

  // Writes only AutoCorrect (Fcitx merges a partial config); fixed values.
  Process {
    id: pause
    property string next: "False"
    command: root.call(["call"].concat(root.controller,
                       ["SetConfig", "sv", "fcitx://config/addon/rasklad", "a{sv}", "1", "AutoCorrect", "s", next]))
    onExited: if (!statusQuery.running) statusQuery.running = true
  }

  visible: label !== ""
  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  WidgetButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: root.label
    dimmed: root.correction === "paused"
    fontSize: Style.font.caption
    horizontalMargin: 6
    tooltipText: strings.t(root.correction === "on" ? "tooltip.on"
                         : root.correction === "paused" ? "tooltip.paused"
                         : root.correction === "broken" ? "tooltip.broken"
                         : "tooltip.unknown").arg(root.code || "—")
    onPressed: function(pressedButton) {
      if (pressedButton === Qt.RightButton) {
        Util.execArgv(["rasklad-settings"])
      } else if (pressedButton === Qt.MiddleButton) {
        if ((root.correction === "on" || root.correction === "paused") && !pause.running) {
          pause.next = root.correction === "on" ? "False" : "True"
          pause.running = true
        }
      } else if (pressedButton === Qt.LeftButton && !toggle.running) {
        toggle.running = true
      }
    }
  }
}
