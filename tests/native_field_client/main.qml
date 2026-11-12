import QtQuick

// Fixed trusted resource. Two real fields; no user QML is ever loaded.
Window {
    width: 400
    height: 200
    visible: true
    title: "native-field-probe"

    Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        TextEdit {
            objectName: "normalField"
            width: parent.width
            height: 60
            textFormat: TextEdit.PlainText
            // Ordinary field: not sensitive, predictive allowed.
        }

        TextInput {
            objectName: "secretField"
            width: parent.width
            height: 30
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
        }
    }
}
