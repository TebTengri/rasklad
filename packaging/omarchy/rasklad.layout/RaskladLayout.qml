// SPDX-License-Identifier: MIT
// Rasklad bar widget: the Fcitx layout as EN or RU, and whether automatic
// correction is on, paused or off, from rasklad-layout-watch (one long-lived helper
// that asks Fcitx itself rather than through the shell's shared input-method
// indicator, so it keeps up across Fcitx restarts). Only the method name and two
// booleans are read, never text.
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

  // One line from rasklad-layout-watch: {"method": "...", "correction": "..."}.
  // Validated here as well: only known shapes change the widget.
  function applyWatch(line) {
    let state = null
    try {
      state = JSON.parse(String(line || ""))
    } catch (e) {
      state = null
    }
    if (!state || typeof state.method !== "string" || typeof state.correction !== "string") return
    root.method = /^[A-Za-z0-9_.:-]{1,64}$/.test(state.method) ? state.method : ""
    root.correction = ["on", "paused", "broken"].indexOf(state.correction) >= 0 ? state.correction : ""
  }

  // SYS-251: one long-lived helper reports the layout and the correction state as
  // they change (Fcitx announces neither on the bus), instead of a busctl process
  // every 400 ms. If it stops, it is started again after 2 s.
  Process {
    id: watch
    command: ["rasklad-layout-watch"]
    running: true
    stdout: SplitParser {
      onRead: data => root.applyWatch(data)
    }
    onExited: { root.method = ""; root.correction = ""; restart.start() }
  }
  Timer {
    id: restart
    interval: 2000
    onTriggered: if (!watch.running) watch.running = true
  }

  Process {
    id: toggle
    command: ["rasklad-layout-toggle"]
  }

  // Middle click: rasklad-pause-toggle reads the status and writes AutoCorrect to
  // the same Fcitx instance, and exits 0 only after a write.
  Process {
    id: pause
    command: ["rasklad-pause-toggle"]
    property string before: ""
    // Shown at once; the helper confirms or corrects it within half a second.
    onExited: (code) => {
      if (code === 0 && root.correction === before)
        root.correction = before === "on" ? "paused" : before === "paused" ? "on" : root.correction
    }
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
    // The bar copies the tooltip when the pointer enters; refresh it in place.
    onTooltipTextChanged: if (tooltipHovered && root.bar) root.bar.showTooltip(button, tooltipText)
    onPressed: function(pressedButton) {
      if (pressedButton === Qt.RightButton) {
        Util.execArgv(["rasklad-settings"])
      } else if (pressedButton === Qt.MiddleButton) {
        if (!pause.running) { pause.before = root.correction; pause.running = true }
      } else if (pressedButton === Qt.LeftButton && !toggle.running) {
        toggle.running = true
      }
    }
  }
}
