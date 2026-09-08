import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "quickGenerate"

    property alias prompt: promptField.text
    readonly property string mediaType: "Image"
    property string aspectRatio: "1:1"
    property bool menusOpenUpward: false
    readonly property var platformInputMethod: Qt.inputMethod
    signal generateRequested(string prompt, string mediaType, string aspectRatio)

    implicitWidth: 402
    implicitHeight: content.implicitHeight + LV.Theme.gap10 * 2

    function openMenu(menu, button) {
        const offset = menusOpenUpward
            ? -Math.max(menu.implicitHeight, menu.height) - LV.Theme.gap2
            : button.height + LV.Theme.gap2
        menu.openFor(button, 0, offset)
    }

    function dismissInput() {
        mediaMenu.close()
        ratioMenu.close()
        platformInputMethod.hide()
    }

    function submit() {
        const trimmedPrompt = prompt.trim()
        if (trimmedPrompt.length === 0) {
            promptField.inputItem.forceActiveFocus()
            return
        }
        dismissInput()
        generateRequested(trimmedPrompt, mediaType, aspectRatio)
    }

    LV.VStack {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: LV.Theme.gap10
        height: implicitHeight
        spacing: LV.Theme.gap8
        alignment: Qt.AlignLeft

        LV.InputField {
            id: promptField
            objectName: "promptField"
            Layout.fillWidth: true
            placeholderText: qsTr("Prompt")
            style: roundedStyle
            clearButtonVisible: false
            Accessible.name: qsTr("Prompt")
            onAccepted: root.submit()
        }

        LV.HStack {
            id: actions
            Layout.fillWidth: true
            spacing: 0

            // Preserve natural button widths when the shared layout narrows.
            readonly property real actionSpacing: Math.min(LV.Theme.gap8, Math.max(0,
                (width - mediaButton.implicitWidth - ratioButton.implicitWidth
                 - generateButton.implicitWidth) / 2))

            LV.HStack {
                Layout.minimumWidth: implicitWidth
                spacing: actions.actionSpacing

                LV.LabelMenuButton {
                    id: mediaButton
                    objectName: "mediaTypeButton"
                    text: qsTr("Image")
                    tone: LV.AbstractButton.Default
                    Accessible.name: qsTr("Media type: Image")
                    onClicked: root.openMenu(mediaMenu, mediaButton)
                }

                LV.LabelMenuButton {
                    id: ratioButton
                    objectName: "aspectRatioButton"
                    text: root.aspectRatio
                    tone: LV.AbstractButton.Default
                    Accessible.name: qsTr("Aspect ratio: %1").arg(root.aspectRatio)
                    onClicked: root.openMenu(ratioMenu, ratioButton)
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.minimumWidth: actions.actionSpacing
            }

            LV.LabelButton {
                id: generateButton
                objectName: "generateButton"
                Layout.minimumWidth: implicitWidth
                text: qsTr("Generate")
                tone: LV.AbstractButton.Primary
                onClicked: root.submit()
            }
        }
    }

    LV.ContextMenu {
        id: mediaMenu
        objectName: "mediaTypeMenu"
        showIconSlot: false
        itemWidth: Math.max(0, Math.min(LV.Theme.scaleMetric(145),
            root.width - leftPadding - rightPadding - edgeMargin * 2))
        selectedIndex: 0
        items: [qsTr("Image")]
    }

    LV.ContextMenu {
        id: ratioMenu
        objectName: "aspectRatioMenu"
        showIconSlot: false
        itemWidth: Math.max(0, Math.min(LV.Theme.scaleMetric(145),
            root.width - leftPadding - rightPadding - edgeMargin * 2))
        items: ["1:1", "4:3", "3:4", "16:9", "9:16"]
        selectedIndex: items.indexOf(root.aspectRatio)
        onItemTriggered: function(index, entry) {
            root.aspectRatio = String(entry)
        }
    }
}
