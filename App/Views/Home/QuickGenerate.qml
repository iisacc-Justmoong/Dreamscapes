pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "quickGenerate"

    property alias prompt: promptField.text
    readonly property string mediaType: "Image"
    property string aspectRatio: "1:1"
    property int generationCount: 1
    readonly property var generationCounts: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        15, 20, 25, 30, 40, 50, 100, 200, 500, 1000]
    property bool menusOpenUpward: false
    readonly property var platformInputMethod: Qt.inputMethod
    signal generateRequested(string prompt, string mediaType, string aspectRatio, int count)

    implicitWidth: 402
    implicitHeight: content.implicitHeight + LV.Theme.gap10 * 2

    function openMenu(menu, button) {
        const offset = menusOpenUpward
            ? -(menu.height > 0 ? menu.height : menu.implicitHeight) - LV.Theme.gap2
            : button.height + LV.Theme.gap2
        menu.openFor(button, 0, offset)
    }

    function dismissInput() {
        mediaMenu.close()
        ratioMenu.close()
        countMenu.close()
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
                 - countButton.implicitWidth - generateButton.implicitWidth) / 3))

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

                LV.LabelMenuButton {
                    id: countButton
                    objectName: "generationCountButton"
                    text: String(root.generationCount)
                    tone: LV.AbstractButton.Default
                    Accessible.name: qsTr("Image count: %1").arg(root.generationCount)
                    onClicked: root.openMenu(countMenu, countButton)
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

    LV.ContextMenu {
        id: countMenu
        objectName: "generationCountMenu"
        showIconSlot: false
        itemWidth: Math.max(0, Math.min(LV.Theme.scaleMetric(145),
            root.width - leftPadding - rightPadding - edgeMargin * 2))
        items: root.generationCounts.map(function(count) { return String(count) })
        selectedIndex: root.generationCounts.indexOf(root.generationCount)
        implicitHeight: Math.min(countList.contentHeight + topPadding + bottomPadding,
            LV.Theme.scaleMetric(320), parent ? Math.max(0, parent.height - edgeMargin * 2) : LV.Theme.scaleMetric(320))
        onItemTriggered: function(index, entry) { root.generationCount = Number(entry) }
        onOpened: {
            countList.currentIndex = selectedIndex
            countList.positionViewAtIndex(selectedIndex, ListView.Contain)
            countList.forceActiveFocus()
        }

        contentItem: ListView {
            id: countList
            objectName: "generationCountList"
            implicitHeight: contentHeight
            spacing: countMenu.itemSpacing
            model: countMenu.items
            currentIndex: 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            Keys.onReturnPressed: countMenu.triggerEntry(currentIndex)
            Keys.onEnterPressed: countMenu.triggerEntry(currentIndex)
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Home) {
                    currentIndex = 0
                    positionViewAtBeginning()
                    event.accepted = true
                } else if (event.key === Qt.Key_End) {
                    currentIndex = count - 1
                    positionViewAtEnd()
                    event.accepted = true
                }
            }
            delegate: LV.MenuItem {
                required property int index
                required property var modelData
                objectName: "generationCountOption" + index
                width: countList.width
                itemWidth: countMenu.itemWidth
                label: String(modelData)
                keyVisible: false
                showIconSlot: false
                hasChildItems: false
                state: index === countList.currentIndex ? selectedState : defaultState
                Accessible.name: qsTr("%1 images").arg(modelData)
                onClicked: countMenu.triggerEntry(index)
            }
        }
    }
}
