pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

Rectangle {
    id: root
    objectName: "desktopGenerationComposer"
    required property var composer
    property var generation: null
    property var parameterStore: homeDraft
    property alias shortSide: nativeComposer.shortSide
    property alias seed: nativeComposer.seed
    property alias negativePrompt: nativeComposer.negativePrompt
    property alias appliedNegativePrompt: nativeComposer.appliedNegativePrompt
    property alias loadedPreset: nativeComposer.loadedPreset
    property alias errorText: nativeComposer.errorText
    readonly property bool wide: width >= 1000
    property alias selectedPresetName: nativeComposer.selectedPresetName
    readonly property alias video: nativeComposer.video
    readonly property alias modelChoices: nativeComposer.modelChoices
    readonly property alias selectedModel: nativeComposer.selectedModel
    readonly property alias selectedModelName: nativeComposer.selectedModelName
    readonly property alias outputSize: nativeComposer.outputSize
    readonly property alias usesExplicitParameters: nativeComposer.usesExplicitParameters
    NativeGenerationComposer {
        id: nativeComposer
        objectName: "desktopNativeComposer"
        sharedModel: root.composer.nativeModel
        generation: root.generation
        canvas: root.composer.homePaint.canvas
        parameterStore: root.parameterStore
        onMediaActionRequested: action => root.composer.mediaActionRequested(action)
    }
    readonly property var attachments: composer.homePaint.canvas.attachments
    color: "transparent"
    radius: 0
    border.color: LV.Theme.panelBackground10
    border.width: 0
    implicitHeight: layout.implicitHeight + LV.Theme.gap12 * 2

    AdvancedImageParameters { id: homeDraft; objectName: "homeParameterStore" }
    function sizeFor(shorter) { return nativeComposer.sizeFor(shorter) }
    function applyNegativeDefault() { nativeComposer.applyNegativeDefault() }
    function parameters(model, includeInputs) { return nativeComposer.parameters(model, includeInputs) }
    function savePreset(name) { return nativeComposer.savePreset(name) }
    function loadPreset(name) { return nativeComposer.loadPreset(name) }
    function focusPrompt() { promptEditor.editorItem.forceActiveFocus() }
    function dismissInput() {
        modelMenu.close(); resolutionMenu.close(); loadMenu.close()
        seedPopover.close(); negativePopover.close(); presetPopover.close()
    }
    function openPopup(popup, button) {
        const p = button.mapToItem(popup.parent, 0, button.height + LV.Theme.gap2)
        popup.x = Math.max(LV.Theme.gap8, Math.min(p.x, popup.parent.width - popup.width - LV.Theme.gap8))
        popup.y = Math.max(LV.Theme.gap8, Math.min(p.y, popup.parent.height - popup.height - LV.Theme.gap8))
        popup.open()
    }

    // Generation prompts are plain text. Keep LVRS geometry and interaction,
    // and avoid feeding RichText's serialized HTML back into the shared draft.
    component PromptEditor: LV.TextEditor {
        id: field
        property bool compact: false
        fieldMinHeight: compact ? LV.Theme.controlHeightSm : LV.Theme.controlHeightMd * 4
        editorHeight: compact ? Math.max(fieldMinHeight, Math.min(160, Math.ceil(contentHeight) + insetVertical * 2)) : fieldMinHeight
        insetVertical: compact ? LV.Theme.gap2 : LV.Theme.gap8
        property string value: ""
        property bool initialized: false
        readonly property alias inputItem: field.editorItem
        signal edited(string value)
        filePath: ""
        onValueChanged: if (initialized && text !== value) text = value
        onTextEdited: function(value) {
            if (initialized && value !== field.value) field.edited(value)
        }
        Component.onCompleted: {
            editorItem.textFormat = TextEdit.PlainText
            text = value
            initialized = true
        }
        function ensureCaretVisible() {
            if (!editorItem.activeFocus) return
            let ancestor = editorItem.parent
            while (ancestor && !(ancestor instanceof Flickable)) ancestor = ancestor.parent
            const viewport = ancestor as Flickable
            if (!viewport) return
            const cursor = editorItem.cursorRectangle
            const point = editorItem.mapToItem(viewport.contentItem, cursor.x, cursor.y)
            const target = point.y < viewport.contentY ? point.y
                : point.y + cursor.height > viewport.contentY + viewport.height ? point.y + cursor.height - viewport.height : viewport.contentY
            viewport.contentY = Math.max(0, Math.min(target, viewport.contentHeight - viewport.height))
        }
        Connections {
            target: field.editorItem
            function onCursorRectangleChanged() { Qt.callLater(field.ensureCaretVisible) }
            function onActiveFocusChanged() { Qt.callLater(field.ensureCaretVisible) }
        }
    }

    component Setting: LV.VStack {
        id: setting
        property string label: ""
        property string value: ""
        property string controlName: ""
        property bool controlEnabled: true
        signal activated(Item control)
        spacing: LV.Theme.gap4
        alignment: Qt.AlignLeft
        LV.Label { text: setting.label; style: caption; color: LV.Theme.descriptionColor }
        LV.LabelMenuButton {
            objectName: root.composer.desktopLayout ? setting.controlName : "inactiveDesktop" + setting.controlName
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: setting.value
            enabled: setting.controlEnabled
            tone: LV.AbstractButton.Default
            onClicked: setting.activated(this)
        }
    }

    function chooseMedia(type) { nativeComposer.chooseMedia(type) }
    LV.VStack { alignment: Qt.AlignLeft;
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: LV.Theme.gap12
        spacing: LV.Theme.gap8
        GridLayout {
            Layout.fillWidth: true
            columns: root.wide ? (root.composer.prompt.length ? 7 : 6) : root.width < 400 ? 1 : 2
            columnSpacing: LV.Theme.gap8
            rowSpacing: LV.Theme.gap8
            LV.LabelMenuButton { tone: LV.AbstractButton.Default;
                id: mediaButton
                objectName: "homeMediaButton"
                text: root.composer.mediaType
                Layout.preferredWidth: 88
                onClicked: root.composer.openMenu(homeMediaMenu, this)
            }
            PromptEditor {
                id: promptEditor
                objectName: root.composer.desktopLayout ? "promptField" : "inactiveDesktopPromptField"
                compact: true
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                value: root.composer.prompt
                placeholderText: qsTr("Describe what to create…")
                Accessible.name: qsTr("Prompt")
                onEdited: function(value) { root.composer.prompt = value }
            }
            LV.IconButton { tone: LV.AbstractButton.Default;
                objectName: "homeClearPrompt"
                visible: root.composer.prompt.length > 0
                iconName: "generalremove"
                Accessible.name: qsTr("Clear prompt")
                onClicked: root.composer.prompt = ""
            }
            LV.IconButton { tone: LV.AbstractButton.Default;
                objectName: root.composer.desktopLayout ? "homeAttachButton" : "inactiveDesktopAttachButton"
                iconName: "generaladd"
                Accessible.name: qsTr("Attach reference images")
                onClicked: root.composer.chooseAttachments()
            }
            LV.LabelButton { tone: LV.AbstractButton.Default; objectName: "homePaintButton"; text: qsTr("Paint canvas"); onClicked: root.composer.paintRequested() }
            LV.LabelMenuButton { tone: LV.AbstractButton.Default;
                objectName: "homeNegativePromptButton"
                text: qsTr("Negative prompt")
                onClicked: root.openPopup(negativePopover, this)
            }
            LV.LabelButton {
                objectName: root.composer.desktopLayout ? "generateButton" : "inactiveDesktopGenerateButton"
                text: root.video
                    ? (root.composer.generationCount === 1 ? qsTr("Generate 1 video") : qsTr("Generate %1 videos").arg(root.composer.generationCount))
                    : (root.composer.generationCount === 1 ? qsTr("Generate 1 image") : qsTr("Generate %1 images").arg(root.composer.generationCount))
                tone: LV.AbstractButton.Primary
                enabled: !root.composer.submitting && !root.composer.generationPending
                onClicked: root.composer.submit()
            }
        }
        GridLayout {
            objectName: "homeGenerationSettings"
            Layout.fillWidth: true
            columns: root.wide ? 7 : root.width >= 600 ? 3 : 2
            columnSpacing: LV.Theme.gap8
            rowSpacing: LV.Theme.gap8
            Setting { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 327; label: qsTr("Model"); controlName: "homeModelButton"; value: root.selectedModelName || qsTr("Choose model"); controlEnabled: root.modelChoices.length > 0; onActivated: control => root.composer.openMenu(modelMenu, control) }
            Setting { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 88; label: qsTr("Aspect ratio"); controlName: "aspectRatioButton"; value: root.composer.aspectRatio; onActivated: control => root.composer.openRatioMenu(control) }
            Setting { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 144; label: qsTr("Resolution"); controlName: "homeResolutionButton"; value: root.outputSize.width + " × " + root.outputSize.height; controlEnabled: !root.video; onActivated: control => root.composer.openMenu(resolutionMenu, control) }
            Setting { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 96; label: qsTr("Batch size"); controlName: "generationCountButton"; value: qsTr("%1 %2").arg(root.composer.generationCount).arg(root.video ? root.composer.generationCount === 1 ? qsTr("video") : qsTr("videos") : root.composer.generationCount === 1 ? qsTr("image") : qsTr("images")); onActivated: control => root.composer.openCountMenu(control) }
            Setting { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 120; label: qsTr("Seed"); controlName: "homeSeedButton"; value: root.seed < 0 ? qsTr("Random") : String(root.seed); onActivated: control => root.openPopup(seedPopover, control) }
            LV.VStack { alignment: Qt.AlignLeft;
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 228; Layout.columnSpan: !root.wide && root.width < 600 ? 2 : 1; spacing: LV.Theme.gap4
                LV.Label { text: qsTr("Preset"); style: caption; color: LV.Theme.descriptionColor }
                LV.HStack {
                    Layout.fillWidth: true; spacing: LV.Theme.gap4
                    LV.LabelMenuButton { tone: LV.AbstractButton.Default; objectName: "homeLoadPreset"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: root.selectedPresetName || qsTr("Load preset"); onClicked: root.composer.openMenu(loadMenu, this) }
                    LV.LabelButton { tone: LV.AbstractButton.Default; objectName: "homeSavePreset"; text: qsTr("Save"); onClicked: root.openPopup(presetPopover, this) }
                }
            }
            LV.VStack { alignment: Qt.AlignLeft;
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 152; Layout.columnSpan: !root.wide && root.width < 600 ? 2 : 1; spacing: LV.Theme.gap4
                LV.Label { text: qsTr("More controls"); style: caption; color: LV.Theme.descriptionColor }
                LV.LabelButton { tone: LV.AbstractButton.Default; objectName: "homeAdvancedParameters"; text: qsTr("Advanced parameters"); onClicked: root.composer.advancedRequested(root.parameters(root.selectedModel, true)) }
            }
            Setting { visible: root.video; Layout.fillWidth: true; Layout.minimumWidth: 0; label: qsTr("Duration"); controlName: "homeVideoDuration"; value: qsTr("%1 s").arg(root.composer.videoDuration); onActivated: control => root.composer.openDurationMenu(control) }
            Setting { visible: root.video; Layout.fillWidth: true; Layout.minimumWidth: 0; label: qsTr("Frame rate"); controlName: "homeVideoFps"; value: qsTr("%1 FPS").arg(root.composer.videoFps); onActivated: control => root.composer.openFpsMenu(control) }
        }
                Flickable {
                    id: references
                    objectName: root.composer.desktopLayout ? "homeAttachmentsSlot" : "inactiveDesktopAttachmentsSlot"
                    visible: root.attachments.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: LV.Theme.scaleMetric(44)
                    contentWidth: referenceCards.implicitWidth
                    contentHeight: height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    LV.HStack {
                        id: referenceCards
                        spacing: LV.Theme.gap8
                        Repeater {
                            model: root.attachments
                            delegate: Rectangle {
                                id: reference
                                required property var modelData
                                required property int index
                                objectName: "homeAttachment_" + index
                                width: LV.Theme.scaleMetric(220)
                                height: LV.Theme.scaleMetric(44)
                                radius: LV.Theme.radiusLg
                                color: LV.Theme.panelBackground08
                                LV.HStack {
                                    anchors.fill: parent
                                    anchors.margins: LV.Theme.gap6
                                    spacing: LV.Theme.gap8
                                    Image {
                                        source: reference.modelData.previewSource || reference.modelData.source
                                        sourceSize: Qt.size(60,60)
                                        Layout.preferredWidth: LV.Theme.scaleMetric(30)
                                        Layout.preferredHeight: LV.Theme.scaleMetric(30)
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        clip: true
                                    }
                                    LV.VStack { alignment: Qt.AlignLeft;
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        spacing: 0
                                        LV.Label { text: reference.modelData.filename; style: body; elide: Text.ElideRight; Layout.fillWidth: true }
                                        LV.Label {
                                            text: qsTr("Image · %1").arg(reference.modelData.sizeBytes >= 1048576
                                                ? (reference.modelData.sizeBytes / 1048576).toFixed(1) + " MB"
                                                : Math.max(1, Math.round(reference.modelData.sizeBytes / 1024)) + " KB")
                                            style: caption
                                            color: LV.Theme.descriptionColor
                                        }
                                    }
                                }
                                HoverHandler { id: referenceHover }
                                LV.IconButton {
                                    objectName: "homeRemoveAttachment" + reference.index
                                    visible: referenceHover.hovered || activeFocus
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    iconName: "generalremove"
                                    tone: LV.AbstractButton.Default
                                    Accessible.name: qsTr("Remove %1").arg(reference.modelData.filename)
                                    onClicked: root.composer.homePaint.canvas.removeAttachment(reference.index)
                                }
                            }
                        }
                    }
                }
        LV.Label { Layout.fillWidth: true; visible: root.errorText.length > 0; text: root.errorText; style: caption; color: LV.Theme.accentRed; wrapMode: Text.Wrap }
    }
    LV.ContextMenu { id: homeMediaMenu; objectName: "homeMediaMenu"; showIconSlot: false; items: ["Image", "Video", "Audio", "Board"]; selectedIndex: items.indexOf(root.composer.mediaType); onItemTriggered: function(index, entry) { root.chooseMedia(String(entry)) } }

    LV.ContextMenu {
        id: modelMenu
        objectName: "homeModelMenu"
        showIconSlot: false
        items: root.modelChoices.map(m => m.name || m.id)
        selectedIndex: root.modelChoices.findIndex(m => m.id === root.selectedModel)
        onItemTriggered: function(index) {
            if (!root.generation || index < 0 || index >= root.modelChoices.length) return
            if (root.video) root.generation.selectedVideoModel = root.modelChoices[index].id
            else root.generation.selectedModel = root.modelChoices[index].id
        }
    }
    LV.ContextMenu {
        id: resolutionMenu
        objectName: "homeResolutionMenu"
        readonly property var extents: [512,576,768,1024,1536]
        showIconSlot: false
        items: extents.map(n => { const s = root.sizeFor(n); return s.width + " × " + s.height })
        selectedIndex: extents.indexOf(root.shortSide)
        onItemTriggered: function(index) { root.shortSide = extents[index] }
    }
    LV.ContextMenu {
        id: loadMenu
        objectName: "homePresetMenu"
        showIconSlot: false
        items: root.parameterStore.presets
        onItemTriggered: function(index, entry) { root.loadPreset(String(entry)) }
    }
    LV.Popover {
        id: negativePopover
        parent: root.Window.window ? root.Window.window.contentItem : root
        width: Math.min(500, parent.width - LV.Theme.gap16)
        height: negativeContent.implicitHeight + padding * 2
        contentItem: LV.VStack { alignment: Qt.AlignLeft;
            id: negativeContent
            spacing: LV.Theme.gap8
            LV.Label { text: qsTr("Negative prompt"); style: header2 }
            PromptEditor { Layout.fillWidth: true; value: root.negativePrompt; onEdited: value => root.negativePrompt = value }
            LV.LabelButton { text: qsTr("Done"); tone: LV.AbstractButton.Default; onClicked: negativePopover.close() }
        }
    }
    LV.Popover {
        id: seedPopover
        parent: root.Window.window ? root.Window.window.contentItem : root
        width: Math.min(320, parent.width - LV.Theme.gap16)
        height: seedContent.implicitHeight + padding * 2
        onOpened: seedField.text = root.seed < 0 ? "" : String(root.seed)
        contentItem: LV.VStack { alignment: Qt.AlignLeft;
            id: seedContent
            spacing: LV.Theme.gap8
            LV.Label { text: qsTr("Seed"); style: header2 }
            LV.InputField { id: seedField; objectName: "homeSeedField"; Layout.fillWidth: true; placeholderText: qsTr("Random"); clearButtonVisible: false }
            LV.HStack {
                LV.LabelButton { text: qsTr("Random"); tone: LV.AbstractButton.Default; onClicked: { root.seed = -1; root.errorText = ""; seedPopover.close() } }
                LV.LabelButton {
                    text: qsTr("Apply")
                    tone: LV.AbstractButton.Default
                    onClicked: {
                        const value = Number(seedField.text)
                        if (seedField.text.trim() && Number.isInteger(value) && value >= 0 && value <= 4294967295 - root.composer.generationCount + 1) {
                            root.seed = value; root.errorText = ""; seedPopover.close()
                        } else root.errorText = qsTr("Enter an integer seed with room for the batch, or choose Random.")
                    }
                }
            }
        }
    }
    LV.Popover {
        id: presetPopover
        parent: root.Window.window ? root.Window.window.contentItem : root
        width: Math.min(320, parent.width - LV.Theme.gap16)
        height: presetContent.implicitHeight + padding * 2
        contentItem: LV.VStack { alignment: Qt.AlignLeft;
            id: presetContent
            spacing: LV.Theme.gap8
            LV.Label { text: qsTr("Save preset"); style: header2 }
            LV.InputField { id: presetName; objectName: "homePresetName"; Layout.fillWidth: true; placeholderText: qsTr("Name"); clearButtonVisible: false }
            LV.HStack {
                LV.LabelButton { text: qsTr("Cancel"); tone: LV.AbstractButton.Default; onClicked: presetPopover.close() }
                LV.LabelButton {
                    text: qsTr("Save"); tone: LV.AbstractButton.Default
                    enabled: presetName.text.trim().length > 0
                    onClicked: if (root.savePreset(presetName.text)) { presetName.text = ""; presetPopover.close() }
                }
            }
        }
    }
    Shortcut { sequences: ["Ctrl+Return", "Meta+Return"]; enabled: root.visible && promptEditor.editorItem.activeFocus; onActivated: root.composer.submit() }
}

