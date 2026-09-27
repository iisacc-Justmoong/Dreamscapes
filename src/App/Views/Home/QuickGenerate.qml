pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "quickGenerate"

    property alias prompt: promptField.text
    property string mediaType: "Image"
    // Retain the request contract for restored drafts and existing callers.
    property string aspectRatio: "1:1"
    property int generationCount: 1
    property bool menusOpenUpward: false
    property real contentInset: LV.Theme.gap10
    property string errorText: ""
    readonly property var platformInputMethod: Qt.inputMethod
    signal generateRequested(string prompt, string mediaType, string aspectRatio, int count)

    implicitWidth: 402
    implicitHeight: composer.implicitHeight + contentInset * 2
        + (notice.visible ? notice.implicitHeight + LV.Theme.gap8 : 0)

    function openMenu(menu, button) {
        const offset = menusOpenUpward
            ? -(menu.height > 0 ? menu.height : menu.implicitHeight) - LV.Theme.gap2
            : button.height + LV.Theme.gap2
        menu.openFor(button, 0, offset)
    }

    function dismissInput() {
        mediaMenu.close()
        platformInputMethod.hide()
    }

    function submit() {
        const trimmedPrompt = prompt.trim()
        if (trimmedPrompt.length === 0) {
            promptField.inputItem.forceActiveFocus()
            return
        }
        dismissInput()
        generateRequested(trimmedPrompt, mediaType, aspectRatio, generationCount)
    }

    Rectangle {
        id: composer
        objectName: "quickGenerateComposer"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: root.contentInset
        implicitHeight: content.implicitHeight + (LV.Theme.gap12 + border.width) * 2
        height: implicitHeight
        radius: LV.Theme.radiusXl
        color: LV.Theme.panelBackground05
        border.width: 1
        border.color: LV.Theme.panelBackground08

        LV.VStack {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: LV.Theme.gap12 + composer.border.width
            spacing: LV.Theme.gap12

            LV.HStack {
                Layout.fillWidth: true
                spacing: LV.Theme.gap8

                LV.LabelMenuButton {
                    id: mediaButton
                    objectName: "mediaTypeButton"
                    Layout.preferredWidth: 76
                    Layout.minimumWidth: 76
                    Layout.preferredHeight: 44
                    text: root.mediaType === "Video" ? qsTr("Video") : qsTr("Image")
                    tone: LV.AbstractButton.Borderless
                    contentItem: Item {
                        implicitWidth: mediaLabel.implicitWidth + 18
                        implicitHeight: 18
                        LV.Label {
                            id: mediaLabel
                            height: LV.Theme.textBodyLineHeight
                            anchors.verticalCenter: parent.verticalCenter
                            text: mediaButton.text
                            style: body
                            color: mediaButton.textColor
                        }
                        Image {
                            objectName: "mediaTypeChevron"
                            x: mediaLabel.implicitWidth
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18
                            height: 18
                            source: Qt.resolvedUrl("Assets/media-chevron.svg")
                            sourceSize: Qt.size(18 * Screen.devicePixelRatio, 18 * Screen.devicePixelRatio)
                        }
                    }
                    Accessible.name: qsTr("Media type: %1").arg(text)
                    onClicked: root.openMenu(mediaMenu, mediaButton)
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 20
                    Layout.alignment: Qt.AlignVCenter
                    color: LV.Theme.panelBackground08
                }

                LV.InputField {
                    id: promptField
                    objectName: "promptField"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: 44
                    placeholderText: composer.width < 480
                        ? qsTr("Describe your idea") : qsTr("Describe what you want to generate")
                    style: inlineStyle
                    clearButtonVisible: false
                    glassEnabled: false
                    backgroundComponent: Item {}
                    placeholderColor: LV.Theme.titleHeaderColor
                    Accessible.name: qsTr("Generation prompt")
                    onAccepted: root.submit()
                }
            }

            LV.HStack {
                Layout.fillWidth: true
                spacing: 0
                Item { Layout.fillWidth: true }
                LV.LabelButton {
                    objectName: "generateButton"
                    Layout.preferredWidth: 104
                    Layout.minimumWidth: 104
                    Layout.preferredHeight: 44
                    text: qsTr("Generate")
                    tone: LV.AbstractButton.Primary
                    shapeStyle: shapeCylinder
                    onClicked: root.submit()
                }
            }
        }
    }

    LV.Label {
        id: notice
        objectName: "quickGenerateNotice"
        anchors.top: composer.bottom
        anchors.topMargin: LV.Theme.gap8
        anchors.left: composer.left
        anchors.right: composer.right
        visible: text.length > 0
        text: root.errorText
        color: LV.Theme.descriptionColor
        wrapMode: Text.Wrap
        sizeToContentHeight: true
        Accessible.name: text
    }

    LV.ContextMenu {
        id: mediaMenu
        objectName: "mediaTypeMenu"
        showIconSlot: false
        itemWidth: Math.max(0, Math.min(LV.Theme.scaleMetric(145),
            root.width - leftPadding - rightPadding - edgeMargin * 2))
        items: [qsTr("Image"), qsTr("Video")]
        selectedIndex: root.mediaType === "Video" ? 1 : 0
        onItemTriggered: function(index, entry) {
            root.mediaType = index === 1 ? "Video" : "Image"
        }
    }
}
