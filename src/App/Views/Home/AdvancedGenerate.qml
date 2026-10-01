pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Dialogs as Dialogs
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

Item {
    id: root
    objectName: "advancedGenerate"

    property alias prompt: promptField.text
    property alias negativePrompt: negativePromptField.text
    property alias parameterStore: draft
    property var models: []
    property var vaes: []
    property bool expanded: true
    property var invalidInputs: ({})
    readonly property bool hasInvalidInputs: Object.keys(invalidInputs).length > 0
    readonly property var values: draft.parameters
    onValuesChanged: {
        // Collection delegates are rebuilt from the new authoritative snapshot.
        const next = Object.assign({}, invalidInputs)
        for (const key of Object.keys(next))
            if (key.indexOf("control.") === 0 || key.indexOf("lora.") === 0) delete next[key]
        invalidInputs = next
    }
    readonly property real seed: values.seed
    readonly property int outputCount: values.outputCount
    readonly property var referenceImages: values.referenceImages
    readonly property var loras: values.loras
    readonly property int maximumReferenceImages: 20
    readonly property int controlNetCount: values.controlNets.length
    readonly property int nextControlNetNumber: controlNetCount + 1
    readonly property int sectionSpacing: LV.Theme.gap12
    readonly property int groupSpacing: LV.Theme.gap6
    signal referenceImageLimitReached()
    signal presetSaved(string name)
    signal generateRequested(var parameters)
    signal draftRestored()
    implicitWidth: LV.Theme.scaleMetric(402)
    implicitHeight: 844

    AdvancedImageParameters { id: draft; objectName: "advancedParameterStore" }
    function edit(key, value) {
        const patch = {}; patch[key] = value
        const accepted = draft.updateParameters(patch)
        recordValidity(key, accepted)
        return accepted
    }
    function recordValidity(key, valid) {
        const next = Object.assign({}, invalidInputs)
        if (valid) delete next[key]; else next[key] = true
        invalidInputs = next
    }
    function resetDraft() { draft.reset(); invalidInputs = ({}); draftRestored() }
    function loadPreset(name) {
        if (!draft.loadPreset(name)) return false
        invalidInputs = ({}); draftRestored(); return true
    }
    function editNumber(key, text) {
        if (String(text).trim() === "") return edit(key, Number.NaN)
        return edit(key, Number(text))
    }
    function selectLearnedUpscaler(source) {
        // Commit selection and source together; cancelling the dialog edits neither.
        const accepted = draft.updateParameters({upscaler: "4x-ultra", upscalerModel: String(source)})
        recordValidity("upscalerModel", accepted)
        return accepted
    }
    function selectDetailer(source) {
        const accepted = draft.updateParameters({detailer: true, detailerModel: String(source)})
        recordValidity("detailerModel", accepted)
        return accepted
    }
    function selectRefiner(source) {
        const accepted = draft.updateParameters({refiner: true, refinerModel: String(source)})
        recordValidity("refinerModel", accepted)
        return accepted
    }
    function selectIPAdapter(id, model, vision) {
        if (!String(model).length || !String(vision).length) return false
        return draft.updateControlNet(id, {ipAdapter: true, ipAdapterModel: String(model), ipAdapterVision: String(vision)})
    }
    function selectPose(id, detector, model) {
        if (!String(detector).length || !String(model).length) return false
        return draft.updateControlNet(id, {process: "Pose", poseDetector: String(detector), poseModel: String(model)})
    }
    function choosePose(id) {
        const entry = values.controlNets.find(function(item) { return item.id === id })
        if (!entry) return
        poseDetectorDialog.controlId = id
        poseDetectorDialog.selectedFile = entry.poseDetector || ""
        poseModelDialog.selectedFile = entry.poseModel || ""
        poseDetectorDialog.open()
    }
    function chooseIPAdapter(id) {
        const entry = values.controlNets.find(function(item) { return item.id === id })
        if (!entry) return
        ipModelDialog.controlId = id
        ipModelDialog.selectedFile = entry.ipAdapterModel || ""
        ipVisionDialog.selectedFile = entry.ipAdapterVision || ""
        ipModelDialog.open()
    }
    function addReferenceImage(source) {
        if (referenceImages.length >= maximumReferenceImages) referenceImageLimitReached()
        return draft.addReferenceImage(String(source))
    }
    function removeReferenceImage(index) { return draft.removeReferenceImage(index) }
    function addControlNet(process) { draft.addControlNet(process || "None"); return controlNetCount }
    function addLora(source, name) { return draft.addLora(String(source), name || String(source).split("/").pop()).length > 0 }
    function savePreset(name) {
        if (hasInvalidInputs) return false
        if (!draft.savePreset(name)) return false
        presetSaved(String(name).trim()); return true
    }
    function parameters() { return draft.parameters }
    function focusPrompt() { promptField.forceActiveFocus() }

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
        property string parameterKey: ""
        property var optionValues: options
        property int currentValueIndex: parameterKey ? Math.max(0, optionValues.indexOf(root.values[parameterKey])) : 0
        type: LV.ListItem.Select
        Layout.fillWidth: true
        showLeadingIcon: false
        showDescription: false
        selector: ({ items: selectRow.options })
        Binding { target: selectRow; property: "selectorIndex"; value: selectRow.currentValueIndex }
        onEdited: function(field, value) {
            if (field === "selectorIndex") {
                if (parameterKey) root.edit(parameterKey, optionValues[value])
                else currentValueIndex = value
            }
        }
    }

    component ToggleRow: LV.ListItem {
        id: toggleRow
        property string parameterKey: ""
        Binding { target: toggleRow; property: "checked"; value: toggleRow.parameterKey ? root.values[toggleRow.parameterKey] : false; when: toggleRow.parameterKey !== "" }
        onEdited: function(field, value) { if (field === "checked" && parameterKey) root.edit(parameterKey, value) }
        type: LV.ListItem.Toggle
        Layout.fillWidth: true
        showLeadingIcon: false
        showDescription: false
    }

    component InputRow: LV.ListItem {
        id: inputRow
        property string parameterKey: ""
        property bool numeric: true
        property bool invalidEdit: false
        input1: ({ clearButtonVisible: false })
        Binding { target: inputRow; property: "inputText1"; value: inputRow.parameterKey ? String(root.values[inputRow.parameterKey]) : ""; when: inputRow.parameterKey !== "" && !inputRow.invalidEdit; restoreMode: Binding.RestoreNone }
        Connections { target: root; function onDraftRestored() { inputRow.invalidEdit = false } }
        onEdited: function(field, value) {
            if (field === "inputText1" && parameterKey) {
                const accepted = numeric ? root.editNumber(parameterKey, value) : root.edit(parameterKey, value)
                invalidEdit = !accepted
            }
        }
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
        id: controlDialog
        property string controlId: ""
        property string sourceField: "imageSource"
        function choose(id, field) { controlId = id; sourceField = field; open() }
        title: qsTr("Select control image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp)")]
        onAccepted: { const patch = {}; patch[sourceField] = selectedFile.toString(); draft.updateControlNet(controlId, patch) }
    }
    Dialogs.FileDialog {
        id: controlModelDialog
        property string controlId: ""
        title: qsTr("Select ControlNet model")
        nameFilters: [qsTr("Model weights (*.safetensors *.gguf)")]
        onAccepted: draft.updateControlNet(controlId, {model: selectedFile.toString()})
    }

    Dialogs.FileDialog {
        id: loraDialog
        objectName: "loraDialog"
        title: qsTr("Add LoRA")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("LoRA weights (*.safetensors *.gguf)")]
        onAccepted: root.addLora(selectedFile, selectedFile.toString().split("/").pop())
    }

    Dialogs.FileDialog {
        id: ipModelDialog
        objectName: "controlIPModelDialog"
        property string controlId: ""
        title: qsTr("Choose IP-Adapter weights")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Adapter weights (*.safetensors *.safetensor *.gguf)")]
        onAccepted: {
            ipVisionDialog.controlId = controlId
            ipVisionDialog.adapterSource = selectedFile.toString()
            ipVisionDialog.open()
        }
    }
    Dialogs.FileDialog {
        id: poseDetectorDialog
        objectName: "controlPoseDetectorDialog"
        property string controlId: ""
        title: qsTr("Choose YOLOX-L Pose detector")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Inline ONNX model (*.onnx)")]
        onAccepted: {
            poseModelDialog.controlId = controlId
            poseModelDialog.detectorSource = selectedFile.toString()
            poseModelDialog.open()
        }
    }
    Dialogs.FileDialog {
        id: poseModelDialog
        objectName: "controlPoseModelDialog"
        property string controlId: ""
        property string detectorSource: ""
        title: qsTr("Choose DWPose joint model")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Inline ONNX model (*.onnx)")]
        onAccepted: root.selectPose(controlId, detectorSource, selectedFile.toString())
    }
    Dialogs.FileDialog {
        id: ipVisionDialog
        objectName: "controlIPVisionDialog"
        property string controlId: ""
        property string adapterSource: ""
        title: qsTr("Choose IP-Adapter CLIP vision weights")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Vision weights (*.safetensors *.safetensor *.gguf)")]
        onAccepted: root.selectIPAdapter(controlId, adapterSource, selectedFile.toString())
    }

    Dialogs.FileDialog {
        id: embeddingDialog
        objectName: "textualEmbeddingDialog"
        title: qsTr("Select textual embedding")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Embedding weights (*.safetensors *.safetensor *.gguf)")]
        onAccepted: root.edit("textualEmbeddings", selectedFile.toString())
    }
    Dialogs.FileDialog {
        id: upscalerDialog
        objectName: "upscalerModelDialog"
        title: qsTr("Choose 4× ESRGAN weights")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Upscaler weights (*.safetensors *.safetensor *.gguf)")]
        onAccepted: root.selectLearnedUpscaler(selectedFile.toString())
    }
    Dialogs.FileDialog {
        id: detailerDialog
        objectName: "detailerModelDialog"
        title: qsTr("Choose converted YOLOv8 detector weights")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Detector weights (*.safetensors *.safetensor *.gguf)")]
        onAccepted: root.selectDetailer(selectedFile.toString())
    }
    Dialogs.FileDialog {
        id: refinerDialog
        objectName: "refinerModelDialog"
        title: qsTr("Choose SDXL Refiner checkpoint")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [qsTr("Refiner weights (*.safetensors *.safetensor *.gguf)")]
        onAccepted: root.selectRefiner(selectedFile.toString())
    }

    Flickable {
        id: viewport
        objectName: "advancedGenerationViewport"
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }

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
                PromptField {
                    id: promptField
                    text: root.values.prompt
                    onTextChanged: if (text !== root.values.prompt) root.edit("prompt", text)
                    objectName: "advancedPromptField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Describe the image to create")
                    clearButtonVisible: false
                }
                LV.Label { text: qsTr("Negative Prompt"); style: caption; Layout.topMargin: root.groupSpacing }
                PromptField {
                    id: negativePromptField
                    text: root.values.negativePrompt
                    onTextChanged: if (text !== root.values.negativePrompt) root.edit("negativePrompt", text)
                    objectName: "advancedNegativePromptField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("What should not appear")
                    clearButtonVisible: false
                }
            }

            LV.VStack {
                Layout.fillWidth: true
                spacing: 0
                SelectRow { objectName: "advancedModel"; label: qsTr("Model"); parameterKey: "model"; options: [qsTr("Selected model")].concat(root.models.map(m => m.name || m.id)); optionValues: [""].concat(root.models.map(m => m.id)) }
                SelectRow { objectName: "advancedPreset"; label: qsTr("Select preset"); options: [qsTr("Select preset")].concat(draft.presets); onEdited: function(field, value) { if (field === "selectorIndex" && value > 0) root.loadPreset(options[value]) }; visible: root.expanded }
                SectionHeader { label: qsTr("Essentials"); description: qsTr("Core generation settings"); showTrailingIcon: false }
                InputRow { objectName: "advancedWidth"; label: qsTr("Width"); parameterKey: "width" }
                InputRow { objectName: "advancedHeight"; label: qsTr("Height"); parameterKey: "height" }
                InputRow { objectName: "advancedOutputCount"; label: qsTr("Output count"); parameterKey: "outputCount" }
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
                    InputRow { objectName: "advancedSeed"; label: qsTr("Seed"); parameterKey: "seed" }
                    ToggleRow { label: qsTr("Seamless tiling"); parameterKey: "seamlessTiling" }
                    ToggleRow { label: qsTr("Transparent background"); parameterKey: "transparentBackground" }
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
                    InputRow { label: qsTr("Image strength"); parameterKey: "imageStrength" }

                    Repeater {
                        model: root.values.controlNets
                        delegate: LV.VStack {
                            id: controlLayer
                            required property int index
                            required property var modelData
                            property bool expanded: true
                            readonly property string controlId: modelData.id
                            Layout.fillWidth: true
                            spacing: 0
                            SectionHeader {
                                objectName: "controlNetHeader" + (controlLayer.index + 1)
                                label: qsTr("Control %1").arg(controlLayer.index + 1)
                                description: controlLayer.expanded ? qsTr("Expanded") : qsTr("Collapsed")
                                showTrailingIcon: true
                                onClicked: controlLayer.expanded = !controlLayer.expanded
                            }
                            FileCard {
                                Layout.alignment: Qt.AlignHCenter
                                source: controlLayer.modelData.imageSource
                                title: qsTr("Control image")
                                detail: qsTr("Select image")
                                onActivated: controlDialog.choose(controlLayer.controlId, "imageSource")
                                visible: controlLayer.expanded
                            }
                            SelectRow {
                                id: processRow
                                objectName: "controlProcess" + (controlLayer.index + 1)
                                label: qsTr("Process")
                                options: ["None", "Pose", "Canny", "Depth", "Line Art", "Scribble", "MLSD", "Normal Map", "Semantic Segment", "Shuffle", "Tile", "Reference", "IP-Adapter"]
                                currentValueIndex: Math.max(0, options.indexOf(controlLayer.modelData.process))
                                visible: controlLayer.expanded
                                Binding { target: processRow; property: "selectorIndex"; value: processRow.currentValueIndex }
                                onEdited: function(field, value) {
                                    if (field !== "selectorIndex") return
                                    if (options[value] === "Pose") {
                                        selectorIndex = currentValueIndex
                                        root.choosePose(controlLayer.controlId)
                                    } else draft.updateControlNet(controlLayer.controlId, {process: options[value]})
                                }
                            }
                            LV.ListItem {
                                type: LV.ListItem.Action
                                Layout.fillWidth: true
                                visible: controlLayer.expanded && controlLayer.modelData.process === "Pose"
                                label: qsTr("Pose models")
                                description: String(controlLayer.modelData.poseModel).split("/").pop() || qsTr("Select detector and pose model")
                                showLeadingIcon: false
                                primaryAction: ({text: qsTr("Select"), method: function() { root.choosePose(controlLayer.controlId) }})
                            }
                            LV.ListItem {
                                type: LV.ListItem.Action
                                Layout.fillWidth: true
                                visible: controlLayer.expanded
                                label: qsTr("Control model")
                                enabled: controlLayer.modelData.process !== "None" && controlLayer.modelData.process !== "IP-Adapter"
                                description: String(controlLayer.modelData.model).split("/").pop() || qsTr("Select weights")
                                showLeadingIcon: false
                                primaryAction: ({text: qsTr("Select"), method: function() { controlModelDialog.controlId = controlLayer.controlId; controlModelDialog.open() }})
                            }
                            LV.ListItem {
                                type: LV.ListItem.Action
                                Layout.fillWidth: true
                                visible: controlLayer.expanded && controlLayer.modelData.regionalMask
                                label: qsTr("Mask image")
                                description: String(controlLayer.modelData.maskSource).split("/").pop()
                                showLeadingIcon: false
                                primaryAction: ({text: qsTr("Select"), method: function() { controlDialog.choose(controlLayer.controlId, "maskSource") }})
                            }
                            LV.ListItem {
                                type: LV.ListItem.ActionGroup
                                Layout.fillWidth: true
                                visible: controlLayer.expanded
                                label: qsTr("ControlNet")
                                description: controlLayer.modelData.applied ? qsTr("Applied") : qsTr("Draft")
                                showLeadingIcon: false
                                primaryAction: ({text: qsTr("Init"), method: function() { draft.resetControlNet(controlLayer.controlId) }})
                                secondaryAction: ({text: qsTr("Apply"), method: function() { draft.applyControlNet(controlLayer.controlId) }})
                                showPrimaryAction: true
                                showSecondaryAction: true
                            }
                            InputRow {
                                label: qsTr("Control weight")
                                inputText1: String(controlLayer.modelData.weight)
                                visible: controlLayer.expanded
                                onEdited: function(field, value) {
                                    if (field === "inputText1") {
                                        const key = "control." + controlLayer.controlId
                                        root.recordValidity(key, draft.updateControlNet(controlLayer.controlId, {weight: String(value).trim() === "" ? Number.NaN : Number(value)}))
                                    }
                                }
                            }
                            ToggleRow {
                                id: ipToggleRow
                                objectName: "controlIPToggle" + (controlLayer.index + 1)
                                label: qsTr("IP-Adapter")
                                Binding { target: ipToggleRow; property: "checked"; value: controlLayer.modelData.ipAdapter }
                                visible: controlLayer.expanded
                                onEdited: function(field, value) {
                                    if (field !== "checked") return
                                    if (value) {
                                        checked = controlLayer.modelData.ipAdapter
                                        root.chooseIPAdapter(controlLayer.controlId)
                                    }
                                    else draft.updateControlNet(controlLayer.controlId, {ipAdapter: false})
                                }
                            }
                            ToggleRow {
                                label: qsTr("Regional mask")
                                checked: controlLayer.modelData.regionalMask
                                visible: controlLayer.expanded
                                onEdited: function(field, value) { if (field === "checked") draft.updateControlNet(controlLayer.controlId, {regionalMask: value}) }
                            }
                        }
                    }
                    LV.ListItem {
                        objectName: "addControlNet"
                        type: LV.ListItem.Navigation
                        Layout.fillWidth: true
                        label: qsTr("Add control")
                        showLeadingIcon: false
                        showDescription: false
                        showValue: true
                        value: ""
                        showTrailingIcon: true
                        trailingIconName: "generaladd"
                        onClicked: root.addControlNet("None")
                    }
                }

                LV.VStack {
                    Layout.fillWidth: true
                    spacing: 0
                    SectionHeader { label: qsTr("Sampling"); description: qsTr("6 controls"); showTrailingIcon: false }
                    SelectRow { label: qsTr("Sampler"); parameterKey: "sampler"; options: ["Auto", "Euler", "Heun", "Euler a", "DPM++ 2M", "DPM++ SDE", "DDIM"]; optionValues: ["auto", "euler", "heun", "euler_a", "dpmpp_2m", "dpmpp_sde", "ddim"] }
                    SelectRow { label: qsTr("Scheduler"); parameterKey: "scheduler"; options: ["Auto", "Normal", "Karras", "Exponential", "SGM Uniform"]; optionValues: ["auto", "normal", "karras", "exponential", "sgm_uniform"] }
                    InputRow { label: qsTr("Steps"); parameterKey: "steps" }
                    InputRow { label: qsTr("CFG scale"); parameterKey: "cfgScale" }
                    InputRow { label: qsTr("CLIP skip"); parameterKey: "clipSkip" }
                    InputRow { label: qsTr("Eta"); parameterKey: "eta" }
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
                    FileCard { visible: root.loras.length === 0; onActivated: loraDialog.open() }
                    Repeater {
                        model: root.loras
                        delegate: LV.VStack {
                            id: loraLayer
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: root.groupSpacing
                            FileCard {
                                // Model weights do not have an image preview.
                                title: loraLayer.modelData.name
                                detail: qsTr("LoRA")
                            }
                            InputRow {
                                label: qsTr("LoRA weight")
                                inputText1: String(loraLayer.modelData.weight)
                                onEdited: function(field, value) {
                                    if (field === "inputText1") {
                                        const key = "lora." + loraLayer.modelData.id
                                        root.recordValidity(key, draft.updateLora(loraLayer.modelData.id, {weight: String(value).trim() === "" ? Number.NaN : Number(value)}))
                                    }
                                }
                                showPrimaryAction: true
                                primaryAction: ({text: qsTr("Remove"), method: function() { draft.removeLora(loraLayer.modelData.id) }})
                            }
                        }
                    }
                    LV.ListItem {
                        id: embeddingSelector
                        objectName: "textualEmbeddingSelector"
                        readonly property string selectedSource: root.values.textualEmbeddings
                        type: LV.ListItem.Select
                        Layout.fillWidth: true
                        label: qsTr("Textual embeddings")
                        showLeadingIcon: false
                        showDescription: false
                        selector: ({items: selectedSource ? [qsTr("None"), selectedSource.split("/").pop(), qsTr("Choose file…")]
                                                         : [qsTr("None"), qsTr("Choose file…")]})
                        Binding { target: embeddingSelector; property: "selectorIndex"; value: embeddingSelector.selectedSource ? 1 : 0 }
                        onEdited: function(field, value) {
                            if (field !== "selectorIndex") return
                            if (value === 0) root.edit("textualEmbeddings", "")
                            else if (value === (selectedSource ? 2 : 1)) {
                                selectorIndex = selectedSource ? 1 : 0
                                embeddingDialog.open()
                            }
                        }
                    }
                    SelectRow { label: qsTr("VAE"); parameterKey: "vae"; options: [qsTr("Auto")].concat(root.vaes.map(m => m.name || m.id)); optionValues: [""].concat(root.vaes.map(m => m.id)) }
                    ToggleRow { label: qsTr("Prompt weighting"); parameterKey: "promptWeighting" }
                    ToggleRow { label: qsTr("FreeU"); parameterKey: "freeU" }
                }

                LV.VStack {
                    Layout.fillWidth: true
                    spacing: 0
                    SectionHeader { label: qsTr("Enhancement"); description: qsTr("7 controls"); showTrailingIcon: false }
                    LV.ListItem {
                        id: refinerToggle
                        objectName: "refinerToggle"
                        type: LV.ListItem.Toggle
                        Layout.fillWidth: true
                        label: qsTr("Refiner")
                        showLeadingIcon: false
                        showDescription: false
                        Binding { target: refinerToggle; property: "checked"; value: root.values.refiner }
                        onEdited: function(field, value) {
                            if (field !== "checked") return
                            if (value) {
                                checked = root.values.refiner
                                refinerDialog.open()
                            } else root.edit("refiner", false)
                        }
                    }
                    InputRow { label: qsTr("Refiner switch"); parameterKey: "refinerSwitch" }
                    InputRow { label: qsTr("Denoise strength"); parameterKey: "denoiseStrength" }
                    ToggleRow { label: qsTr("Hires fix"); parameterKey: "hiresFix" }
                    LV.ListItem {
                        id: upscalerSelector
                        objectName: "upscalerSelector"
                        readonly property var modes: ["nearest", "bilinear", "bicubic", "lanczos", "4x-ultra"]
                        readonly property int selectedIndex: Math.max(0, modes.indexOf(root.values.upscaler))
                        type: LV.ListItem.Select
                        Layout.fillWidth: true
                        label: qsTr("Upscaler")
                        showLeadingIcon: false
                        showDescription: false
                        selector: ({items: ["Nearest", "Bilinear", "Bicubic", "Lanczos", "4× Ultra", qsTr("Choose weights…")]})
                        Binding { target: upscalerSelector; property: "selectorIndex"; value: upscalerSelector.selectedIndex }
                        onEdited: function(field, value) {
                            if (field !== "selectorIndex") return
                            if (value >= 4) {
                                selectorIndex = selectedIndex
                                upscalerDialog.open()
                            } else root.edit("upscaler", modes[value])
                        }
                    }
                    ToggleRow { label: qsTr("Face restore"); parameterKey: "faceRestore" }
                    LV.ListItem {
                        id: detailerToggle
                        objectName: "detailerToggle"
                        type: LV.ListItem.Toggle
                        Layout.fillWidth: true
                        label: qsTr("Detailer")
                        showLeadingIcon: false
                        showDescription: false
                        Binding { target: detailerToggle; property: "checked"; value: root.values.detailer }
                        onEdited: function(field, value) {
                            if (field !== "checked") return
                            if (value) {
                                checked = root.values.detailer
                                detailerDialog.open()
                            } else root.edit("detailer", false)
                        }
                    }
                }

                LV.VStack {
                    Layout.fillWidth: true
                    spacing: 0
                    SectionHeader { label: qsTr("Output & safety"); description: qsTr("6 controls"); showTrailingIcon: false }
                    SelectRow { label: qsTr("Color profile"); parameterKey: "colorProfile"; options: ["sRGB", "Display P3"] }
                    ToggleRow { label: qsTr("Preserve metadata"); parameterKey: "preserveMetadata" }
                    ToggleRow { objectName: "watermarkToggle"; label: qsTr("Watermark"); parameterKey: "watermark" }
                    SelectRow { label: qsTr("Safety filter"); parameterKey: "safetyFilter"; options: [qsTr("Off"), qsTr("Standard"), qsTr("Strict")]; optionValues: ["off", "standard", "strict"] }
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

            LV.Label {
                Layout.fillWidth: true
                Layout.leftMargin: LV.Theme.gap16
                Layout.rightMargin: LV.Theme.gap16
                text: root.hasInvalidInputs ? qsTr("Correct invalid input before generating or saving a preset. %1").arg(draft.errorString) : draft.errorString
                visible: text.length > 0
                wrapMode: Text.Wrap
                sizeToContentHeight: true
                style: caption
            }
            LV.ListItem {
                objectName: "saveGenerationPreset"
                type: LV.ListItem.Action
                Layout.fillWidth: true
                label: qsTr("Save to preset")
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
                    enabled: presetName.text.trim().length > 0 && !root.hasInvalidInputs
                    onClicked: if (root.savePreset(presetName.text)) { presetName.text = ""; presetPopup.close() }
                }
            }
        }
    }
}
