import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    property bool darkMode: true
    property color pageColor: darkMode ? "#101010" : "#ffffff"
    property color textColor: darkMode ? "#f5f1e8" : "#101010"
    property color strongTextColor: darkMode ? "#ffffff" : "#000000"
    property color mutedColor: darkMode ? "#909191" : "#aeb1b5"
    property color accentColor: "#4a90e2"
    property color selectionFill: "#2c4a6b"
    property int containerWidth: 600
    property int containerHeight: 480
    property real textScale: 1

    signal metadataAccepted(string title, string author, string topic)

    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(520, containerWidth - 40)
    x: Math.round((containerWidth - width) / 2)
    y: Math.round((containerHeight - height) / 2)
    padding: 24

    function scaledSize(size) {
        return Math.max(1, Math.round(size * textScale));
    }

    function loadMetadata(meta) {
        titleField.text = meta.title || "";
        authorField.text = meta.author || "";
        topicField.text = meta.topic || "";
        createdLabel.text = meta.created || "-";
        updatedLabel.text = meta.updated || "-";
    }

    function applyChanges() {
        root.close();
        root.metadataAccepted(titleField.text.trim(), authorField.text.trim(), topicField.text.trim());
    }

    onOpened: {
        titleField.forceActiveFocus();
        titleField.selectAll();
    }

    Overlay.modal: Rectangle {
        color: "transparent"
    }

    background: Rectangle {
        color: root.darkMode ? "#000000" : "#ffffff"
        border.color: root.darkMode ? "#222222" : "#d8d8d8"
        border.width: 1
        radius: 0
    }

    contentItem: Item {
        implicitWidth: contentColumn.implicitWidth
        implicitHeight: contentColumn.implicitHeight

        Column {
            id: contentColumn
            width: parent.width
            spacing: root.scaledSize(14)

            RowLayout {
                width: parent.width

                Label {
                    Layout.fillWidth: true
                    text: "Properties"
                    color: root.strongTextColor
                    font.family: "monospace"
                    font.pixelSize: root.scaledSize(16)
                    font.bold: true
                }

                SearchIconButton {
                    iconName: "close"
                    iconColor: root.mutedColor
                    onClicked: root.close()
                }
            }

            Column {
                width: parent.width
                spacing: 4

                Label {
                    text: "TITLE"
                    color: root.mutedColor
                    font.family: "monospace"
                    font.pixelSize: root.scaledSize(11)
                    font.bold: true
                }

                Rectangle {
                    width: parent.width
                    height: root.scaledSize(36)
                    color: root.darkMode ? "#141414" : "#f9f9f9"
                    border.color: titleField.activeFocus ? root.accentColor : (root.darkMode ? "#2e2e2e" : "#d0d0d0")
                    border.width: 1

                    TextInput {
                        id: titleField
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        verticalAlignment: TextInput.AlignVCenter
                        selectByMouse: true
                        color: root.textColor
                        selectionColor: root.selectionFill
                        selectedTextColor: root.strongTextColor
                        font.family: "monospace"
                        font.pixelSize: root.scaledSize(13)
                        clip: true
                        KeyNavigation.tab: authorField
                        KeyNavigation.backtab: topicField
                        Keys.onReturnPressed: function(event) {
                            if (event.modifiers & Qt.ControlModifier) {
                                root.applyChanges();
                            } else {
                                authorField.forceActiveFocus();
                                authorField.selectAll();
                            }
                            event.accepted = true;
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 4

                Label {
                    text: "AUTHOR"
                    color: root.mutedColor
                    font.family: "monospace"
                    font.pixelSize: root.scaledSize(11)
                    font.bold: true
                }

                Rectangle {
                    width: parent.width
                    height: root.scaledSize(36)
                    color: root.darkMode ? "#141414" : "#f9f9f9"
                    border.color: authorField.activeFocus ? root.accentColor : (root.darkMode ? "#2e2e2e" : "#d0d0d0")
                    border.width: 1

                    TextInput {
                        id: authorField
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        verticalAlignment: TextInput.AlignVCenter
                        selectByMouse: true
                        color: root.textColor
                        selectionColor: root.selectionFill
                        selectedTextColor: root.strongTextColor
                        font.family: "monospace"
                        font.pixelSize: root.scaledSize(13)
                        clip: true
                        KeyNavigation.tab: topicField
                        KeyNavigation.backtab: titleField
                        Keys.onReturnPressed: function(event) {
                            if (event.modifiers & Qt.ControlModifier) {
                                root.applyChanges();
                            } else {
                                topicField.forceActiveFocus();
                                topicField.selectAll();
                            }
                            event.accepted = true;
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 4

                Label {
                    text: "TOPIC"
                    color: root.mutedColor
                    font.family: "monospace"
                    font.pixelSize: root.scaledSize(11)
                    font.bold: true
                }

                Rectangle {
                    width: parent.width
                    height: root.scaledSize(36)
                    color: root.darkMode ? "#141414" : "#f9f9f9"
                    border.color: topicField.activeFocus ? root.accentColor : (root.darkMode ? "#2e2e2e" : "#d0d0d0")
                    border.width: 1

                    TextInput {
                        id: topicField
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        verticalAlignment: TextInput.AlignVCenter
                        selectByMouse: true
                        color: root.textColor
                        selectionColor: root.selectionFill
                        selectedTextColor: root.strongTextColor
                        font.family: "monospace"
                        font.pixelSize: root.scaledSize(13)
                        clip: true
                        KeyNavigation.tab: applyButton
                        KeyNavigation.backtab: authorField
                        Keys.onReturnPressed: function(event) {
                            root.applyChanges();
                            event.accepted = true;
                        }
                    }
                }
            }

            RowLayout {
                width: parent.width
                spacing: 20

                Column {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: "CREATED"
                        color: root.mutedColor
                        font.family: "monospace"
                        font.pixelSize: root.scaledSize(10)
                        font.bold: true
                    }

                    Label {
                        id: createdLabel
                        text: "-"
                        color: root.textColor
                        font.family: "monospace"
                        font.pixelSize: root.scaledSize(12)
                    }
                }

                Column {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: "UPDATED"
                        color: root.mutedColor
                        font.family: "monospace"
                        font.pixelSize: root.scaledSize(10)
                        font.bold: true
                    }

                    Label {
                        id: updatedLabel
                        text: "-"
                        color: root.textColor
                        font.family: "monospace"
                        font.pixelSize: root.scaledSize(12)
                    }
                }
            }
        }
    }

    footer: Item {
        implicitHeight: dialogButtons.implicitHeight + 16

        Row {
            id: dialogButtons
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            SquareDialogButton {
                id: cancelButton
                text: "Cancel"
                darkMode: root.darkMode
                textScale: root.textScale
                KeyNavigation.left: applyButton
                KeyNavigation.right: applyButton
                KeyNavigation.tab: applyButton
                KeyNavigation.backtab: topicField
                onClicked: root.close()
            }

            SquareDialogButton {
                id: applyButton
                text: "Apply"
                primary: true
                darkMode: root.darkMode
                textScale: root.textScale
                KeyNavigation.left: cancelButton
                KeyNavigation.right: cancelButton
                KeyNavigation.tab: titleField
                KeyNavigation.backtab: cancelButton
                onClicked: root.applyChanges()
            }
        }
    }
}
