// rasklad manual-preview window (SYS-239).
//
// User text is shown only in PlainText editors; labels are PlainText too, so
// no input is ever interpreted as rich text. All state lives in the C++
// DocumentController; this file only displays it and forwards explicit
// actions. There is no console logging.

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: window

    required property QtObject controller
    required property QtObject theme
    required property QtObject editorGuard
    required property QtObject clipboard
    property QtObject soundFeedback: null
    required property bool profileRequested

    width: 1040
    height: 680
    minimumWidth: 560
    // Fixed: content that does not fit scrolls inside documentScroll.
    minimumHeight: 480
    visible: true
    title: qsTr("rasklad — layout preview")
    color: theme.background
    font.family: "monospace"
    font.pixelSize: 13

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.background: theme.background
    Material.foreground: theme.foreground
    Material.primary: theme.surface
    Material.roundedScale: theme.radius

    readonly property bool wide: width >= 860
    readonly property int gap: 8
    property bool syncingEditor: false
    property bool allowClose: false
    property bool discardForClose: false
    // Fail closed: typing is enabled only after both editors' native undo
    // history was disabled by the guard.
    property bool editorReady: false
    property bool editorChecked: false

    function requestClear() {
        discardForClose = false
        if (controller.sourceLength > 0)
            discardDialog.open()
        else
            controller.clear()
    }

    onClosing: function(event) {
        if (!allowClose && controller.sourceLength > 0) {
            event.accepted = false
            discardForClose = true
            discardDialog.open()
        }
    }

    Shortcut {
        sequence: "Ctrl+Q"
        context: Qt.ApplicationShortcut
        onActivated: window.close()
    }
    Shortcut {
        sequence: "F1"
        context: Qt.ApplicationShortcut
        onActivated: helpDialog.open()
    }

    Dialog {
        id: soundDialog
        objectName: "soundDialog"
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: Math.min(360, window.width - 32)
        modal: true
        enter: Transition {}
        exit: Transition {}
        title: qsTr("Sound")
        standardButtons: Dialog.Close
        Material.theme: window.theme.dark ? Material.Dark : Material.Light
        Material.accent: window.theme.accent
        Material.foreground: window.theme.foreground
        Material.background: window.theme.surface
        Material.roundedScale: window.theme.radius
        background: Rectangle {
            color: window.theme.surface
            radius: window.theme.radius
            border.width: 1
            border.color: window.theme.muted
        }
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.55) }
        contentItem: ColumnLayout {
            CheckBox {
                objectName: "soundEnabled"
                text: qsTr("Enable sound")
                checked: window.soundFeedback ? window.soundFeedback.enabled : false
                onToggled: if (window.soundFeedback) window.soundFeedback.enabled = checked
            }
            ActionButton {
                objectName: "previewSoundButton"
                text: qsTr("Preview sound")
                enabled: window.soundFeedback ? window.soundFeedback.enabled : false
                onClicked: window.soundFeedback.preview()
            }
            PlainLabel {
                Layout.fillWidth: true
                text: qsTr("Sound unavailable")
                color: window.theme.error
                visible: window.soundFeedback ? window.soundFeedback.unavailable : false
            }
        }
    }

    Dialog {
        id: discardDialog
        objectName: "discardDialog"
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: Math.min(440, window.width - 32)
        modal: true
        enter: Transition {}
        exit: Transition {}
        title: qsTr("Discard text?")
        contentItem: PlainLabel {
            text: qsTr("Text is kept only in this window. Copy it before closing or clearing.")
        }
        footer: DialogButtonBox {
            ActionButton {
                objectName: "discardButton"
                text: qsTr("Discard")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            ActionButton {
                objectName: "cancelDiscardButton"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: {
            window.controller.clear()
            if (window.discardForClose) {
                window.allowClose = true
                window.close()
            }
        }
    }

    Dialog {
        id: helpDialog
        objectName: "helpDialog"
        parent: Overlay.overlay
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: Math.min(480, window.width - 32)
        modal: true
        enter: Transition {}
        exit: Transition {}
        title: qsTr("Keyboard shortcuts")
        contentItem: PlainLabel {
            text: qsTr("Ctrl+Enter — Preview\nCtrl+Shift+Enter — Apply\nCtrl+Z — Undo the last Apply or bulk edit (one step)\nCtrl+Shift+C — Copy\nCtrl+Shift+K — Check word\nCtrl+Q — Close\nF1 — This help\nEscape — Close dialog")
        }
        footer: DialogButtonBox {
            ActionButton {
                text: qsTr("Close")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }
    }

    // Inline components do not see outer ids; the color comes from the
    // Material.foreground set on the window and inherited by children.
    component ActionButton: Button {
        id: control
        leftPadding: 12
        rightPadding: 12
        topPadding: 7
        bottomPadding: 7
        readonly property color ink: control.Material.foreground
        readonly property color accentColor: control.Material.accent
        readonly property color accentInk: {
            function linear(v) { return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
            const luminance = 0.2126 * linear(accentColor.r) + 0.7152 * linear(accentColor.g) + 0.0722 * linear(accentColor.b)
            return luminance > 0.179 ? "#000000" : "#ffffff"
        }
        contentItem: Text {
            text: control.text
            textFormat: Text.PlainText
            font: control.font
            color: control.highlighted ? control.accentInk : control.ink
            opacity: control.enabled ? 1 : 0.4
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            implicitWidth: 84
            implicitHeight: 32
            radius: 0
            color: control.highlighted ? control.accentColor
                : control.hovered ? Qt.lighter(control.Material.background, 1.25) : control.Material.background
            opacity: control.enabled ? 1 : 0.4
            border.width: control.activeFocus ? 2 : 1
            border.color: control.activeFocus || control.highlighted ? control.accentColor
                : Qt.rgba(control.ink.r, control.ink.g, control.ink.b, 0.35)
        }
    }

    component PlainLabel: Label {
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
    }

    // ---- Text mapping (fixed codes from C++ -> translated text) ----

    function noticeText(code) {
        switch (code) {
        case "inputTooLong":
            return qsTr("That edit was not accepted: the text would exceed %1 characters. Nothing was cut off; your previous text is unchanged.").arg(controller.maxInputLength)
        case "previewStale":
            return qsTr("The preview was cleared because the text, direction or word data changed.")
        case "nothingToConvert":
            return qsTr("There is no text to convert.")
        case "conversionNoChange":
            return qsTr("Nothing to convert in this direction.")
        case "analysisUnavailable":
            return qsTr("Word check is not available. Manual conversion still works.")
        case "checkNeedsWord":
            return qsTr("Place the cursor on a word or select one word.")
        case "checkWordTooLong":
            return qsTr("Select a shorter word (at most 64 characters).")
        case "nothingToApply":
            return qsTr("There is no preview to apply.")
        case "applied":
            return qsTr("Preview applied. Use Undo to restore the previous text.")
        case "undoUnavailable":
            return qsTr("There is nothing to undo.")
        case "undoStale":
            return qsTr("Undo is no longer possible because the text changed.")
        case "undone":
            return qsTr("Restored the text from before Apply.")
        case "undoneEdit":
            return qsTr("Restored the text from before the last bulk edit.")
        case "copiedPreview":
            return qsTr("Preview copied to the clipboard.")
        case "copiedText":
            return qsTr("Your text copied to the clipboard.")
        case "nothingToCopy":
            return qsTr("There is nothing to copy.")
        case "copyFailed":
            return qsTr("The clipboard is not available.")
        case "cleared":
            return qsTr("Text, preview and undo were cleared.")
        }
        return ""
    }

    function noticeIsError(code) {
        return code === "inputTooLong" || code === "analysisUnavailable" || code === "undoStale"
            || code === "copyFailed"
    }

    function actionTitle(action) {
        switch (action) {
        case "keep":
            return qsTr("Keep the word")
        case "correct":
            return qsTr("Likely typed in the wrong layout")
        case "suggest":
            return qsTr("Possibly typed in the wrong layout (suggestion)")
        case "abstain":
            return qsTr("No recommendation")
        }
        return ""
    }

    function reasonText(reason) {
        switch (reason) {
        case "protectedField":
            return qsTr("The field is protected; nothing is analysed.")
        case "unknownContext":
            return qsTr("The input context is unknown; nothing is analysed.")
        case "invalidRequest":
            return qsTr("The request was not valid.")
        case "unsupportedToken":
            return qsTr("The word contains characters outside the selected layout.")
        case "tooShort":
            return qsTr("Words shorter than four letters are not judged.")
        case "tooLong":
            return qsTr("Words longer than 32 letters are not judged.")
        case "evidenceUnavailable":
            return qsTr("Local word data is not available.")
        case "invalidEvidence":
            return qsTr("Local word data returned an invalid answer.")
        case "observedWordKnown":
            return qsTr("The word as typed is in the dictionary of its layout.")
        case "sharedAmbiguity":
            return qsTr("Both readings look like real words; the choice is yours.")
        case "dictionaryAndModelAgree":
            return qsTr("The converted word is in the other dictionary, and the letter model agrees.")
        case "dictionaryOnly":
            return qsTr("The converted word is in the other dictionary, but the letter model does not confirm it.")
        case "modelOnly":
            return qsTr("Only the letter model prefers the other layout; neither dictionary knows the word.")
        case "noEvidence":
            return qsTr("Neither the dictionaries nor the letter model support a change.")
        }
        return ""
    }

    function analysisText(state) {
        switch (state) {
        case "loading":
            return qsTr("Word check: verifying local data…")
        case "ready":
            return qsTr("Word check: ready (local)")
        case "unavailable":
            return qsTr("Word check unavailable")
        }
        return qsTr("Word check: off")
    }

    function analysisHint(state) {
        switch (state) {
        case "loading":
            return qsTr("Manual conversion already works while local data is verified.")
        case "ready":
            return qsTr("Checks one word on this computer. Nothing is sent anywhere.")
        case "unavailable":
            return qsTr("Local word data failed verification. Manual conversion still works.")
        }
        return qsTr("Optional word check is not enabled.")
    }

    function previewTitle() {
        if (controller.previewKind === "manual")
            return controller.direction === 0 ? qsTr("Preview: whole text, US → RU") : qsTr("Preview: whole text, RU → US")
        if (controller.previewKind === "check")
            return qsTr("Preview: one word replaced")
        return qsTr("Preview")
    }

    // ---- Editor synchronisation ----

    function resetEditor() {
        const position = sourceEditor.cursorPosition
        syncingEditor = true
        sourceEditor.text = controller.sourceText
        sourceEditor.cursorPosition = Math.min(position, sourceEditor.length)
        syncingEditor = false
    }

    function pushEdit() {
        if (syncingEditor || sourceEditor.text === controller.sourceText)
            return
        if (!controller.setSourceText(sourceEditor.text))
            resetEditor()
    }

    function copyCurrent() {
        if (controller.copyKind === "none")
            return
        if (clipboard.setText(controller.copyPayload()))
            controller.noteCopied()
        else
            controller.noteCopyFailed()
    }

    Connections {
        target: window.controller
        function onSourceTextChanged() {
            if (sourceEditor.text !== window.controller.sourceText)
                window.resetEditor()
        }
        function onSourceRejected() {
            window.resetEditor()
        }
        // User clicks break the radio bindings; keep them in sync explicitly.
        function onDirectionChanged() {
            usToRu.checked = window.controller.direction === 0
            ruToUs.checked = window.controller.direction === 1
        }
    }

    // ---- Actions ----

    Action {
        id: previewAction
        text: qsTr("Preview")
        shortcut: "Ctrl+Return"
        onTriggered: window.controller.preview()
    }
    Action {
        id: checkAction
        text: qsTr("Check word")
        shortcut: "Ctrl+Shift+K"
        enabled: window.controller.analysisState === "ready"
        onTriggered: window.controller.checkWord(sourceEditor.selectionStart, sourceEditor.selectionEnd)
    }
    Action {
        id: applyAction
        text: qsTr("Apply")
        shortcut: "Ctrl+Shift+Return"
        enabled: window.controller.canApply
        onTriggered: window.controller.apply()
    }
    Action {
        id: undoAction
        text: qsTr("Undo")
        shortcut: "Ctrl+Z"
        enabled: window.controller.canUndo
        onTriggered: window.controller.undo()
    }
    Action {
        id: copyAction
        text: window.controller.copyKind === "preview" ? qsTr("Copy preview") : qsTr("Copy text")
        shortcut: "Ctrl+Shift+C"
        enabled: window.controller.copyKind !== "none"
        onTriggered: window.copyCurrent()
    }
    Action {
        id: clearAction
        text: qsTr("Clear")
        enabled: window.controller.sourceLength > 0 || window.controller.previewKind !== "none"
            || window.controller.canUndo
        onTriggered: window.requestClear()
    }

    // ---- Layout ----

    // Editor panes grow with the window; short windows scroll vertically
    // instead of clipping stacked content. Dialogs stay in Overlay.
    Flickable {
        id: documentScroll
        objectName: "documentScroll"
        anchors.fill: parent
        clip: true
        // Keep room for the bar even while hidden, so wrapped labels retain
        // the same width as the content becomes scrollable.
        contentWidth: Math.max(0, width - documentBar.implicitWidth)
        ScrollBar.vertical: ScrollBar {
            id: documentBar
            policy: ScrollBar.AsNeeded
        }
        // The viewport height stays independent of implicit layout size.
        // Content uses the preferred layout size when it exceeds the viewport.
        contentHeight: mainColumn.implicitHeight + 24
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: mainColumn
            x: 12
            y: 12
            width: Math.max(0, documentScroll.contentWidth - 24)
            spacing: window.gap

            // Fail-closed editor setup: generic message, no details.
            Rectangle {
                objectName: "editorError"
                Layout.fillWidth: true
                visible: window.editorChecked && !window.editorReady
                implicitHeight: editorErrorText.implicitHeight + 12
                radius: window.theme.radius
                color: window.theme.surface
                border.width: 1
                border.color: window.theme.error
                PlainLabel {
                    id: editorErrorText
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    verticalAlignment: Text.AlignVCenter
                    color: window.theme.error
                    text: qsTr("The editor could not be prepared safely, so typing is disabled. Restart the app.")
                    Accessible.role: Accessible.AlertMessage
                }
            }

            // Header
            RowLayout {
                Layout.fillWidth: true
                spacing: window.gap

                ColumnLayout {
                    spacing: 2
                    Layout.fillWidth: true
                    PlainLabel {
                        text: "rasklad"
                        font.pixelSize: 22
                        font.bold: true
                        wrapMode: Text.NoWrap
                    }
                    PlainLabel {
                        Layout.fillWidth: true
                        text: qsTr("Manual keyboard-layout preview")
                        color: window.theme.muted
                    }
                }

                ToolButton {
                    objectName: "soundButton"
                    text: qsTr("Sound")
                    visible: window.soundFeedback !== null
                    onClicked: soundDialog.open()
                }
                ToolButton {
                    text: qsTr("Keys")
                    onClicked: helpDialog.open()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Keyboard shortcuts (F1)")
                }
                Rectangle {
                    id: statusChip
                    objectName: "analysisStatus"
                    Layout.alignment: Qt.AlignTop
                    Layout.maximumWidth: window.width * 0.45
                    implicitWidth: statusLabel.implicitWidth + 24
                    implicitHeight: statusLabel.implicitHeight + 12
                    radius: window.theme.radius
                    color: window.theme.surface
                    border.width: 1
                    border.color: window.controller.analysisState === "unavailable" ? window.theme.error
                        : (window.controller.analysisState === "ready" ? window.theme.accent : window.theme.muted)

                    PlainLabel {
                        id: statusLabel
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, statusChip.Layout.maximumWidth - 24)
                        text: window.analysisText(window.controller.analysisState)
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }
                    HoverHandler {
                        id: statusHover
                    }
                    ToolTip.visible: statusHover.hovered
                    ToolTip.text: window.analysisHint(window.controller.analysisState)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: statusLabel.text + ". " + window.analysisHint(window.controller.analysisState)
                }
            }

            // Direction and actions
            Flow {
                Layout.fillWidth: true
                spacing: 8

                PlainLabel {
                    text: qsTr("Direction")
                    height: usToRu.height
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.NoWrap
                    color: window.theme.muted
                }
                ButtonGroup {
                    id: directionGroup
                }
                RadioButton {
                    id: usToRu
                    objectName: "directionUsToRu"
                    text: qsTr("US layout → Russian")
                    ButtonGroup.group: directionGroup
                    checked: window.controller.direction === 0
                    onToggled: if (checked) window.controller.direction = 0
                }
                RadioButton {
                    id: ruToUs
                    objectName: "directionRuToUs"
                    text: qsTr("RU layout → English")
                    ButtonGroup.group: directionGroup
                    checked: window.controller.direction === 1
                    onToggled: if (checked) window.controller.direction = 1
                }
                Item {
                    width: 8
                    height: 1
                }
                ActionButton {
                    objectName: "previewButton"
                    action: previewAction
                    highlighted: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Convert the whole text (Ctrl+Enter)")
                }
                ActionButton {
                    objectName: "checkButton"
                    action: checkAction
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Analyse the word at the cursor (Ctrl+Shift+K)")
                }
                ActionButton {
                    objectName: "applyButton"
                    action: applyAction
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Replace your text with the preview (Ctrl+Shift+Enter)")
                }
                ActionButton {
                    objectName: "undoButton"
                    action: undoAction
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Undo the last Apply or bulk edit, one step (Ctrl+Z)")
                }
                ActionButton {
                    objectName: "copyButton"
                    action: copyAction
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Copy to the clipboard (Ctrl+Shift+C)")
                }
                ActionButton {
                    objectName: "clearButton"
                    action: clearAction
                    flat: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Remove the text, preview and undo")
                }
            }

            // Notice
            Rectangle {
                objectName: "noticeBar"
                Layout.fillWidth: true
                visible: window.controller.notice !== ""
                implicitHeight: noticeRow.implicitHeight + 12
                radius: window.theme.radius
                color: window.theme.surface
                border.width: 1
                border.color: window.noticeIsError(window.controller.notice) ? window.theme.error : window.theme.accent

                RowLayout {
                    id: noticeRow
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 4
                    spacing: 8
                    PlainLabel {
                        objectName: "noticeText"
                        Layout.fillWidth: true
                        text: window.noticeText(window.controller.notice)
                        color: window.noticeIsError(window.controller.notice) ? window.theme.error : window.theme.foreground
                        Accessible.role: Accessible.AlertMessage
                    }
                    ToolButton {
                        text: qsTr("Dismiss")
                        onClicked: window.controller.dismissNotice()
                    }
                }
            }

            // Panes
            GridLayout {
                Layout.fillWidth: true
                // Prefer a useful editor area without making the enclosing
                // layout's height depend on its own implicit height.
                Layout.preferredHeight: window.wide ? Math.max(240, window.height - 240)
                    : Math.max(260, window.height - 360)
                columns: window.wide ? 2 : 1
                columnSpacing: window.gap
                rowSpacing: window.gap

                // Source
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    Layout.minimumHeight: 120
                    Layout.preferredHeight: 120
                    radius: window.theme.radius
                    color: window.theme.surface
                    border.width: 1
                    border.color: sourceEditor.activeFocus ? window.theme.accent : Qt.rgba(0, 0, 0, 0)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            PlainLabel {
                                Layout.fillWidth: true
                                text: qsTr("Your text")
                                font.bold: true
                                wrapMode: Text.NoWrap
                                elide: Text.ElideRight
                            }
                            PlainLabel {
                                objectName: "lengthCounter"
                                text: qsTr("%1 / %2 characters").arg(window.controller.sourceLength).arg(window.controller.maxInputLength)
                                color: window.theme.muted
                                wrapMode: Text.NoWrap
                            }
                        }
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            TextArea {
                                id: sourceEditor
                                objectName: "sourceEditor"
                                textFormat: TextEdit.PlainText
                                wrapMode: TextEdit.Wrap
                                readOnly: !window.editorReady
                                selectByMouse: true
                                persistentSelection: true
                                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                                font.family: "monospace"
                                color: window.theme.foreground
                                selectionColor: window.theme.accent
                                placeholderText: qsTr("Type or paste text here. It stays in this window and is not saved.")
                                placeholderTextColor: window.theme.muted
                                background: null
                                Accessible.name: qsTr("Your text")
                                onTextChanged: window.pushEdit()
                                // Exact modifier sets only: Ctrl+Alt(+Shift)+Enter
                                // must not Apply by accident. Keypad Enter is allowed.
                                Keys.onPressed: function (event) {
                                    const mods = event.modifiers & ~Qt.KeypadModifier
                                    const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                                    if (event.key === Qt.Key_Z && mods === Qt.ControlModifier) {
                                        window.controller.undo()
                                        event.accepted = true
                                    } else if (enter && mods === Qt.ControlModifier) {
                                        window.controller.preview()
                                        event.accepted = true
                                    } else if (enter && mods === (Qt.ControlModifier | Qt.ShiftModifier)) {
                                        window.controller.apply()
                                        event.accepted = true
                                    }
                                }
                            }
                        }
                    }
                }

                // Preview
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    Layout.minimumHeight: 120
                    Layout.preferredHeight: 120
                    radius: window.theme.radius
                    color: window.theme.surface
                    border.width: 1
                    border.color: window.controller.canApply ? window.theme.accent : Qt.rgba(0, 0, 0, 0)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6
                        PlainLabel {
                            objectName: "previewTitle"
                            Layout.fillWidth: true
                            text: window.previewTitle()
                            font.bold: true
                            wrapMode: Text.NoWrap
                            elide: Text.ElideRight
                        }
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            TextArea {
                                id: previewArea
                                objectName: "previewArea"
                                textFormat: TextEdit.PlainText
                                wrapMode: TextEdit.Wrap
                                readOnly: true
                                selectByMouse: true
                                font.family: "monospace"
                                color: window.theme.foreground
                                selectionColor: window.theme.accent
                                text: window.controller.previewText
                                placeholderText: qsTr("Press Preview to see the converted text. Nothing changes until you press Apply.")
                                placeholderTextColor: window.theme.muted
                                background: null
                                Accessible.name: qsTr("Preview")
                                Accessible.description: qsTr("Read-only preview. Press Apply to use it.")
                            }
                        }
                    }
                }
            }

            // Word check result
            Rectangle {
                objectName: "checkCard"
                Layout.fillWidth: true
                visible: window.controller.checking || window.controller.checkAction !== ""
                implicitHeight: checkColumn.implicitHeight + 16
                radius: window.theme.radius
                color: window.theme.surface

                ColumnLayout {
                    id: checkColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 12
                    spacing: 2
                    PlainLabel {
                        objectName: "checkTitle"
                        Layout.fillWidth: true
                        font.bold: true
                        text: window.controller.checking ? qsTr("Checking the word…") : window.actionTitle(window.controller.checkAction)
                    }
                    PlainLabel {
                        Layout.fillWidth: true
                        visible: !window.controller.checking
                        text: window.reasonText(window.controller.checkReason)
                    }
                    PlainLabel {
                        Layout.fillWidth: true
                        visible: !window.controller.checking
                            && (window.controller.checkAction === "correct" || window.controller.checkAction === "suggest")
                        color: window.theme.muted
                        text: (window.controller.checkSafeAction === "correct"
                            ? qsTr("The conservative automatic profile would also change it.")
                            : qsTr("The conservative automatic profile would leave it unchanged."))
                            + " " + qsTr("Nothing changes until you press Apply.")
                    }
                }
            }

            PlainLabel {
                objectName: "footerNote"
                Layout.fillWidth: true
                color: window.theme.muted
                font.pixelSize: 12
                text: qsTr("Text stays local and is not saved. Clipboard copy, cut and paste require your explicit action.")
            }
        }
    }

    Component.onCompleted: {
        // Both are evaluated; either failure keeps the source read-only.
        const sourceGuarded = editorGuard.configure(sourceEditor.textDocument) === true
        const previewGuarded = editorGuard.configure(previewArea.textDocument) === true
        editorReady = sourceGuarded && previewGuarded
        editorChecked = true
        sourceEditor.forceActiveFocus()
    }
}
