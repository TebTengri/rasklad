// SPDX-License-Identifier: MIT
// Rasklad bar widget: the Fcitx layout as EN or RU, and whether automatic
// correction is on, paused or broken and whether Rasklad is off, from rasklad-layout-watch (one long-lived helper
// that asks Fcitx itself rather than through the shell's shared input-method
// indicator, so it keeps up across Fcitx restarts). Only the method name and three
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
  // SYS-272: unknown correction state is not shown as the ordinary label: the addon
  // may be missing or not loaded, and then nothing is corrected.
  // SYS-272: a click that did not go through ("toggle" or "pause"), for 3 s.
  property string failed: ""
  readonly property string label: code === "" ? "" : failed !== "" ? code + "×"
                                : correction === "broken" ? code + "!"
                                : correction === "" ? code + "?" : code

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
    root.correction = ["on", "paused", "broken", "off"].indexOf(state.correction) >= 0 ? state.correction : ""
  }

  // SYS-251: one long-lived helper reports the layout and the correction state as
  // they change (Fcitx announces neither on the bus), instead of a busctl process
  // every 400 ms. If it stops or cannot start (no signal comes then), the timer
  // below starts it again within 2 s.
  Process {
    id: watch
    command: ["/usr/bin/rasklad-layout-watch"]
    running: true
    stdout: SplitParser {
      onRead: data => root.applyWatch(data)
    }
    onExited: { root.method = ""; root.correction = "" }
  }
  Timer {
    interval: 2000
    running: true
    repeat: true
    onTriggered: if (!watch.running) watch.running = true
  }

  Process {
    id: toggle
    command: ["/usr/bin/rasklad-layout-toggle"]
    onExited: function(exitCode) { if (exitCode !== 0) root.fail("toggle"); else root.clearFail("toggle") }
  }
  function fail(what) { root.failed = what; failedTimer.restart() }
  // A later success of the same action takes the mark off at once.
  function clearFail(what) { if (root.failed === what) { root.failed = ""; failedTimer.stop() } }
  Timer { id: failedTimer; interval: 3000; onTriggered: root.failed = "" }

  // Middle click: rasklad-pause-toggle reads the status and writes AutoCorrect to
  // the same Fcitx instance. The widget shows only what the watcher reports.
  Process {
    id: pause
    command: ["/usr/bin/rasklad-pause-toggle"]
    // While Rasklad is off the middle click does nothing by design: no complaint.
    onExited: function(exitCode) { if (exitCode !== 0 && root.correction !== "off") root.fail("pause"); else if (exitCode === 0) root.clearFail("pause") }
  }

  visible: label !== ""
  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  WidgetButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: root.label
    dimmed: root.correction === "paused" || root.correction === "off" || root.correction === ""
    fontSize: Style.font.caption
    horizontalMargin: 6
    tooltipText: strings.t(root.failed === "toggle" ? "tooltip.toggleFailed"
                         : root.failed === "pause" ? "tooltip.pauseFailed"
                         : root.correction === "on" ? "tooltip.on"
                         : root.correction === "paused" ? "tooltip.paused"
                         : root.correction === "broken" ? "tooltip.broken"
                         : root.correction === "off" ? "tooltip.off"
                         : "tooltip.unknown").arg(root.code || "—")
    // The bar copies the tooltip when the pointer enters; refresh it in place.
    onTooltipTextChanged: if (tooltipHovered && root.bar) root.bar.showTooltip(button, tooltipText)
    onPressed: function(pressedButton) {
      if (pressedButton === Qt.RightButton) {
        Util.execArgv(["/usr/bin/rasklad-settings"])
      } else if (pressedButton === Qt.MiddleButton) {
        if (!pause.running) pause.running = true
      } else if (pressedButton === Qt.LeftButton && !toggle.running) {
        toggle.running = true
      }
    }
  }
}
