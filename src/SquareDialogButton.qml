import QtQuick
import QtQuick.Controls

Button {
    id: control

    property bool primary: false
    property bool darkMode: true
    property color labelColor: darkMode ? "#ffffff" : "#101010"
    property real textScale: 1

    leftPadding: 16
    rightPadding: 16
    topPadding: 7
    bottomPadding: 7

    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    contentItem: Label {
        text: control.text
        color: control.labelColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: "monospace"
        font.pixelSize: Math.round(12 * control.textScale)
    }

    background: Rectangle {
        implicitWidth: 88
        implicitHeight: 34
        color: control.down 
            ? (control.darkMode ? "#2a2a2a" : "#e0e0e0")
            : control.primary
                ? (control.darkMode ? "#000000" : "#ffffff")
                : (control.darkMode ? "#1a1a1a" : "#f5f5f5")
        border.color: control.activeFocus
            ? (control.darkMode ? "#ffffff" : "#101010")
            : (control.darkMode ? "#333333" : "#d8d8d8")
    }
}
