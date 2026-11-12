import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    property bool darkMode: true
    property color textColor: darkMode ? "#f5f1e8" : "#101010"
    property color strongTextColor: darkMode ? "#ffffff" : "#000000"
    property color mutedColor: darkMode ? "#909191" : "#6c7075"
    property color accentColor: "#4a90e2"
    property color selectionFill: darkMode ? "#1f2a36" : "#e8f1fb"
    property int containerWidth: 800
    property int containerHeight: 600
    property real textScale: 1

    property int diagStart: 0
    property int diagEnd: 0
    property string diagMessage: ""
    property string diagWord: ""
    property string diagLanguage: ""
    property bool diagIsSpelling: true
    property var suggestions: []
    property int selectedIndex: 0
    property int anchorX: 0
    property int anchorY: 0
    property int anchorLineHeight: 24

    signal suggestionChosen(int start, int end, string replacement)
    signal addToDictionaryRequested(string word, string language)

    readonly property int actionCount: suggestions.length + (canAddToDictionary ? 1 : 0)
    readonly property bool canAddToDictionary: diagIsSpelling && diagWord.length > 0

    function scaledSize(size) {
        return Math.max(1, Math.round(size * textScale));
    }

    function loadDiagnostic(diag) {
        diagStart = diag.start || 0;
        diagEnd = diag.end || 0;
        diagMessage = diag.message || "";
        diagWord = diag.word || "";
        diagLanguage = diag.language || "";
        diagIsSpelling = diag.isSpelling !== undefined ? diag.isSpelling : true;
        suggestions = diag.suggestions || [];
        selectedIndex = actionCount > 0 ? 0 : -1;
    }

    function updatePosition() {
        var margin = 12;
        var px = Math.max(margin, Math.min(anchorX, containerWidth - width - margin));
        var belowY = anchorY + anchorLineHeight + 4;
        var aboveY = anchorY - height - 4;
        var py = (belowY + height <= containerHeight - margin || aboveY < margin)
            ? Math.min(belowY, containerHeight - height - margin)
            : aboveY;
        x = Math.round(px);
        y = Math.round(Math.max(margin, py));
    }

    function openForDiagnostic(px, py, lineHeight, diag) {
        anchorX = px;
        anchorY = py;
        anchorLineHeight = lineHeight;
        loadDiagnostic(diag);
        open();
        updatePosition();
        contentFocusItem.forceActiveFocus();
    }

    function activateIndex(idx) {
        if (idx < 0 || idx >= actionCount)
            return;
        if (idx < suggestions.length) {
            var replacement = suggestions[idx];
            close();
            suggestionChosen(diagStart, diagEnd, replacement);
        } else if (canAddToDictionary) {
            var word = diagWord;
            var lang = diagLanguage;
            close();
            addToDictionaryRequested(word, lang);
        }
    }

    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(scaledSize(360), Math.max(220, containerWidth - 24))
    padding: scaledSize(12)

    onImplicitHeightChanged: {
        if (opened)
            updatePosition();
    }

    background: Rectangle {
        color: root.darkMode ? "#121212" : "#ffffff"
        border.color: root.darkMode ? "#333333" : "#d8d8d8"
        border.width: 1
        radius: 0
    }

    contentItem: ColumnLayout {
        id: contentFocusItem
        width: root.availableWidth
        spacing: root.scaledSize(8)
        focus: true

        Keys.onUpPressed: function(event) {
            if (root.actionCount > 0) {
                root.selectedIndex = (root.selectedIndex - 1 + root.actionCount) % root.actionCount;
            }
            event.accepted = true;
        }

        Keys.onDownPressed: function(event) {
            if (root.actionCount > 0) {
                root.selectedIndex = (root.selectedIndex + 1) % root.actionCount;
            }
            event.accepted = true;
        }

        Keys.onReturnPressed: function(event) {
            root.activateIndex(root.selectedIndex);
            event.accepted = true;
        }

        Keys.onEnterPressed: function(event) {
            root.activateIndex(root.selectedIndex);
            event.accepted = true;
        }

        Keys.onPressed: function(event) {
            if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9 && !event.modifiers) {
                var numIdx = event.key - Qt.Key_1;
                if (numIdx < root.suggestions.length) {
                    root.activateIndex(numIdx);
                    event.accepted = true;
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.diagIsSpelling ? "SPELLING" : "GRAMMAR"
            color: root.diagIsSpelling
                ? (root.darkMode ? "#e05d5d" : "#d32f2f")
                : (root.darkMode ? "#e0a84b" : "#c77c02")
            font.family: systemTheme.monospaceFamily
            font.pixelSize: root.scaledSize(10)
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            text: root.diagMessage
            color: root.mutedColor
            wrapMode: Text.Wrap
            font.family: systemTheme.monospaceFamily
            font.pixelSize: root.scaledSize(12)
            visible: text.length > 0
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: root.darkMode ? "#262626" : "#e5e5e5"
        }

        Label {
            Layout.fillWidth: true
            text: "No suggestions"
            color: root.mutedColor
            font.family: systemTheme.monospaceFamily
            font.pixelSize: root.scaledSize(12)
            visible: root.suggestions.length === 0
        }

        Column {
            Layout.fillWidth: true
            spacing: 2
            visible: root.suggestions.length > 0

            Repeater {
                model: root.suggestions

                Rectangle {
                    required property string modelData
                    required property int index

                    width: parent.width
                    height: suggestionRow.implicitHeight + root.scaledSize(10)
                    color: root.selectedIndex === index ? root.selectionFill : "transparent"

                    RowLayout {
                        id: suggestionRow
                        anchors.fill: parent
                        anchors.leftMargin: root.scaledSize(8)
                        anchors.rightMargin: root.scaledSize(8)
                        spacing: root.scaledSize(8)

                        Label {
                            text: (index < 9) ? (index + 1) + "." : " "
                            color: root.mutedColor
                            font.family: systemTheme.monospaceFamily
                            font.pixelSize: root.scaledSize(12)
                        }

                        Label {
                            Layout.fillWidth: true
                            text: modelData
                            color: root.strongTextColor
                            elide: Text.ElideRight
                            font.family: systemTheme.monospaceFamily
                            font.pixelSize: root.scaledSize(13)
                            font.bold: root.selectedIndex === index
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: root.selectedIndex = index
                        onClicked: root.activateIndex(index)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: root.darkMode ? "#262626" : "#e5e5e5"
            visible: root.canAddToDictionary
        }

        Rectangle {
            Layout.fillWidth: true
            height: addLabel.implicitHeight + root.scaledSize(10)
            visible: root.canAddToDictionary
            color: root.selectedIndex === root.suggestions.length ? root.selectionFill : "transparent"

            Label {
                id: addLabel
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: root.scaledSize(8)
                anchors.rightMargin: root.scaledSize(8)
                text: "Add \"" + root.diagWord + "\" to dictionary"
                elide: Text.ElideMiddle
                color: root.textColor
                font.family: systemTheme.monospaceFamily
                font.pixelSize: root.scaledSize(12)
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onEntered: root.selectedIndex = root.suggestions.length
                onClicked: root.activateIndex(root.suggestions.length)
            }
        }
    }
}
