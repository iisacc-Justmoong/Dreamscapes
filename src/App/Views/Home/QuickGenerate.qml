pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "quickGenerate"

    property string errorText: ""
    property alias prompt: promptField.text
    property string mediaType: "Image"
    property string aspectRatio: "1:1"
    property int generationCount: 1
    property int videoDuration: 5
    property int videoFps: 24
    property var videoModels: []
    property string selectedVideoModel: ""
    signal videoModelSelected(string modelId)
    property bool submitting: false
    readonly property var generationCounts: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        15, 20, 25, 30, 40, 50, 100, 200, 500, 1000]
    property bool canvasEnabled: false
    property alias homePaint: homePaint
    readonly property bool hasCanvasInputs: homePaint.hasInputs
    function addAttachment(source) {
        return homePaint.addAttachment(source)
    }
    function generationParameters(model) {
        return homePaint.generationParameters(prompt.trim(), model, aspectRatio, generationCount)
    }
    property bool menusOpenUpward: false
    property real contentInset: LV.Theme.gap10
    readonly property var platformInputMethod: Qt.inputMethod
    signal generateRequested(string prompt, string mediaType, string aspectRatio, int count)

    implicitWidth: 402
    implicitHeight: content.implicitHeight + contentInset * 2
        + (notice.visible ? notice.implicitHeight + LV.Theme.gap8 : 0)

    function openMenu(menu, button) {
        const offset = menusOpenUpward
            ? -(menu.height > 0 ? menu.height : menu.implicitHeight) - LV.Theme.gap2
            : button.height + LV.Theme.gap2
        menu.openFor(button, 0, offset)
    }

    function dismissInput() {
        homePaint.dismissPopovers()
        mediaMenu.close()
        ratioMenu.close()
        countMenu.close()
        videoModelMenu.close()
        durationMenu.close()
        fpsMenu.close()
        platformInputMethod.hide()
    }

    function submit() {
        if (submitting) return
        const trimmedPrompt = prompt.trim()
        if (trimmedPrompt.length === 0) {
            promptField.inputItem.forceActiveFocus()
            return
        }
        dismissInput()
        submitting = true
        try {
            generateRequested(trimmedPrompt, mediaType, aspectRatio, generationCount)
        } finally {
            submitting = false
        }
    }

    function focusPrompt() {
        promptField.inputItem.forceActiveFocus()
    }

    LV.VStack {
        id: content
        objectName: "quickGenerateContent"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: root.contentInset
        height: implicitHeight
        spacing: root.canvasEnabled ? LV.Theme.gap16 : LV.Theme.gap8
        alignment: Qt.AlignLeft

        HomePaintCanvas {
            id: homePaint
            // Keep reference-image state; cards render in the external attachment slot.
            visible: false
            Layout.fillWidth: true
            aspectRatio: root.aspectRatio
            attachmentHost: attachmentsSlot
        }
        PromptField {
            id: promptField
            objectName: "promptField"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            placeholderColor: LV.Theme.titleHeaderColor
            placeholderText: qsTr("Prompt")
            style: roundedStyle
            clearButtonVisible: false
            Accessible.name: qsTr("Prompt")
            onAccepted: root.submit()
        }

        Item {
            id: attachmentsSlot
            objectName: "homeAttachmentsSlot"
            visible: root.canvasEnabled && homePaint.attachmentsHeight > 0
            Layout.fillWidth: true
            Layout.preferredHeight: homePaint.attachmentsHeight
        }

        LV.HStack {
            id: actions
            objectName: "quickGenerateActions"
            Layout.fillWidth: true
            Layout.preferredHeight: root.canvasEnabled ? 28 : 22
            spacing: 0

            // Preserve natural button widths when the shared layout narrows.
            readonly property real actionSpacing: Math.min(LV.Theme.gap8, Math.max(0,
                (width - mediaButton.implicitWidth - ratioButton.implicitWidth
                 - countButton.implicitWidth - generateButton.implicitWidth
                 - (attachButton.visible ? attachButton.implicitWidth : 0)) / (root.canvasEnabled ? 4 : 3)))

            LV.HStack {
                Layout.minimumWidth: implicitWidth
                spacing: actions.actionSpacing

                LV.IconButton {
                    id: attachButton
                    objectName: "homeAttachButton"
                    visible: root.canvasEnabled
                    implicitWidth: 22; implicitHeight: 22; iconSize: 18
                    iconName: "generaladd"
                    tone: LV.AbstractButton.Default
                    Accessible.name: qsTr("Attach images")
                    onClicked: attachmentDialog.open()
                }
                ChoiceButton {
                    id: mediaButton
                    objectName: "mediaTypeButton"
                    text: root.mediaType === "Video" ? qsTr("Video") : qsTr("Image")
                    tone: LV.AbstractButton.Default
                    Accessible.name: qsTr("Media type: %1").arg(text)
                    onClicked: root.openMenu(mediaMenu, mediaButton)
                }

                ChoiceButton {
                    id: ratioButton
                    objectName: "aspectRatioButton"
                    text: root.aspectRatio
                    tone: LV.AbstractButton.Default
                    Accessible.name: qsTr("Aspect ratio: %1").arg(root.aspectRatio)
                    onClicked: root.openMenu(ratioMenu, ratioButton)
                }

                ChoiceButton {
                    id: countButton
                    objectName: "generationCountButton"
                    text: root.canvasEnabled && root.width >= 520
                        ? String(root.generationCount) + (root.mediaType === "Video"
                            ? root.generationCount === 1 ? qsTr(" video") : qsTr(" videos")
                            : root.generationCount === 1 ? qsTr(" image") : qsTr(" images"))
                        : String(root.generationCount)
                    tone: LV.AbstractButton.Default
                    Accessible.name: (root.mediaType === "Video" ? qsTr("Video count: %1") : qsTr("Image count: %1")).arg(root.generationCount)
                    onClicked: root.openMenu(countMenu, countButton)
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.minimumWidth: actions.actionSpacing
            }

            LV.PushButton {
                id: generateButton
                objectName: "generateButton"
                Layout.minimumWidth: implicitWidth
                text: qsTr("Generate")
                iconMode: root.canvasEnabled && root.width < 320
                iconSource: Qt.resolvedUrl("../Result/Assets/right.svg")
                iconSize: 18
                Accessible.name: qsTr("Generate")
                tone: LV.AbstractButton.Primary
                onClicked: root.submit()
            }
        }

        LV.VStack {
            objectName: "videoGenerationOptions"
            visible: root.mediaType === "Video"
            Layout.fillWidth: true
            height: implicitHeight
            spacing: LV.Theme.gap8
            LV.LabelMenuButton {
                id: videoModelButton
                objectName: "videoModelButton"
                tone: LV.AbstractButton.Default
                Layout.fillWidth: true
                text: root.selectedVideoModel || qsTr("Add an LTX video model in Society")
                enabled: root.videoModels.length > 0
                onClicked: root.openMenu(videoModelMenu, videoModelButton)
            }
            LV.HStack {
                Layout.fillWidth: true
                spacing: LV.Theme.gap8
                ChoiceButton {
                    id: durationButton
                    objectName: "videoDurationButton"
                    tone: LV.AbstractButton.Default
                    text: qsTr("%1 s").arg(root.videoDuration)
                    Accessible.name: qsTr("Video duration: %1 seconds").arg(root.videoDuration)
                    onClicked: root.openMenu(durationMenu,durationButton)
                }
                ChoiceButton {
                    id: fpsButton
                    objectName: "videoFpsButton"
                    tone: LV.AbstractButton.Default
                    text: qsTr("%1 FPS").arg(root.videoFps)
                    Accessible.name: qsTr("Video frame rate: %1 FPS").arg(root.videoFps)
                    onClicked: root.openMenu(fpsMenu,fpsButton)
                }
                LV.Label { text: qsTr("MP4 · LTX"); style: caption }
            }
        }
    }

    LV.ContextMenu {
        id: videoModelMenu
        objectName: "videoModelMenu"
        showIconSlot: false
        items: root.videoModels.map(function(model) { return model.id })
        selectedIndex: items.indexOf(root.selectedVideoModel)
        onItemTriggered: function(index,entry) { root.videoModelSelected(String(entry)) }
    }
    LV.ContextMenu {
        id: durationMenu
        objectName: "videoDurationMenu"
        showIconSlot: false
        items: [qsTr("1 s"),qsTr("3 s"),qsTr("5 s"),qsTr("10 s")]
        selectedIndex: [1,3,5,10].indexOf(root.videoDuration)
        onItemTriggered: function(index) { root.videoDuration = [1,3,5,10][index] }
    }
    LV.ContextMenu {
        id: fpsMenu
        objectName: "videoFpsMenu"
        showIconSlot: false
        items: [qsTr("12 FPS"),qsTr("24 FPS"),qsTr("30 FPS")]
        selectedIndex: [12,24,30].indexOf(root.videoFps)
        onItemTriggered: function(index) { root.videoFps = [12,24,30][index] }
    }

    FileDialog {
        id: attachmentDialog
        title: qsTr("Attach reference images")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff *.heic)")]
        onAccepted: { for (const source of selectedFiles) root.addAttachment(source) }
    }

    // Figma 203:6930 uses the same 18px asset in all three dropdown slots.
    component ChoiceButton: LV.LabelMenuButton {
        id: choice
        contentItem: Item {
            implicitWidth: Math.ceil(choiceLabel.implicitWidth) + 18
            implicitHeight: 18
            LV.Label {
                id: choiceLabel
                height: LV.Theme.textBodyLineHeight
                anchors.verticalCenter: parent.verticalCenter
                text: choice.text
                style: body
                color: choice.textColor
            }
            Image {
                objectName: "quickGenerateChevron"
                x: Math.ceil(choiceLabel.implicitWidth)
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                source: Qt.resolvedUrl("Assets/media-chevron.svg")
                sourceSize: Qt.size(18 * Screen.devicePixelRatio, 18 * Screen.devicePixelRatio)
            }
        }
    }

    LV.Label {
        id: notice
        objectName: "quickGenerateNotice"
        anchors.top: content.bottom
        anchors.topMargin: LV.Theme.gap8
        anchors.left: content.left
        anchors.right: content.right
        visible: text.length > 0
        text: root.errorText.length > 0 ? root.errorText : root.canvasEnabled ? homePaint.canvas.inputError : ""
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
        selectedIndex: root.mediaType === "Video" ? 1 : 0
        items: [qsTr("Image"), qsTr("Video")]
        onItemTriggered: function(index, entry) { root.mediaType = index === 1 ? "Video" : "Image" }
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
