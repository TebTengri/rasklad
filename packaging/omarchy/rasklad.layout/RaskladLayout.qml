// SPDX-License-Identifier: MIT
// Rasklad bar widget: the focused window's Fcitx layout as EN or RU.
// It asks Fcitx itself (Controller1.CurrentInputMethod, about every 400 ms)
// rather than through the shell's shared input-method indicator, so it keeps
// up across Fcitx restarts. Only the method name is read, never text.
// Left click switches us <-> ru (the same rasklad-layout-toggle that double Alt
// runs); right click opens the Rasklad settings window.
import QtQuick
import Quickshell
import Quickshell.Io
import qs.Ui
import qs.Commons

BarWidget {
  id: root
  moduleName: "rasklad.layout"

  // "keyboard-us", "keyboard-ru", another method, or "" when Fcitx is not answering.
  property string method: ""

  // Fixed strings only: nothing from Fcitx reaches the label as text.
  readonly property string label: method === "keyboard-us" ? "EN"
                                : method === "keyboard-ru" ? "RU"
                                : method === "" ? "" : "··"

  function parse(output) {
    const match = /^s "([A-Za-z0-9_.:-]{1,64})"/.exec(String(output || "").trim())
    root.method = match ? match[1] : ""
  }

  Process {
    id: query
    command: ["busctl", "--user", "--timeout=1", "call", "org.fcitx.Fcitx5", "/controller",
              "org.fcitx.Fcitx.Controller1", "CurrentInputMethod"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: root.parse(text)
    }
    onExited: (code) => { if (code !== 0) root.method = "" }
  }

  Timer {
    interval: 400
    running: true
    repeat: true
    triggeredOnStart: true
    onTriggered: if (!query.running) query.running = true
  }

  Process {
    id: toggle
    command: ["rasklad-layout-toggle"]
    onExited: if (!query.running) query.running = true
  }

  visible: label !== ""
  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  WidgetButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: root.label
    fontSize: Style.font.caption
    horizontalMargin: 6
    tooltipText: "Rasklad · " + (root.label || "—") + " · Alt Alt"
    onPressed: function(pressedButton) {
      if (pressedButton === Qt.RightButton) Util.execArgv(["rasklad-settings"])
      else if (pressedButton === Qt.LeftButton && !toggle.running) toggle.running = true
    }
  }
}
