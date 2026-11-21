// SPDX-License-Identifier: MIT
// SYS-227: settings window. Layout and keys only; state lives in `backend`.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 560
    height: 760
    minimumWidth: 420
    minimumHeight: 480
    visible: true
    title: qsTr("Rasklad settings")
    color: theme.background

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground
    Material.roundedScale: theme.radius

    // Friendly names for ids people meet often; anything else shows its id.
    function appName(id) {
        const known = {
            "slack": "Slack", "chromium": "Chromium", "chromium-browser": "Chromium",
            "google-chrome": "Google Chrome", "firefox": "Firefox", "chatgpt": "ChatGPT",
            "org.telegram.desktop": "Telegram", "org.omarchy.agent": "Omarchy agent",
            "obsidian": "Obsidian", "code": "VS Code", "discord": "Discord"
        }
        if (known[id] !== undefined) return known[id]
        const web = /^chrome-(.+?)__.*$/.exec(id)
        if (web) return qsTr("%1 (web app)").arg(web[1])
        return id
    }

    function errorText(code) {
        switch (code) {
        case "fcitx": return qsTr("Fcitx is not running or the Rasklad addon is not loaded. Start Fcitx (fcitx5 -rd) and press Refresh.")
        case "write": return qsTr("Fcitx did not accept the change. Nothing was changed.")
        case "invalid": return qsTr("A window id has only Latin letters, digits, dot, dash and underscore.")
        case "terminal": return qsTr("Terminals are never corrected: they hold commands and passwords.")
        case "full": return qsTr("At most 64 applications.")
        }
        return ""
    }

    Shortcut { sequences: ["Ctrl+R", "F5"]; context: Qt.ApplicationShortcut; onActivated: backend.refresh() }
    Shortcut { sequences: ["Ctrl+Q", "Escape"]; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Ctrl+N"; context: Qt.ApplicationShortcut; onActivated: addField.forceActiveFocus() }

    // Open windows change while the settings are open: look again on focus.
    onActiveChanged: if (active) backend.refreshIfStale()

    component StatusLine: RowLayout {
        id: line
        property bool ok
        property string label
        property string hint
        spacing: 8
        Rectangle {
            Layout.preferredWidth: 10
            Layout.preferredHeight: 10
            radius: 5
            color: line.ok ? theme.accent : theme.error
        }
        Label {
            text: line.label
            font.bold: true
        }
        Label {
            text: line.ok ? "" : line.hint
            color: theme.muted
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    component Badge: Rectangle {
        id: badge
        property alias text: badgeLabel.text
        property color tone: theme.muted
        radius: Math.min(4, theme.radius)
        color: "transparent"
        border.color: tone
        implicitWidth: badgeLabel.implicitWidth + 12
        implicitHeight: badgeLabel.implicitHeight + 4
        Label {
            id: badgeLabel
            anchors.centerIn: parent
            color: badge.tone
            font.pixelSize: 11
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        Label {
            text: "Rasklad"
            font.pixelSize: 24
            font.bold: true
        }
        Label {
            text: qsTr("Fixes words typed in the wrong layout (ghbdtn → привет). Double Shift right after a correction undoes it.")
            wrapMode: Text.WordWrap
            color: theme.muted
            Layout.fillWidth: true
        }

        ColumnLayout {
            spacing: 6
            Layout.fillWidth: true
            StatusLine {
                Layout.fillWidth: true
                ok: backend.fcitxRunning && backend.addonEnabled && backend.configLoaded
                label: ok ? qsTr("Working") : qsTr("Not working")
                hint: qsTr("Fcitx or the addon is not running")
            }
            StatusLine {
                Layout.fillWidth: true
                ok: backend.layoutsReady
                label: qsTr("English and Russian layouts in Fcitx")
                hint: qsTr("add keyboard-us and keyboard-ru to the Fcitx group")
            }
        }

        Rectangle {
            visible: backend.error !== ""
            Layout.fillWidth: true
            implicitHeight: errorLabel.implicitHeight + 16
            radius: theme.radius
            color: "transparent"
            border.color: theme.error
            Label {
                id: errorLabel
                anchors.fill: parent
                anchors.margins: 8
                text: window.errorText(backend.error)
                color: theme.error
                wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Applications")
                font.pixelSize: 17
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("%n on", "", backend.enabledCount)
                color: theme.muted
            }
        }
        Label {
            text: qsTr("Correction works only in the applications switched on here. Password fields and terminals are never touched.")
            wrapMode: Text.WordWrap
            color: theme.muted
            Layout.fillWidth: true
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: backend.apps
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                width: ListView.view.width
                height: 52
                radius: theme.radius
                color: hover.hovered ? theme.surface : "transparent"
                HoverHandler { id: hover }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 4
                    spacing: 8
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Label {
                            text: window.appName(row.modelData.id)
                            font.pixelSize: 15
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            opacity: row.modelData.terminal ? 0.6 : 1
                        }
                        Label {
                            text: row.modelData.id
                            visible: window.appName(row.modelData.id) !== row.modelData.id
                            color: theme.muted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Badge {
                        visible: row.modelData.terminal
                        text: qsTr("terminal")
                        tone: theme.error
                    }
                    Badge {
                        visible: !row.modelData.terminal
                        text: row.modelData.tested ? qsTr("tested") : qsTr("not tested")
                        tone: row.modelData.tested ? theme.accent : theme.muted
                    }
                    Badge {
                        visible: row.modelData.open
                        text: qsTr("open")
                    }
                    Switch {
                        checked: row.modelData.enabled
                        enabled: !row.modelData.terminal && backend.configLoaded
                        // A toggle overwrites `checked`; bind it back so a refused change shows the truth.
                        onToggled: {
                            backend.setAppEnabled(row.modelData.id, checked)
                            checked = Qt.binding(() => row.modelData.enabled)
                        }
                        Accessible.name: window.appName(row.modelData.id)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: addField
                Layout.fillWidth: true
                placeholderText: qsTr("Window id, e.g. org.telegram.desktop")
                enabled: backend.configLoaded
                onAccepted: if (backend.addApp(text)) text = ""
            }
            Button {
                text: qsTr("Add")
                enabled: backend.configLoaded && addField.text.trim() !== ""
                onClicked: if (backend.addApp(addField.text)) addField.text = ""
            }
        }
        Label {
            text: qsTr("Open the application first: it appears in the list by itself.")
            color: theme.muted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Label {
                    text: qsTr("Sound on layout switch")
                    font.pixelSize: 15
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("A short retro cue when the layout changes")
                    color: theme.muted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
            Switch {
                checked: backend.sound
                enabled: backend.configLoaded
                onToggled: {
                    backend.setSound(checked)
                    checked = Qt.binding(() => backend.sound)
                }
                Accessible.name: qsTr("Sound on layout switch")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Ctrl+R refresh · Ctrl+N add · Esc close")
                color: theme.muted
                font.pixelSize: 12
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Refresh")
                flat: true
                onClicked: backend.refresh()
            }
        }
    }
}
