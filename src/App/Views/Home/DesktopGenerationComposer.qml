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
    property int shortSide: 1024
    property real seed: -1
    property string negativePrompt: ""
    property string appliedNegativePrompt: ""
    property bool loadedPreset: false
    property string errorText: ""
    readonly property bool wide: width >= 820
    readonly property bool video: composer.mediaType === "Video"
    readonly property var modelChoices: !generation ? [] : video ? generation.videoModels : generation.models
    readonly property string selectedModel: !generation ? "" : video ? generation.selectedVideoModel : generation.selectedModel
    readonly property string selectedModelName: (modelChoices.find(m => m.id === selectedModel) || {}).name || selectedModel
    readonly property var outputSize: video
        ? ({"1:1": {width:512,height:512}, "4:3":{width:640,height:480}, "3:4":{width:480,height:640},
            "16:9":{width:1024,height:576}, "9:16":{width:576,height:1024}})[composer.aspectRatio] || ({width:512,height:512})
        : sizeFor(shortSide)
    readonly property bool usesExplicitParameters: loadedPreset || shortSide !== 1024 || negativePrompt !== appliedNegativePrompt
    readonly property var attachments: composer.homePaint.canvas.attachments
    color: LV.Theme.panelBackground03
    radius: LV.Theme.radiusLg
    border.color: LV.Theme.panelBackground10
    border.width: 1
    implicitHeight: Math.max(wide ? 300 : 0, layout.implicitHeight + LV.Theme.gap16 * 2)

    AdvancedImageParameters { id: homeDraft; objectName: "homeParameterStore" }
    function sizeFor(shorter) {
        const parts = composer.aspectRatio.split(":").map(Number)
        const scale = shorter / Math.min(parts[0], parts[1])
        return {width: Math.round(parts[0] * scale / 8) * 8, height: Math.round(parts[1] * scale / 8) * 8}
    }
    function applyNegativeDefault() {
        const next = (composer.defaultPrompts[composer.mediaType] || {}).negativePrompt || ""
        if (negativePrompt === appliedNegativePrompt) negativePrompt = next
        appliedNegativePrompt = next
    }
    Component.onCompleted: applyNegativeDefault()
    Connections {
        target: root.composer
        function onDefaultPromptsChanged() { root.applyNegativeDefault() }
        function onMediaTypeChanged() { root.applyNegativeDefault() }
    }
    function parameters(model, includeInputs) {
        const next = Object.assign({}, parameterStore.parameters, {
            prompt: composer.prompt.trim(), negativePrompt: negativePrompt, model: model,
            width: outputSize.width, height: outputSize.height,
            outputCount: composer.generationCount, seed: seed, referenceImages: []
        })
        if (includeInputs && composer.hasCanvasInputs) {
            const inputs = composer.homePaint.generationParameters(composer.prompt.trim(), model, composer.aspectRatio, composer.generationCount)
            if (!Object.keys(inputs).length) return ({})
            next.referenceImages = inputs.referenceImages || []
        }
        return next
    }
    function savePreset(name) {
        if (!parameterStore.updateParameters(parameters(selectedModel, false)) || !parameterStore.savePreset(name)) {
            errorText = parameterStore.errorString
            return false
        }
        errorText = ""
        return true
    }
    function loadPreset(name) {
        if (!parameterStore.loadPreset(name)) { errorText = parameterStore.errorString; return false }
        const p = parameterStore.parameters
        composer.prompt = p.prompt
        composer.generationCount = p.outputCount
        seed = p.seed
        negativePrompt = p.negativePrompt
        const ratio = p.width / p.height
        const standard = ["1:1","4:3","3:4","16:9","9:16"].find(r => {
            const a = r.split(":").map(Number); return Math.abs(a[0] / a[1] - ratio) < .01
        })
        composer.aspectRatio = standard || String(p.width) + ":" + String(p.height)
        shortSide = Math.min(p.width, p.height)
        loadedPreset = true
        if (generation && modelChoices.some(m => m.id === p.model)) {
            if (video) generation.selectedVideoModel = p.model
            else generation.selectedModel = p.model
        }
        errorText = ""
        return true
    }
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

    GridLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: LV.Theme.gap16
        columns: root.wide ? 2 : 1
        columnSpacing: LV.Theme.gap20
        rowSpacing: LV.Theme.gap20

        LV.VStack {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignTop
            spacing: LV.Theme.gap12
            alignment: Qt.AlignLeft
            Flickable {
                id: modesViewport
                Layout.fillWidth: true
                Layout.preferredHeight: modes.implicitHeight
                contentWidth: modes.implicitWidth
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                LV.HStack {
                    id: modes
                    spacing: LV.Theme.gap6
                    Repeater {
                        model: ["Image", "Video", "Audio", "Board"]
                        delegate: LV.LabelButton {
                            required property string modelData
                            objectName: "homeMedia" + modelData
                            text: modelData
                            tone: LV.AbstractButton.Default
                            checked: root.composer.mediaType === modelData
                            onClicked: {
                                if (modelData === "Image" || modelData === "Video") root.composer.mediaType = modelData
                                else root.composer.mediaActionRequested(modelData.toLowerCase())
                            }
                        }
                    }
                }
            }
            LV.VStack {
                Layout.fillWidth: true
                spacing: LV.Theme.gap6
                LV.HStack {
                    Layout.fillWidth: true
                    LV.Label { text: qsTr("PROMPT"); style: caption; color: LV.Theme.descriptionColor }
                    Item { Layout.fillWidth: true }
                    LV.LabelButton {
                        text: qsTr("Clear")
                        tone: LV.AbstractButton.Borderless
                        onClicked: root.composer.prompt = ""
                    }
                }
                PromptEditor {
                    id: promptEditor
                    objectName: root.composer.desktopLayout ? "promptField" : "inactiveDesktopPromptField"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    value: root.composer.prompt
                    placeholderText: qsTr("Describe what to create…")
                    Accessible.name: qsTr("Prompt")
                    onEdited: function(value) { root.composer.prompt = value }
                }
            }
            LV.HStack {
                Layout.fillWidth: true
                Layout.preferredHeight: LV.Theme.scaleMetric(44)
                spacing: LV.Theme.gap8
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
                                    LV.VStack {
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
                Item { visible: !references.visible; Layout.fillWidth: true }
                LV.IconButton {
                    objectName: root.composer.desktopLayout ? "homeAttachButton" : "inactiveDesktopAttachButton"
                    iconName: "generaladd"
                    tone: LV.AbstractButton.Default
                    Accessible.name: qsTr("Attach reference images")
                    onClicked: root.composer.chooseAttachments()
                }
            }
            Flickable {
                id: footerViewport
                objectName: root.composer.desktopLayout ? "quickGenerateActions" : "inactiveDesktopActions"
                Layout.fillWidth: true
                Layout.preferredHeight: footer.implicitHeight
                contentWidth: Math.max(width, footer.implicitWidth)
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                LV.HStack {
                    id: footer
                    width: Math.max(footerViewport.width, implicitWidth)
                    spacing: LV.Theme.gap8
                    LV.LabelButton { text: qsTr("Paint canvas"); tone: LV.AbstractButton.Default; onClicked: root.composer.paintRequested() }
                    LV.LabelMenuButton {
                        id: negativeButton
                        objectName: "homeNegativePromptButton"
                        text: qsTr("Negative prompt")
                        tone: LV.AbstractButton.Default
                        onClicked: root.openPopup(negativePopover, this)
                    }
                    Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
                    LV.LabelButton { text: qsTr("Load preset"); tone: LV.AbstractButton.Borderless; onClicked: root.composer.openMenu(loadMenu, this) }
                    LV.LabelButton { id: saveButton; text: qsTr("Save preset"); tone: LV.AbstractButton.Borderless; onClicked: root.openPopup(presetPopover, this) }
                }
            }
        }

        LV.VStack {
            Layout.fillWidth: true
            Layout.minimumWidth: root.wide ? 288 : 0
            Layout.maximumWidth: root.wide ? 288 : Infinity
            Layout.preferredWidth: root.wide ? 288 : -1
            Layout.alignment: Qt.AlignTop
            spacing: LV.Theme.gap10
            alignment: Qt.AlignLeft
            LV.Label { text: qsTr("Generation setup"); style: header2 }
            Setting {
                Layout.fillWidth: true
                label: qsTr("Model"); controlName: "homeModelButton"
                value: root.selectedModelName || qsTr("Choose model")
                controlEnabled: root.modelChoices.length > 0
                onActivated: control => root.composer.openMenu(modelMenu, control)
            }
            LV.HStack {
                Layout.fillWidth: true
                spacing: LV.Theme.gap8
                Setting {
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    label: qsTr("Aspect ratio"); controlName: "aspectRatioButton"; value: root.composer.aspectRatio
                    onActivated: control => root.composer.openRatioMenu(control)
                }
                Setting {
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    label: qsTr("Resolution"); controlName: "homeResolutionButton"
                    value: root.outputSize.width + " × " + root.outputSize.height
                    controlEnabled: !root.video
                    onActivated: control => root.composer.openMenu(resolutionMenu, control)
                }
            }
            LV.HStack {
                Layout.fillWidth: true
                spacing: LV.Theme.gap8
                Setting {
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    label: qsTr("Batch size"); controlName: "generationCountButton"
                    value: qsTr("%1 %2").arg(root.composer.generationCount).arg(root.video
                        ? root.composer.generationCount === 1 ? qsTr("video") : qsTr("videos")
                        : root.composer.generationCount === 1 ? qsTr("image") : qsTr("images"))
                    onActivated: control => root.composer.openCountMenu(control)
                }
                Setting {
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    Layout.preferredWidth: 1
                    label: qsTr("Seed"); controlName: "homeSeedButton"
                    value: root.seed < 0 ? qsTr("Random") : String(root.seed)
                    onActivated: control => root.openPopup(seedPopover, control)
                }
            }
            LV.LabelButton {
                objectName: "homeAdvancedParameters"
                text: qsTr("Advanced parameters")
                tone: LV.AbstractButton.Borderless
                onClicked: root.composer.advancedRequested(root.parameters(root.selectedModel, true))
            }
            LV.HStack {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
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
        }
    }

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
        contentItem: LV.VStack {
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
        contentItem: LV.VStack {
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
        contentItem: LV.VStack {
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

