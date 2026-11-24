// SPDX-License-Identifier: MIT
// SYS-227: settings window. Layout and keys only; state lives in `backend`.
// SYS-254: Omarchy look (docs/SYS-254-SETTINGS-DESIGN.md): one scrolling page,
// bordered toggle rows, monospace type scale, colors from the Omarchy theme,
// corners from Hyprland (`corner`), help on ? and keys as in Omarchy apps.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 560
    height: 720
    minimumWidth: 360
    minimumHeight: 400
    visible: true
    title: qsTr("Rasklad settings")
    color: theme.background
    font.family: "monospace"
    font.pixelSize: 12

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground
    Material.roundedScale: corner

    // Omarchy tokens (shell/Commons/Style.qml): the foreground at fixed alphas.
    readonly property color fg: theme.foreground
    readonly property color secondary: Qt.darker(fg, 1.5)
    function tint(a) { return Qt.rgba(fg.r, fg.g, fg.b, a) }
    readonly property int small: Math.min(4, corner)
    // Letters typed into a field are text, never window keys.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.cursorPosition !== undefined

    // The app row that had keyboard focus: a list rebuild (after a switch or a
    // refresh) recreates the rows, and the new one takes the focus back.
    property string rowFocus: ""
    property var rowItems: Object.create(null) // key -> live row; no prototype keys
    property string limitApp: "" // the app whose switch-on hit the 64-app limit
    // Typed adds have no row yet: the limit message shows below the list, at no row.
    function addTyped(text) { limitApp = ""; return backend.addApp(text) }
    function restoreFocus() {
        const row = Object.prototype.hasOwnProperty.call(rowItems, rowFocus) ? rowItems[rowFocus] : null
        if (!focusLost()) return
        if (rowFocus !== "" && row) row.forceActiveFocus()
        else firstRow.forceActiveFocus() // the row went away (a component switched off)
    }
    // After every rebuild, once the old rows are gone and the new ones exist.
    Connections { target: backend; function onChanged() { Qt.callLater(window.restoreFocus) } }
    function focusLost() {
        if (help.visible || revertConfirm.visible) return false // a popup keeps its own focus, also while it animates in
        for (let it = activeFocusItem; it; it = it.parent)
            if (it === content) return false
        return true
    }
    Component.onCompleted: firstRow.forceActiveFocus()

    // The addon notes new programs while the window is open: look again on focus.
    onActiveChanged: if (active) backend.refreshIfStale()
    // Keyboard focus (Tab, j/k) never lands outside the visible part of the page.
    onActiveFocusItemChanged: {
        for (let it = activeFocusItem; it; it = it.parent)
            if (it === content) { reveal(activeFocusItem); return }
    }
    function reveal(item) {
        const y = item.mapToItem(content, 0, 0).y + content.y
        const max = Math.max(0, page.contentHeight - page.height)
        if (y < page.contentY) page.contentY = Math.max(0, y - 20)
        else if (y + item.height > page.contentY + page.height)
            page.contentY = Math.min(max, y + item.height - page.height + 20)
    }

    Shortcut { sequences: ["Ctrl+R", "F5"]; onActivated: backend.refresh() }
    Shortcut { sequence: "F1"; onActivated: help.opened ? help.close() : help.open() }
    Shortcut { sequence: "?"; enabled: !window.typing; onActivated: help.opened ? help.close() : help.open() }
    Shortcut { sequence: "Ctrl+Q"; onActivated: Qt.quit() }
    // Й is Q in the Russian layout: the keys work in either layout.
    Shortcut { sequences: ["Q", "Й"]; autoRepeat: false; enabled: !window.typing && !help.opened; onActivated: Qt.quit() }
    Shortcut {
        sequence: "Escape"
        autoRepeat: false // a held Esc takes one step, never clear-then-quit
        onActivated: {
            if (help.opened) help.close()
            else if (addBox.visible) { addBox.visible = false; addField.text = ""; addButton.forceActiveFocus() }
            else if (window.typing && activeFocusItem.text !== "") activeFocusItem.text = "" // keep typed text from a stray quit
            else if (window.typing) firstRow.forceActiveFocus()
            else Qt.quit()
        }
    }
    Shortcut {
        sequence: "Ctrl+N"
        enabled: backend.configLoaded
        onActivated: { addBox.visible = true; window.reveal(addBox); addField.forceActiveFocus() }
    }

    function errorText(code) {
        switch (code) {
        case "write": return qsTr("Fcitx did not accept the change. Nothing was changed.")
        case "invalid": return qsTr("Use Latin letters, digits, dot, dash or underscore.")
        case "terminal": return qsTr("Terminals are not supported.")
        case "full": return qsTr("At most 64 applications can be on.")
        case "word": return qsTr("One word of Latin or Cyrillic letters, up to 32.")
        case "words-full": return qsTr("At most 256 exceptions.")
        }
        return ""
    }
    // Only the most serious problem, with what to do about it.
    readonly property string banner: {
        if (!backend.fcitxRunning || !backend.addonEnabled || !backend.configLoaded)
            return qsTr("Rasklad is not running. Start Fcitx (fcitx5 -rd), then press Retry.")
        if (!backend.wordOptions) return qsTr("The Rasklad addon is older than this window. Update Rasklad and restart Fcitx.")
        if (!backend.layoutsReady) return qsTr("Add the English and Russian layouts (keyboard-us, keyboard-ru) to the Fcitx group.")
        if (backend.profileBroken) return qsTr("Automatic correction is unavailable: the dictionaries did not pass verification. Reinstall Rasklad.")
        if (backend.error === "write") return errorText("write")
        return ""
    }

    // ---- Omarchy components ------------------------------------------------

    component SectionHeader: ColumnLayout {
        property alias text: headerLabel.text
        spacing: 8
        Layout.fillWidth: true
        Layout.topMargin: 10
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: window.tint(0.12) }
        Label {
            id: headerLabel
            font.pixelSize: 10
            font.bold: true
            font.letterSpacing: 1
            color: Qt.darker(window.fg, 1.4)
            Accessible.role: Accessible.Heading
        }
    }

    // Square on sharp corners, a pill when Hyprland rounds them; display only.
    component ToggleSwitch: Rectangle {
        property bool on
        implicitWidth: 36
        implicitHeight: 20
        radius: corner > 0 ? height / 2 : 0
        color: on ? window.tint(0.18) : "transparent"
        border.color: window.tint(on ? 0.7 : 0.4)
        Rectangle {
            width: 12
            height: 12
            radius: corner > 0 ? width / 2 : 0
            anchors.verticalCenter: parent.verticalCenter
            x: parent.on ? parent.width - width - 4 : 4
            color: parent.on ? window.fg : window.tint(0.45)
            Behavior on x { NumberAnimation { duration: 90 } }
        }
    }

    // One bordered row that owns the click, Space and Enter; j/k move between rows.
    component ToggleRow: Rectangle {
        id: row
        property string title
        property string detail
        property string tip
        property string iconName
        property string key // app id, for focus after a rebuild
        readonly property bool isRow: true
        property bool showIcon: false
        property bool checked
        property bool available: true
        signal toggled(bool on)
        Layout.fillWidth: true
        implicitHeight: Math.max(54, texts.implicitHeight + 20)
        radius: window.small
        activeFocusOnTab: available
        opacity: available ? 1 : 0.5
        color: mouse.pressed ? window.tint(0.22) : (mouse.containsMouse || activeFocus) ? window.tint(0.08) : window.tint(0.04)
        border.color: activeFocus ? theme.accent : window.tint(mouse.containsMouse ? 0.25 : 0.4)
        Accessible.role: Accessible.CheckBox
        Accessible.name: title
        Accessible.description: detail
        Accessible.checkable: true
        Accessible.checked: checked
        Accessible.focusable: true
        Accessible.onToggleAction: flip()
        Accessible.onPressAction: flip()
        function flip() {
            if (!available) return
            if (key !== "") window.rowFocus = key
            toggled(!checked)
        }
        onActiveFocusChanged: if (activeFocus) window.rowFocus = key
        Component.onCompleted: if (key !== "") window.rowItems[key] = row
        Component.onDestruction: if (key !== "" && window.rowItems[key] === row) delete window.rowItems[key]
        // j/k walk rows only, never into fields where they are letters.
        function step(forward) {
            let it = nextItemInFocusChain(forward)
            for (let i = 0; i < 1000 && it && it !== row && !it.isRow; ++i) it = it.nextItemInFocusChain(forward)
            if (it && it.isRow) it.forceActiveFocus()
        }
        Keys.onPressed: event => {
            if (event.isAutoRepeat && (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                event.accepted = true // a held key switches once
                return
            }
            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { flip(); event.accepted = true; return }
            if (event.modifiers !== Qt.NoModifier && event.modifiers !== Qt.KeypadModifier) return
            // j/k, and о/л in the same places of the Russian layout.
            const t = event.text.toLowerCase()
            if (event.key === Qt.Key_Down || t === "j" || t === "о") { step(true); event.accepted = true }
            else if (event.key === Qt.Key_Up || t === "k" || t === "л") { step(false); event.accepted = true }
        }
        Rectangle { // keyboard cursor bar, as in Omarchy menus
            visible: row.activeFocus
            width: 3
            height: parent.height
            color: theme.accent
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: row.available ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: { row.forceActiveFocus(); row.flip() }
        }
        ToolTip.visible: tip !== "" && (mouse.containsMouse || activeFocus)
        ToolTip.delay: 600
        ToolTip.text: tip
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 12
            spacing: 12
            Item {
                visible: row.showIcon
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                Image {
                    id: icon
                    anchors.fill: parent
                    sourceSize: Qt.size(56, 56)
                    source: row.iconName !== "" ? "image://appicon/" + encodeURIComponent(row.iconName) : ""
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                }
                Rectangle { // a letter when the app has no icon
                    anchors.fill: parent
                    visible: icon.status !== Image.Ready
                    radius: window.small
                    color: window.tint(0.08)
                    Label {
                        anchors.centerIn: parent
                        text: row.title.length > 0 ? row.title.charAt(0).toUpperCase() : "?"
                        textFormat: Text.PlainText
                        font.pixelSize: 13
                        font.bold: true
                    }
                }
            }
            ColumnLayout {
                id: texts
                spacing: 2
                Layout.fillWidth: true
                Label {
                    text: row.title
                    Accessible.ignored: true
                    textFormat: Text.PlainText
                    font.pixelSize: 13
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    Accessible.ignored: true
                    visible: row.detail !== ""
                    text: row.detail
                    textFormat: Text.PlainText
                    font.pixelSize: 11
                    color: window.secondary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
            ToggleSwitch { on: row.checked }
        }
    }

    component Hint: Label {
        font.pixelSize: 11
        color: window.secondary
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
        Layout.fillWidth: true
    }

    component InlineError: Label {
        font.pixelSize: 11
        color: theme.error
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
        Layout.fillWidth: true
    }

    // ---- Page ----------------------------------------------------------------

    // The whole page scrolls, so a small tiled window still reaches every row.
    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 36
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: content
            x: 18
            y: 18
            width: page.width - 36
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: 4
                    Layout.fillWidth: true
                    Label {
                        text: "Rasklad"
                        font.pixelSize: 24
                        font.bold: true
                    }
                    Hint {
                        text: qsTr("Fixes words typed in the wrong layout: ghbdtn → привет.")
                        font.pixelSize: 12
                    }
                }
                ToolButton {
                    text: "?"
                    font.pixelSize: 16
                    font.bold: true
                    Layout.alignment: Qt.AlignTop
                    onClicked: help.open()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Help (?)")
                    Accessible.name: qsTr("Help")
                }
            }

            Rectangle {
                visible: window.banner !== ""
                Layout.fillWidth: true
                Layout.topMargin: 6
                implicitHeight: bannerRow.implicitHeight + 20
                radius: window.small
                color: "transparent"
                border.color: theme.error
                RowLayout {
                    id: bannerRow
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10
                    Label {
                        text: window.banner
                        color: theme.error
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Accessible.role: Accessible.AlertMessage
                    }
                    Button {
                        visible: !backend.configLoaded
                        text: qsTr("Retry")
                        flat: true
                        onClicked: backend.refresh()
                    }
                }
            }

            // ---- Correction
            SectionHeader { text: qsTr("CORRECTION") }
            ToggleRow {
                id: firstRow
                title: qsTr("Automatic correction")
                detail: qsTr("When off, double Shift still fixes a word.")
                checked: backend.autoCorrect
                available: backend.wordOptions
                onToggled: on => backend.setAutoCorrect(on)
            }
            ToggleRow {
                title: qsTr("Sound on layout switch")
                checked: backend.sound
                available: backend.configLoaded
                onToggled: on => backend.setSound(on)
            }

            // ---- Applications
            SectionHeader { text: qsTr("APPLICATIONS") }
            Hint { text: qsTr("Rasklad works only in the applications switched on here. Terminals are not supported.") }

            TextField {
                id: filter
                visible: backend.apps.length > 12
                placeholderText: qsTr("Find an application")
                placeholderTextColor: window.secondary
                Layout.fillWidth: true
                Accessible.name: placeholderText
            }

            Repeater {
                model: [
                    { key: "on", title: qsTr("On") },
                    { key: "recent", title: qsTr("Recently used") },
                    { key: "recommended", title: qsTr("Recommended") }
                ]
                delegate: ColumnLayout {
                    id: group
                    required property var modelData
                    readonly property var rows: backend.apps.filter(a => {
                        if (a.group !== modelData.key) return false
                        const q = filter.visible ? filter.text.trim().toLowerCase() : ""
                        return q === "" || a.name.toLowerCase().includes(q) || a.id.toLowerCase().includes(q)
                    })
                    visible: rows.length > 0
                    spacing: 6
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    Label {
                        text: group.modelData.title
                        font.pixelSize: 11
                        color: window.secondary
                    }
                    Repeater {
                        model: group.rows
                        delegate: ToggleRow {
                            required property var modelData
                            key: modelData.id
                            showIcon: true
                            iconName: modelData.icon
                            title: modelData.name
                            // The limit is said at the row that hit it, not only below the list.
                            detail: backend.error === "full" && modelData.id === window.limitApp && !modelData.enabled
                                    ? window.errorText("full")
                                  : modelData.partial ? qsTr("On only for part of its program names.")
                                  : modelData.description !== "" ? modelData.description
                                  : modelData.webApp ? qsTr("Web app") : ""
                            tip: qsTr("Program name: %1").arg(modelData.names.join(", "))
                            Accessible.description: detail === "" ? tip : detail + ". " + tip
                            checked: modelData.enabled
                            available: backend.configLoaded
                            onToggled: on => { window.limitApp = modelData.id; backend.setAppEnabled(modelData.id, on) }
                        }
                    }
                }
            }

            RowLayout {
                visible: !addBox.visible
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 10
                Button {
                    id: addButton
                    text: qsTr("Add manually…")
                    flat: true
                    enabled: backend.configLoaded
                    onClicked: { addBox.visible = true; addField.forceActiveFocus() }
                }
                Hint { text: qsTr("Not in the list? Click a text field in the application.") }
            }
            ColumnLayout {
                id: addBox
                visible: false
                spacing: 4
                Layout.fillWidth: true
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: addField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Program name, e.g. Telegram")
                        placeholderTextColor: window.secondary
                        enabled: backend.configLoaded
                        onAccepted: if (window.addTyped(text)) { text = ""; addBox.visible = false; addButton.forceActiveFocus() }
                        Accessible.name: placeholderText
                    }
                    Button {
                        text: qsTr("Add")
                        enabled: backend.configLoaded && addField.text.trim() !== ""
                        onClicked: if (window.addTyped(addField.text)) { addField.text = ""; addBox.visible = false; addButton.forceActiveFocus() }
                    }
                }
                InlineError {
                    visible: ["invalid", "terminal"].includes(backend.error)
                    text: window.errorText(backend.error)
                }
            }
            InlineError { // a switched row can hit the limit too, with the form closed
                visible: backend.error === "full"
                text: window.errorText(backend.error)
            }

            // ---- Exceptions
            SectionHeader { text: qsTr("EXCEPTIONS") }
            Hint { text: qsTr("Never correct these words automatically, in either layout.") }
            Flow {
                id: words
                visible: backend.exceptions.length > 0
                spacing: 6
                Layout.fillWidth: true
                Repeater {
                    model: backend.exceptions
                    delegate: Rectangle {
                        id: chip
                        required property string modelData
                        radius: window.small
                        color: window.tint(0.04)
                        border.color: window.tint(0.4)
                        width: Math.min(chipRow.implicitWidth + 8, words.width)
                        height: 28
                        RowLayout {
                            id: chipRow
                            anchors.verticalCenter: parent.verticalCenter
                            x: 8
                            width: parent.width - 8
                            spacing: 0
                            Label {
                                text: chip.modelData
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            ToolButton {
                                text: "×"
                                implicitWidth: 32
                                implicitHeight: 28
                                enabled: backend.wordOptions
                                onClicked: backend.removeException(chip.modelData)
                                Accessible.name: qsTr("Remove %1").arg(chip.modelData)
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: wordField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Word, e.g. kek")
                    placeholderTextColor: window.secondary
                    enabled: backend.wordOptions
                    onAccepted: if (backend.addException(text)) text = ""
                    Accessible.name: placeholderText
                }
                Button {
                    text: qsTr("Add")
                    enabled: backend.wordOptions && wordField.text.trim() !== ""
                    onClicked: if (backend.addException(wordField.text)) wordField.text = ""
                }
            }
            InlineError {
                visible: ["word", "words-full"].includes(backend.error)
                text: window.errorText(backend.error)
            }

            // ---- Omarchy connection (SYS-260): states from rasklad-setup --check.
            // The window never edits these files; the buttons open rasklad-setup in
            // a terminal, where the user sees the changes and agrees to them.
            SectionHeader {
                visible: setupBox.visible
                text: qsTr("OMARCHY CONNECTION")
            }
            ColumnLayout {
                id: setupBox
                // Shown while there are states, or something to revert even if the check gave no answer.
                readonly property bool hasRows: Object.keys(backend.setupRows).length > 0
                visible: hasRows || backend.setupInstalled
                spacing: 8
                Layout.fillWidth: true
                readonly property var rows: !hasRows ? [] : [
                    { title: qsTr("Layout icon at the top right"), state: backend.setupRows.icon },
                    { title: qsTr("Double Alt switches the layout"), state: backend.setupRows.alt },
                    { title: qsTr("English and Russian layouts in Fcitx"), state: backend.setupRows.layouts }
                ]
                readonly property bool anyMissing: rows.some(r => r.state === "missing")
                Hint { text: qsTr("Connecting changes the Hyprland, bar and Fcitx settings. A terminal shows each change and asks first.") }
                Repeater {
                    model: setupBox.rows
                    delegate: Rectangle {
                        id: stateRow
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: Math.max(44, stateTexts.implicitHeight + 16)
                        radius: window.small
                        color: window.tint(0.04)
                        border.color: window.tint(0.4)
                        Accessible.role: Accessible.StaticText
                        Accessible.name: modelData.title + ": " + stateLabel.text
                        RowLayout {
                            id: stateTexts
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 12
                            Label {
                                text: stateRow.modelData.title
                                font.pixelSize: 13
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                Accessible.ignored: true
                            }
                            Label {
                                id: stateLabel
                                Accessible.ignored: true
                                font.pixelSize: 11
                                font.bold: stateRow.modelData.state === "ok"
                                color: stateRow.modelData.state === "ok" ? window.fg
                                     : (stateRow.modelData.state === "missing" || stateRow.modelData.state === "problem")
                                       ? theme.error : window.secondary
                                text: stateRow.modelData.state === "ok" ? qsTr("connected")
                                    : stateRow.modelData.state === "missing" ? qsTr("not connected")
                                    : stateRow.modelData.state === "problem" ? qsTr("error")
                                    : stateRow.modelData.state === "other" ? qsTr("set up differently")
                                    : qsTr("unknown")
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Button {
                        visible: setupBox.anyMissing || setupBox.rows.some(r => r.state === "problem")
                        text: qsTr("Connect…")
                        onClicked: setupError.failed = backend.openSetup("install") ? "" : "install"
                    }
                    Button {
                        visible: backend.setupInstalled
                        flat: true
                        text: qsTr("Restore Omarchy defaults…")
                        onClicked: revertConfirm.open()
                    }
                }
                InlineError {
                    id: setupError
                    property string failed: ""
                    visible: failed !== ""
                    text: qsTr("Could not open a terminal. Run rasklad-setup --%1 in one.").arg(failed)
                }
            }
        }
    }

    // Reverting puts back saved files: ask first, as rasklad-setup --install does.
    Dialog {
        id: revertConfirm
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(window.width - 32, 440)
        title: qsTr("Restore Omarchy defaults?")
        background: Rectangle {
            color: theme.background
            radius: window.small
            border.color: window.tint(0.4)
        }
        contentItem: Hint {
            text: qsTr("The Hyprland, bar and Fcitx settings go back to how they were before Rasklad was connected. Files you have changed since then are left as they are.")
            font.pixelSize: 12
        }
        footer: DialogButtonBox {
            Button {
                text: qsTr("Restore")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: setupError.failed = backend.openSetup("revert") ? "" : "revert"
    }

    // ---- Help (? or F1) ------------------------------------------------------------

    Popup {
        id: help
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(window.width - 32, 480)
        height: Math.min(window.height - 32, implicitHeight)
        padding: 18
        property Item returnTo: null
        onAboutToShow: returnTo = window.activeFocusItem
        onOpened: helpClose.forceActiveFocus()
        // A row destroyed while help was open nulls returnTo: find its successor.
        onClosed: if (returnTo) returnTo.forceActiveFocus(); else window.restoreFocus()
        background: Rectangle {
            color: theme.background
            radius: window.small
            border.color: window.tint(0.4)
        }
        contentItem: Flickable {
            implicitHeight: helpBody.implicitHeight
            contentHeight: helpBody.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar {}
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTr("Help")
            ColumnLayout {
                id: helpBody
                width: parent.width
                spacing: 10
                Label {
                    text: qsTr("Help")
                    font.pixelSize: 16
                    font.bold: true
                }
                Hint { text: qsTr("Double Shift changes the layout of a word, or undoes the correction just made. Double Alt switches the layout."); font.pixelSize: 12 }
                Hint { text: qsTr("To pause automatic correction, switch it off here or middle-click the layout icon at the top right. Double Shift and double Alt keep working."); font.pixelSize: 12 }
                Hint { text: qsTr("An application appears in the list once you click a text field in it. The list starts over when Fcitx restarts."); font.pixelSize: 12 }
                Hint { text: qsTr("A password field that the application does not mark as one looks like ordinary text to Rasklad."); font.pixelSize: 12 }
                GridLayout {
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 4
                    Layout.topMargin: 4
                    Repeater {
                        model: [
                            "Space, Enter", qsTr("switch the selected row"),
                            "j, k, ↓, ↑", qsTr("next or previous row"),
                            "Tab", qsTr("next control"),
                            "Ctrl+N", qsTr("add an application"),
                            "Ctrl+R, F5", qsTr("refresh"),
                            "?, F1", qsTr("help"),
                            "Q, Esc", qsTr("close")
                        ]
                        delegate: Label {
                            required property string modelData
                            required property int index
                            text: modelData
                            textFormat: Text.PlainText
                            font.bold: index % 2 === 0
                            color: index % 2 === 0 ? window.fg : window.secondary
                        }
                    }
                }
                Button {
                    id: helpClose
                    text: qsTr("Close")
                    Layout.alignment: Qt.AlignRight
                    onClicked: help.close()
                }
            }
        }
    }
}
