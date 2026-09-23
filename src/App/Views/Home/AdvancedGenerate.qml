pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Dialogs as Dialogs
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "advancedGenerate"

    property alias prompt: promptField.text
    property alias negativePrompt: negativePromptField.text
    property bool expanded: true
    property int seed: -1
    property int outputCount: 4
    property var referenceImages: []
    property var loras: []
    readonly property int maximumReferenceImages: 20
    readonly property int controlNetCount: controlNetModel.count
    readonly property int nextControlNetNumber: controlNetModel.count + 1
    readonly property int sectionSpacing: LV.Theme.gap12
    readonly property int groupSpacing: LV.Theme.gap6

    signal referenceImageLimitReached()
    signal presetSaved(string name)
    signal generateRequested(var parameters)

    implicitWidth: LV.Theme.scaleMetric(402)
    implicitHeight: viewport.implicitHeight

    function addReferenceImage(source) {
        if (!source || referenceImages.length >= maximumReferenceImages) {
            if (referenceImages.length >= maximumReferenceImages)
                referenceImageLimitReached()
            return false
        }
        const next = referenceImages.slice()
        next.push(source)
        referenceImages = next
        return true
    }

    function removeReferenceImage(index) {
        if (index < 0 || index >= referenceImages.length)
            return false
        const next = referenceImages.slice()
        next.splice(index, 1)
        referenceImages = next
        return true
    }

    function addControlNet(process) {
        controlNetModel.append({
            expanded: true,
            process: process || "None",
            imageSource: "",
            weight: "1.0",
            ipAdapter: true,
            regionalMask: false
        })
        return controlNetModel.count
    }

    function addLora(source, name) {
        if (!source)
            return false
        const next = loras.slice()
        next.push({ source: source, name: name || String(source).split("/").pop(), weight: 1.0 })
        loras = next
        return true
    }

    function savePreset(name) {
        const normalized = String(name || "").trim()
        if (!normalized)
            return false
        presetSaved(normalized)
        return true
    }

    function parameters() {
        const controls = []
        for (let index = 0; index < controlNetModel.count; ++index)
            controls.push(controlNetModel.get(index))
        return {
            prompt: prompt,
            negativePrompt: negativePrompt,
            seed: seed,
            outputCount: outputCount,
            referenceImages: referenceImages.slice(),
            controlNets: controls,
            loras: loras.slice()
        }
    }

    component SectionHeader: LV.ListItem {
        type: LV.ListItem.Navigation
        Layout.fillWidth: true
        showLeadingIcon: false
        showValue: false
        showTrailingIcon: true
        showDescription: true
    }

    component SelectRow: LV.ListItem {
        id: selectRow
        property var options: []
        property int currentValueIndex: 0
        type: LV.ListItem.Select
        Layout.fillWidth: true
        showLeadingIcon: false
        showDescription: false
        selector: ({ items: selectRow.options })
        selectorIndex: currentValueIndex
        onEdited: function(field, value) {
            if (field === "selectorIndex")
                currentValueIndex = value
        }
    }

    component ToggleRow: LV.ListItem {
        type: LV.ListItem.Toggle
        Layout.fillWidth: true
        showLeadingIcon: false
        showDescription: false
    }

    component InputRow: LV.ListItem {
        type: LV.ListItem.InlineEdit
        Layout.fillWidth: true
        showLeadingIcon: false
        showDescription: false
        showPrimaryAction: false
        inputWidth: LV.Theme.scaleMetric(140)
    }

    component FileCard: Rectangle {
        id: card
        property url source: ""
        property string title: ""
        property string detail: ""
        property bool empty: source.toString().length === 0 && title.length === 0
        signal activated()
        implicitWidth: LV.Theme.scaleMetric(140)
        implicitHeight: LV.Theme.scaleMetric(160)
        radius: LV.Theme.radiusLg
        color: LV.Theme.panelBackground10
        border.width: LV.Theme.scaleRealMetric(0.5)
        border.color: LV.Theme.accentGray
        clip: true

        Image {
            anchors.fill: parent
            anchors.bottomMargin: LV.Theme.scaleMetric(42)
            source: card.source
            fillMode: Image.PreserveAspectCrop
            visible: card.source.toString().length > 0
        }
        LV.Label {
            anchors.centerIn: parent
            text: "+"
            style: title
            visible: card.empty
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: LV.Theme.scaleMetric(42)
            color: LV.Theme.panelBackground01
            opacity: 0.92
            visible: !card.empty
            LV.Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: LV.Theme.gap6
                text: card.title
                style: body
                elide: Text.ElideRight
            }
            LV.Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: LV.Theme.gap6
                text: card.detail
                style: caption
                elide: Text.ElideRight
            }
        }
        MouseArea { anchors.fill: parent; onClicked: card.activated() }
    }

    ListModel {
        id: controlNetModel
        ListElement { expanded: true; process: "Pose"; imageSource: ""; weight: "1.0"; ipAdapter: true; regionalMask: false }
        ListElement { expanded: true; process: "Canny"; imageSource: ""; weight: "1.0"; ipAdapter: true; regionalMask: false }
    }

    Dialogs.FileDialog {
        id: referenceDialog
        objectName: "referenceImageDialog"
        title: qsTr("Add reference images")
        fileMode: Dialogs.FileDialog.OpenFiles
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.heic *.avif)")]
        onAccepted: {
            for (let index = 0; index < selectedFiles.length; ++index) {
                if (!root.addReferenceImage(selectedFiles[index]))
                    break
            }
        }
    }

    Dialogs.FileDialog {
        id: loraDialog
        objectName: "loraDialog"
        title: qsTr("Add LoRA")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("LoRA weights (*.safetensors *.safetensor *.pt *.ckpt)")]
        onAccepted: root.addLora(selectedFile, selectedFile.toString().split("/").pop())
    }

    Flickable {
        id: viewport
        objectName: "advancedGenerationViewport"
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        LV.VStack {
            id: content
            width: viewport.width
            height: implicitHeight
            spacing: root.sectionSpacing
            alignment: Qt.AlignLeft

            SectionHeader {
                objectName: "advancedGenerationHeader"
                label: qsTr("Create image")
                description: root.expanded ? qsTr("All controls visible") : qsTr("Compact controls")
                value: root.expanded ? qsTr("Expanded") : qsTr("Advanced")
                showValue: true
                showTrailingIcon: false
                onClicked: root.expanded = !root.expanded
            }

            LV.VStack {
                Layout.fillWidth: true
                Layout.leftMargin: LV.Theme.gap16
                Layout.rightMargin: LV.Theme.gap16
                spacing: root.groupSpacing
                alignment: Qt.AlignLeft
                LV.Label { text: qsTr("Prompt"); style: caption }
                LV.InputField {
                    id: promptField
                    objectName: "advancedPromptField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Describe the image to create")
                    clearButtonVisible: false
                }
                LV.Label { text: qsTr("Negative Prompt"); style: caption; Layout.topMargin: root.groupSpacing }
                LV.InputField {
                    id: negativePromptField
                    objectName: "advancedNegativePromptField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("What should not appear")
                    clearButtonVisible: false
                }
            }

            LV.VStack {
                Layout.fillWidth: true
                spacing: 0
                SelectRow { objectName: "advancedModel"; label: qsTr("Model"); options: ["Dream XL 2.1"]; currentValueIndex: 0 }
                SelectRow { objectName: "advancedPreset"; label: qsTr("Select preset"); options: [qsTr("Preset 1")]; currentValueIndex: 0; visible: root.expanded }
                SectionHeader { label: qsTr("Essentials"); description: qsTr("Core generation settings"); showTrailingIcon: false }
                SelectRow { label: qsTr("Width"); options: ["512px", "768px", "1024px", "1536px"]; currentValueIndex: 2 }
                SelectRow { label: qsTr("Height"); options: ["512px", "768px", "1024px", "1536px"]; currentValueIndex: 2 }
                SelectRow {
                    objectName: "advancedOutputCount"
                    label: qsTr("Output count")
                    options: ["1 image", "2 images", "3 images", "4 images", "8 images"]
                    currentValueIndex: 3
                    onCurrentValueIndexChanged: root.outputCount = [1, 2, 3, 4, 8][currentValueIndex]
                }
            }

            LV.VStack {
                objectName: "advancedExpandedOptions"
                Layout.fillWidth: true
                spacing: root.sectionSpacing
                Layout.topMargin: root.groupSpacing
                Layout.bottomMargin: root.groupSpacing
                visible: root.expanded

                SectionHeader {
                    label: qsTr("Advanced options")
                    description: qsTr("All controls visible")
                    value: qsTr("Expanded")
                    showValue: true
                    showTrailingIcon: false
                    onClicked: root.expanded = false
                }

                LV.VStack {
                    Layout.fillWidth: true
                    spacing: 0
                    SectionHeader { label: qsTr("Composition"); description: qsTr("3 controls"); showTrailingIcon: false }
                    InputRow {
                        objectName: "advancedSeed"
                        label: qsTr("Seed")
                        inputText1: String(root.seed)
                        input1: ({ clearButtonVisible: false })
                        onEdited: function(field, value) { if (field === "inputText1" && /^-?\d+$/.test(value)) root.seed = Number(value) }
                    }
                    ToggleRow { label: qsTr("Seamless tiling"); checked: false }
                    ToggleRow { label: qsTr("Transparent background"); checked: false }
                }

                LV.VStack {
                    Layout.fillWidth: true
                    spacing: root.groupSpacing
                    SectionHeader { label: qsTr("References & control"); description: qsTr("Reference images · ControlNet · IP-Adapter · masks"); showTrailingIcon: false }
                    LV.ListItem {
                        type: LV.ListItem.Action
                        Layout.fillWidth: true
                        label: qsTr("Reference images")
                        description: qsTr("%1 of %2").arg(root.referenceImages.length).arg(root.maximumReferenceImages)
                        showLeadingIcon: false
                        primaryAction: ({ text: qsTr("Add"), enabled: root.referenceImages.length < root.maximumReferenceImages,
                            method: function() { referenceDialog.open() } })
                    }
                    Flickable {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.referenceImages.length > 0 ? LV.Theme.scaleMetric(160) : 0
                        contentWidth: referenceCards.implicitWidth
                        contentHeight: height
                        visible: root.referenceImages.length > 0
                        clip: true
                        LV.HStack {
                            id: referenceCards
                            height: parent.height
                            spacing: root.groupSpacing
                            Repeater {
                                model: root.referenceImages
                                delegate: FileCard {
                                    required property int index
                                    required property var modelData
                                    source: modelData
                                    title: String(modelData).split("/").pop()
                                    detail: qsTr("Reference %1").arg(index + 1)
                                    onActivated: root.removeReferenceImage(index)
                                }
                            }
                        }
                    }
                    SelectRow { label: qsTr("Image strength"); options: ["0.25", "0.50", "0.65", "0.75", "1.00"]; currentValueIndex: 2 }

                    Repeater {
                        model: controlNetModel
                        delegate: LV.VStack {
                            id: controlLayer
                            required property int index
                            required property bool expanded
                            required property string process
                            required property string weight
                            required property bool ipAdapter
                            required property bool regionalMask
                            Layout.fillWidth: true
                            spacing: 0
                            SectionHeader {
                                objectName: "controlNetHeader" + (controlLayer.index + 1)
                                label: qsTr("Control %1").arg(controlLayer.index + 1)
                                description: controlLayer.expanded ? qsTr("Expanded") : qsTr("Collapsed")
                                showTrailingIcon: true
                                onClicked: controlNetModel.setProperty(controlLayer.index, "expanded", !controlLayer.expanded)
                            }
                            FileCard {
                                Layout.alignment: Qt.AlignHCenter
                                source: ""
                                title: qsTr("Control image")
                                detail: qsTr("Add image")
                                visible: controlLayer.expanded
                            }
                            SelectRow {
                                label: qsTr("Process")
                                options: ["None", "Pose", "Canny", "Depth", "Lineart", "Scribble"]
                                currentValueIndex: Math.max(0, options.indexOf(controlLayer.process))
                                visible: controlLayer.expanded
                                onCurrentValueIndexChanged: controlNetModel.setProperty(controlLayer.index, "process", options[currentValueIndex])
                            }
                            InputRow {
                                label: qsTr("Control weight")
                                inputText1: controlLayer.weight
                                visible: controlLayer.expanded
                                onEdited: function(field, value) { if (field === "inputText1") controlNetModel.setProperty(controlLayer.index, "weight", value) }
                            }
                            ToggleRow {
                                label: qsTr("IP-Adapter")
                                checked: controlLayer.ipAdapter
                                visible: controlLayer.expanded
                                onEdited: function(field, value) { if (field === "checked") controlNetModel.setProperty(controlLayer.index, "ipAdapter", value) }
                            }
                            ToggleRow {
                                label: qsTr("Regional mask")
                                checked: controlLayer.regionalMask
                                visible: controlLayer.expanded
                                onEdited: function(field, value) { if (field === "checked") controlNetModel.setProperty(controlLayer.index, "regionalMask", value) }
                            }
                        }
                    }
                    SectionHeader {
                        objectName: "nextControlNet"
                        label: qsTr("Control %1").arg(root.nextControlNetNumber)
                        description: qsTr("Collapsed")
                        showTrailingIcon: true
                        onClicked: root.addControlNet("None")
                    }
                }

                LV.VStack {
                    Layout.fillWidth: true
                    spacing: 0
                    SectionHeader { label: qsTr("Sampling"); description: qsTr("6 controls"); showTrailingIcon: false }
                    SelectRow { label: qsTr("Sampler"); options: ["DPM++ 2M", "Euler a", "DDIM"]; currentValueIndex: 0 }
                    SelectRow { label: qsTr("Scheduler"); options: ["Karras", "Normal", "Exponential"]; currentValueIndex: 0 }
                    SelectRow { label: qsTr("Steps"); options: ["20", "30", "40", "50"]; currentValueIndex: 1 }
                    SelectRow { label: qsTr("CFG scale"); options: ["5.0", "7.0", "8.0", "12.0"]; currentValueIndex: 1 }
                    SelectRow { label: qsTr("CLIP skip"); options: ["1", "2"]; currentValueIndex: 0 }
                    SelectRow { label: qsTr("Eta"); options: ["0.0", "0.5", "1.0"]; currentValueIndex: 0 }
                }

                LV.VStack {
                    Layout.fillWidth: true
                    spacing: root.groupSpacing
                    LV.ListItem {
                        type: LV.ListItem.Action
                        Layout.fillWidth: true
                        label: qsTr("Fine-tuning")
                        description: qsTr("LoRA · embeddings · VAE · prompt weighting · FreeU")
                        showLeadingIcon: false
                        primaryAction: ({ text: qsTr("Add"), method: function() { loraDialog.open() } })
                    }
                    LV.HStack {
                        Layout.fillWidth: true
                        spacing: root.groupSpacing
                        Repeater {
                            model: root.loras.length > 0 ? root.loras : [{ source: "", name: "", weight: 1.0 }]
                            delegate: FileCard {
                                required property int index
                                required property var modelData
                                // LoRA weights are not preview images. A selected LoRA uses
                                // its filename card; only the empty slot renders the large +.
                                source: ""
                                title: modelData.name || ""
                                detail: modelData.source ? qsTr("LoRA · %1").arg(modelData.weight) : ""
                                onActivated: if (empty) loraDialog.open()
                            }
                        }
                    }
                    SelectRow { label: qsTr("Textual embeddings"); options: [qsTr("None")]; currentValueIndex: 0 }
                    SelectRow { label: qsTr("VAE"); options: [qsTr("Auto"), qsTr("Model default")]; currentValueIndex: 0 }
                    ToggleRow { label: qsTr("Prompt weighting"); checked: true }
                    ToggleRow { label: qsTr("FreeU"); checked: false }
                }

                LV.VStack {
                    Layout.fillWidth: true
                    spacing: 0
                    SectionHeader { label: qsTr("Enhancement"); description: qsTr("7 controls"); showTrailingIcon: false }
                    ToggleRow { label: qsTr("Refiner"); checked: true }
                    SelectRow { label: qsTr("Refiner switch"); options: ["0.80", "0.85", "0.90"]; currentValueIndex: 0 }
                    SelectRow { label: qsTr("Denoise strength"); options: ["0.15", "0.25", "0.40", "0.55"]; currentValueIndex: 1 }
                    ToggleRow { label: qsTr("Hires fix"); checked: true }
                    SelectRow { label: qsTr("Upscaler"); options: ["4× Ultra", "2× Latent", "None"]; currentValueIndex: 0 }
                    ToggleRow { label: qsTr("Face restore"); checked: true }
                    ToggleRow { label: qsTr("Detailer"); checked: true }
                }

                LV.VStack {
                    Layout.fillWidth: true
                    spacing: 0
                    SectionHeader { label: qsTr("Output & safety"); description: qsTr("6 controls"); showTrailingIcon: false }
                    SelectRow { label: qsTr("Format"); options: ["PNG", "JPEG", "WebP"]; currentValueIndex: 0 }
                    SelectRow { label: qsTr("Quality"); options: ["100", "95", "90", "80"]; currentValueIndex: 1 }
                    SelectRow { label: qsTr("Color profile"); options: ["sRGB", "Display P3"]; currentValueIndex: 0 }
                    ToggleRow { label: qsTr("Preserve metadata"); checked: true }
                    ToggleRow { label: qsTr("Watermark"); checked: false }
                    SelectRow { label: qsTr("Safety filter"); options: [qsTr("Standard"), qsTr("Strict"), qsTr("Off")]; currentValueIndex: 0 }
                }
            }

            LV.VStack {
                objectName: "advancedCompactOptions"
                Layout.fillWidth: true
                spacing: root.groupSpacing
                Layout.topMargin: root.groupSpacing
                Layout.bottomMargin: root.groupSpacing
                visible: !root.expanded
                SectionHeader {
                    label: qsTr("Advanced options")
                    description: qsTr("Six control groups")
                    value: qsTr("Collapsed")
                    showValue: true
                    showTrailingIcon: false
                    onClicked: root.expanded = true
                }
                SectionHeader { label: qsTr("Composition"); description: qsTr("Seed · dimensions · batch · tiling · transparency"); onClicked: root.expanded = true }
                SectionHeader { label: qsTr("References & control"); description: qsTr("Image prompt · strength · ControlNet · IP-Adapter · masks"); onClicked: root.expanded = true }
                SectionHeader { label: qsTr("Sampling"); description: qsTr("Sampler · scheduler · steps · CFG · CLIP skip · eta"); onClicked: root.expanded = true }
                SectionHeader { label: qsTr("Fine-tuning"); description: qsTr("LoRA · embeddings · VAE · prompt weighting · FreeU"); onClicked: root.expanded = true }
                SectionHeader { label: qsTr("Enhancement"); description: qsTr("Refiner · denoise · hires fix · upscale · face restore · detailer"); onClicked: root.expanded = true }
                SectionHeader { label: qsTr("Output & safety"); description: qsTr("Format · quality · profile · metadata · watermark · safety filter"); onClicked: root.expanded = true }
            }

            LV.ListItem {
                objectName: "saveGenerationPreset"
                type: LV.ListItem.Action
                Layout.fillWidth: true
                label: qsTr("Generate preset")
                showLeadingIcon: false
                showDescription: false
                visible: root.expanded
                primaryAction: ({ text: qsTr("Save"), method: function() { presetPopup.open() } })
            }
        }
    }

    Controls.Popup {
        id: presetPopup
        objectName: "presetNamePopup"
        anchors.centerIn: parent
        modal: true
        focus: true
        padding: LV.Theme.gap16
        closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
        background: Rectangle { color: LV.Theme.panelBackground10; radius: LV.Theme.radiusLg }
        contentItem: LV.VStack {
            spacing: LV.Theme.gap8
            LV.Label { text: qsTr("Preset name"); style: body }
            LV.InputField { id: presetName; objectName: "presetNameField"; placeholderText: qsTr("Name"); clearButtonVisible: false }
            LV.HStack {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                LV.LabelButton { text: qsTr("Cancel"); tone: LV.AbstractButton.Default; onClicked: presetPopup.close() }
                LV.LabelButton {
                    objectName: "confirmPresetSave"
                    text: qsTr("Save")
                    tone: LV.AbstractButton.Primary
                    enabled: presetName.text.trim().length > 0
                    onClicked: if (root.savePreset(presetName.text)) { presetName.text = ""; presetPopup.close() }
                }
            }
        }
    }
}
