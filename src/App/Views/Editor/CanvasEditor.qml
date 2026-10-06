pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import "EditorToolDefinitions.js" as Definitions
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0
import "../Home"

Item {
    id: root
    objectName: "canvasEditor"
    property var generationRuntime: null
    property var editorJobIds: []
    property bool submitting: false
    property string generationOperation: ""
    property var previewData: ({})
    property string previewTool: ""
    property string previewField: ""
    property var previewValues: ({})
    readonly property bool modalActive: toolSheet.modalActive || documentControls.modalActive || previewSheet.visible || sourceDialog.visible || exportDialog.visible || advancedSheet.visible
    property int selectedPath: -1
    property var generationTargetCanvas: null
    property url imageSource: ""
    property var generationResult: ({})
    property var generationResults: []
    property var canvasSpecification: ({})
    readonly property alias project: canvasProject
    readonly property var blankCanvas: canvasProject.currentCanvas
    readonly property string documentError: canvasProject.error
    readonly property var activeGenerationResult: generationResults[canvasProject.currentIndex] || generationResult
    function createBlankCanvas(specification) { return canvasProject.createCanvas(specification) }
    function openDocumentSource(source, asCopy) {
        if (!canvasProject.openDocumentSource(source, Boolean(asCopy))) return false
        documentOpened(source)
        return true
    }
    function saveDocumentAs(destination) { return canvasProject.saveDocumentAs(destination) }
    function openImages(sources) { return canvasProject.openImages(sources) }
    function localFileSource(path) { return blankCanvas.localFileSource(path) }
    property bool mobileLayout: false
    readonly property string selectedTool: bottomToolbar.currentTool
    property bool desktopPanelExpanded: true
    // Retain the requested width when a smaller window temporarily clamps it.
    property real desktopPanelWidth: 342
    readonly property real desktopPanelMinimumWidth: 280
    readonly property real desktopPanelMaximumWidth: Math.min(720,
        Math.max(desktopPanelMinimumWidth, width - sidebar.width - 320),
        Math.max(0, width - sidebar.width))
    readonly property bool compactSidebar: width < 900
    signal backRequested()
    signal navigationRequested(string action)
    signal toolSelected(string toolId)
    signal toolActionRequested(string toolId, string fieldId, var values)
    signal documentOpened(url source)
    EditorProject { id: canvasProject; objectName: "editorProject" }
    function mountCanvas() {
        canvasProject.attachCanvas(canvasSurface)
        resizeCanvas()
        blankCanvas.configureTool(toolSheet.toolId, toolSheet.values)
        if (root.visible) blankCanvas.forceActiveFocus()
    }
    function resizeCanvas() {
        const resized = blankCanvas.width !== canvasSurface.width || blankCanvas.height !== canvasSurface.height
        blankCanvas.width = canvasSurface.width
        blankCanvas.height = canvasSurface.height
        if (resized && blankCanvas.documentReady) blankCanvas.fitToView()
    }
    Connections {
        target: canvasProject
        function onCurrentCanvasChanged() { root.mountCanvas() }
    }
    Connections {
        target: blankCanvas
        function onHistoryRestored(settings) { toolSheet.settingsByTool = settings; toolSheet.notice = "" }
    }
    function requestSource(tool, values) {
        sourceDialog.targetTool = tool
        sourceDialog.settings = values
        const folder = blankCanvas.localDirectory(tool === "file" ? values["field-0"] || "" : values["field-8"] || "")
        if (folder.toString().length) sourceDialog.currentFolder = folder
        sourceDialog.nameFilters = tool === "audio-track" ? [qsTr("WAV audio (*.wav)")]
            : tool === "asset" ? [qsTr("Media (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff *.svg *.wav *.mp4 *.mov *.mkv *.webm)")]
            : [qsTr("Images and vectors (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff *.svg)")]
        sourceDialog.open()
    }
    function showPreview(tool, field, values) {
        previewTool = tool
        previewField = field
        selectedPath = -1
        const snapshot = Object.assign({}, values)
        if (generationRuntime) snapshot.libraryDirectory = generationRuntime.containerPath
        previewValues = snapshot
        previewData = blankCanvas.toolPreview(tool, field, snapshot)
        previewSheet.open()
    }
    function submitGeneration(tool, field, values) {
        if (!generationRuntime) { toolSheet.notice = qsTr("The generation runtime is not attached."); return }
        let operation = field === "field-14" ? "Outpaint" : field === "field-10" || tool === "retouch" || tool === "eraser" ? "Inpaint" : field === "field-7" ? "Image-to-image" : values.selector || "Text-to-image"
        let recipe = values
        if (tool === "background") { operation = "Background"; recipe = {"field-0": values["field-12"]} }
        if (tool === "fill") { operation = "Fill"; recipe = {"field-0": values["field-12"], "field-5": values["field-13"]} }
        if (tool === "retouch") recipe = {"field-0": values["field-5"], "field-5": values["field-6"]}
        if (tool === "eraser") recipe = toolSheet.settingsByTool.generative || Definitions.defaults(Definitions.tool("generative"))
        const parameters = blankCanvas.generationParameters(operation, recipe)
        if (!Object.keys(parameters).length) { toolSheet.notice = blankCanvas.error; return }
        generationOperation = operation
        generationTargetCanvas = blankCanvas
        submitting = true
        const id = generationRuntime.enqueueAdvanced(parameters)
        submitting = false
        toolSheet.notice = id.length ? qsTr("Generation queued. Open results to place a variation.") : generationRuntime.errorString
    }
    function handleToolAction(toolId, fieldId, values) {
        toolActionRequested(toolId, fieldId, values)
        toolSheet.notice = ""
        const definition = Definitions.tool(toolId)
        if (fieldId === "capabilities") {
            previewTool = "capabilities"
            previewData = {title: qsTr("Tool availability"), description: qsTr("Required document, device and runtime capabilities"), items: definition.fields.map(function(f) { const c = blankCanvas.toolControlState(toolId, f.id, values); return {label: f.label, description: c.reason || qsTr("Connected"), enabled: false} }).filter(function(f) { return f.description !== qsTr("Connected") })}
            previewSheet.open()
            return
        }
        const field = definition.fields.find(function(entry) { return entry.id === fieldId })
        if (field && field.type === "Visual") { showPreview(toolId, fieldId, values); return }
        if (toolId === "file" && fieldId === "field-3") documentControls.requestOpen(values["field-2"], blankCanvas.localDirectory(values["field-0"] || ""))
        else if (toolId === "file" && fieldId === "field-6") requestSource("file", values)
        else if (toolId === "file" && fieldId === "field-12") {
            if (values["field-1"] === "IISC" && values["field-11"]) documentControls.requestSaveAs(values["field-10"], blankCanvas.localDirectory(values["field-0"] || ""))
            else { const folder = blankCanvas.localDirectory(values["field-0"] || ""); if (folder.toString().length) exportDialog.currentFolder = folder; exportDialog.nameFilters = [values["field-1"] + " (*." + String(values["field-1"]).toLowerCase() + ")"]; exportDialog.format = values["field-1"]; exportDialog.selectedFile = values["field-10"] + "." + String(values["field-1"]).toLowerCase(); exportDialog.open() }
        }
        else if (toolId === "camera-photo" && fieldId === "field-7") requestSource(toolId, values)
        else if (toolId === "audio-track" && fieldId === "field-3") requestSource(toolId, values)
        else if (toolId === "asset" && fieldId === "field-15") showPreview(toolId, fieldId, values)
        else if (toolId === "layers" && ["field-11", "field-15"].indexOf(fieldId) >= 0) showPreview(toolId, fieldId, values)
        else if (toolId === "asset" && fieldId === "field-11") requestSource(toolId, values)
        else if (toolId === "brush" && fieldId === "field-20") documentControls.showPaintColor()
        else if (toolId === "brush" && fieldId === "field-3") showPreview(toolId, "field-2", values)
        else if (toolId === "generative" && ["field-3", "field-7", "field-10", "field-14"].indexOf(fieldId) >= 0 || toolId === "background" && fieldId === "field-15" && values["field-13"] === "Generate" || toolId === "fill" && fieldId === "field-15" || toolId === "retouch" && (fieldId === "field-7" || fieldId === "field-3" && values["field-2"] === "Generate") || toolId === "eraser" && fieldId === "field-7" && values["field-6"] === "Generate") submitGeneration(toolId, fieldId, values)
        else if (toolId === "background" && fieldId === "field-15" && values["field-13"] !== "Generate") requestSource(toolId, values)
        else if (toolId === "generative" && fieldId === "field-22") requestSource("reference", values)
        else if (toolId === "layers" && fieldId === "field-3") submitGeneration("generative", "field-7", toolSheet.settingsByTool.generative || Definitions.defaults(Definitions.tool("generative")))
        else if (!blankCanvas.executeToolAction(toolId, fieldId, values)) toolSheet.notice = blankCanvas.error
        else toolSheet.notice = blankCanvas.toolHint
    }
    Component.onCompleted: {
        if (!blankCanvas.documentReady) createBlankCanvas({width: 1024, height: 1024, unit: "px", background: "White"})
        blankCanvas.configureTool(toolSheet.toolId, toolSheet.values)
        mountCanvas()
    }
    onVisibleChanged: if (!visible) { toolSheet.dismiss(); playback.stop(); blankCanvas.media.stop() }
    onMobileLayoutChanged: if (!mobileLayout) toolSheet.dismiss()

    Rectangle {
        anchors.fill: parent
        visible: !root.mobileLayout
        color: LV.Theme.panelBackground05
    }
    DesktopHomeSidebar {
        id: sidebar
        objectName: "editorSidebar"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: listWidth
        visible: !root.mobileLayout
        compact: root.compactSidebar
        editorLayout: true
        selectedAction: ""
        onActionRequested: function(action) {
            if (action === "home") root.backRequested()
            else root.navigationRequested(action)
        }
    }

    LV.HStack {
        id: toolbar
        visible: root.mobileLayout
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: LV.Theme.gap10
        height: implicitHeight
        spacing: LV.Theme.gap12

        LV.LabelButton {
            objectName: "editorBackButton"
            text: qsTr("Back")
            tone: LV.AbstractButton.Default
            onClicked: root.backRequested()
        }
        LV.Label {
            text: qsTr("Editor")
            style: header
            Layout.fillWidth: true
        }
        LV.Label {
            text: root.canvasSpecification.pixelWidth
                ? root.canvasSpecification.name + " · " + root.canvasSpecification.pixelWidth + " × " + root.canvasSpecification.pixelHeight + " px"
                : qsTr("Untitled Canvas")
            style: description
            Layout.maximumWidth: root.width * 0.6
            elide: Text.ElideRight
        }
    }

    Item {
        id: workspace
        objectName: "editorWorkspace"
        anchors.top: root.mobileLayout ? toolbar.bottom : bottomToolbar.bottom
        anchors.bottom: root.mobileLayout ? bottomToolbar.top : parent.bottom
        anchors.left: root.mobileLayout ? parent.left : sidebar.right
        anchors.right: desktopDock.visible ? desktopDock.left : parent.right
        anchors.margins: root.mobileLayout ? LV.Theme.gap16 : 0
        clip: true

        EditorDocumentControls {
            id: documentControls
            objectName: "editorDocumentControls"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            anchors.topMargin: height > 0 ? 8 : 0
            height: implicitHeight
            canvas: blankCanvas
            project: canvasProject
            mobileLayout: root.mobileLayout
            showCommandBar: root.mobileLayout
            onOpenDocumentRequested: function(source, asCopy) { root.openDocumentSource(source, asCopy) }
        }
        Item {
            id: canvasSurface
            transform: Scale {
                origin.x: canvasSurface.width / 2
                origin.y: canvasSurface.height / 2
                xScale: blankCanvas.previewMirrorX ? -1 : 1
                yScale: blankCanvas.previewMirrorY ? -1 : 1
            }
            objectName: "editorCanvasSurface"
            anchors.top: documentControls.bottom
            anchors.topMargin: documentControls.height > 0 ? 8 : 0
            anchors.bottom: frameControls.top
            anchors.left: parent.left
            anchors.right: parent.right
            Image {
                objectName: "editorSelectionOverlay"
                z: 10
                x: blankCanvas.panX
                y: blankCanvas.panY
                width: blankCanvas.canvasWidth * blankCanvas.zoom
                height: blankCanvas.canvasHeight * blankCanvas.zoom
                source: blankCanvas.selectionOverlay
                visible: source.toString().length > 0
                cache: false
                smooth: true
            }
            Image {
                z: 11
                x: blankCanvas.panX; y: blankCanvas.panY
                width: blankCanvas.canvasWidth * blankCanvas.zoom; height: blankCanvas.canvasHeight * blankCanvas.zoom
                source: blankCanvas.toolState.eraserOverlay || ""
                visible: root.selectedTool === "eraser"
                smooth: true
            }
            Image {
                objectName: "editorClippingOverlay"
                z: 12
                x: blankCanvas.panX; y: blankCanvas.panY
                width: blankCanvas.canvasWidth * blankCanvas.zoom; height: blankCanvas.canvasHeight * blankCanvas.zoom
                source: blankCanvas.toolState.clippingOverlay || ""
                visible: root.selectedTool === "color"
                cache: false
                smooth: true
            }
            Rectangle {
                objectName: "editorCanvasGuide"
                z: 13
                readonly property var guide: blankCanvas.toolState.canvasGuide || ({})
                readonly property real insetX: guide.mode === "Safe" ? guide.safeX || 0 : guide.mode === "Bleed" ? -(guide.bleed || 0) : 0
                readonly property real insetY: guide.mode === "Safe" ? guide.safeY || 0 : guide.mode === "Bleed" ? -(guide.bleed || 0) : 0
                x: blankCanvas.panX + insetX * blankCanvas.zoom
                y: blankCanvas.panY + insetY * blankCanvas.zoom
                width: Math.max(0, blankCanvas.canvasWidth - insetX * 2) * blankCanvas.zoom
                height: Math.max(0, blankCanvas.canvasHeight - insetY * 2) * blankCanvas.zoom
                visible: root.selectedTool === "canvas"
                color: "transparent"
                border.width: 1
                border.color: guide.mode === "Bleed" ? "#FF5F57" : guide.mode === "Safe" ? "#28C840" : "#8B7CFF"
            }
            MouseArea {
                objectName: "editorCanvasContextArea"
                anchors.fill: parent
                z: 20
                enabled: !root.mobileLayout && !root.modalActive
                acceptedButtons: Qt.RightButton
                onClicked: function(mouse) { documentControls.openContextMenu(canvasSurface, mouse.x, mouse.y) }
            }
            onWidthChanged: root.resizeCanvas()
            onHeightChanged: root.resizeCanvas()
        }
        LV.HStack {
            id: frameControls
            objectName: "editorFrameControls"
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: visible ? 32 : 0
            visible: blankCanvas.frameCount > 1
            spacing: 8
            LV.LabelButton {
                text: playback.running ? qsTr("Pause") : qsTr("Play")
                onClicked: {
                    if (playback.running) { playback.stop(); blankCanvas.media.stop(); return }
                    if (blankCanvas.toolState.audioTracks > 0 && !blankCanvas.playTimelineAudio()) { toolSheet.notice = blankCanvas.error; return }
                    playback.start()
                }
            }
            LV.Slider {
                Layout.fillWidth: true
                from: 0; to: Math.max(1, blankCanvas.frameCount - 1); stepSize: 1; value: blankCanvas.frame
                onMoved: { playback.stop(); blankCanvas.media.stop(); blankCanvas.frame = Math.round(value) }
            }
            LV.Label { text: qsTr("%1 / %2").arg(blankCanvas.frame + 1).arg(blankCanvas.frameCount); style: description }
        }
        Timer {
            id: playback
            interval: Math.max(1, 1000 / blankCanvas.toolState.frameRate)
            repeat: true
            property int firstFrame: 0
            property double startedAt: 0
            onRunningChanged: if (running) { firstFrame = blankCanvas.frame; startedAt = Date.now() }
            onTriggered: {
                const next = firstFrame + Math.floor((Date.now() - startedAt) / 1000 * blankCanvas.toolState.frameRate)
                if (next >= blankCanvas.frameCount) { stop(); blankCanvas.media.stop(); blankCanvas.frame = 0 }
                else blankCanvas.frame = next
            }
        }
    }
    LV.Label {
        objectName: "editorToolStatus"
        anchors.left: workspace.left; anchors.right: workspace.right; anchors.bottom: workspace.bottom
        anchors.margins: 8
        text: toolSheet.notice || blankCanvas.toolHint
        style: description
        wrapMode: Text.Wrap
        sizeToContentHeight: true
        visible: text.length > 0
    }
    Loader {
        id: desktopDock
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: Math.min(root.desktopPanelMaximumWidth,
            Math.max(root.desktopPanelMinimumWidth, root.desktopPanelWidth))
        visible: !root.mobileLayout && root.desktopPanelExpanded
        active: !root.mobileLayout
        sourceComponent: EditorDesktopPanel {
            toolState: toolSheet
            onCloseRequested: root.desktopPanelExpanded = false
            onActionRequested: function(toolId, fieldId, values) { root.handleToolAction(toolId, fieldId, values) }
        }
    }
    MouseArea {
        id: panelResizeHandle
        objectName: "editorDesktopPanelResizeHandle"
        anchors.top: desktopDock.top
        anchors.bottom: desktopDock.bottom
        x: desktopDock.x - width / 2
        width: 10
        z: 20
        visible: desktopDock.visible
        enabled: visible && !root.modalActive
        hoverEnabled: true
        preventStealing: true
        cursorShape: Qt.SizeHorCursor
        property real pressX: 0
        property real pressWidth: 0
        onPressed: function(mouse) {
            pressX = mapToItem(root, mouse.x, mouse.y).x
            pressWidth = desktopDock.width
        }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const requested = pressWidth + pressX - mapToItem(root, mouse.x, mouse.y).x
            root.desktopPanelWidth = Math.min(root.desktopPanelMaximumWidth,
                Math.max(root.desktopPanelMinimumWidth, requested))
        }
        onDoubleClicked: root.desktopPanelWidth = 342
    }
    EditorToolbar {
        id: bottomToolbar
        anchors.left: root.mobileLayout ? parent.left : sidebar.right
        anchors.right: desktopDock.visible ? desktopDock.left : parent.right
        anchors.top: root.mobileLayout ? undefined : parent.top
        anchors.bottom: root.mobileLayout ? parent.bottom : undefined
        anchors.leftMargin: root.mobileLayout ? LV.Theme.gap8 : 0
        anchors.rightMargin: root.mobileLayout ? LV.Theme.gap8 : 0
        anchors.bottomMargin: root.mobileLayout ? LV.Theme.gap8 : 0
        height: implicitHeight
        mobileLayout: root.mobileLayout
        onToolSelected: function(toolId) {
            root.toolSelected(toolId)
            if (root.mobileLayout && root.visible) toolSheet.openTool(toolId)
            else if (!root.mobileLayout) {
                const sameTool = toolSheet.toolId === toolId
                toolSheet.dismiss()
                toolSheet.toolId = toolId
                root.desktopPanelExpanded = sameTool ? !root.desktopPanelExpanded : true
            }
        }
    }
    EditorToolSheet {
        id: toolSheet
        parent: root
        engine: blankCanvas
        generationRuntime: root.generationRuntime
        onResetRequested: function(toolId, defaults) { if (!blankCanvas.resetTool(toolId, defaults)) notice = blankCanvas.error }
        onValuesChanged: blankCanvas.configureTool(toolId, values)
        onValueEdited: function(toolId, fieldId, value) {
            if (!blankCanvas.applyToolField(toolId, fieldId, value)) notice = blankCanvas.error
            else notice = ""
            if (toolId === "elements" && fieldId === "field-2" && value === "Image") root.requestSource("elements", values)
        }
        onActionRequested: function(toolId, fieldId, values) { root.handleToolAction(toolId, fieldId, values) }
    }
    Dialogs.FileDialog {
        id: sourceDialog
        objectName: "editorSourceDialog"
        property string targetTool: "file"
        property var settings: ({})
        onAccepted: {
            if (targetTool === "reference") {
                toolSheet.setValue("field-4", selectedFile.toString())
            } else if (!blankCanvas.importToolSource(selectedFile, targetTool, settings)) toolSheet.notice = blankCanvas.error
            else if (targetTool === "audio-track") {
                const authoritative = blankCanvas.toolValues("audio-track")
                const all = Object.assign({}, toolSheet.settingsByTool)
                all["audio-track"] = authoritative
                toolSheet.settingsByTool = all
            }
        }
    }
    Dialogs.FileDialog {
        id: exportDialog
        objectName: "editorExportDialog"
        property string format: "PNG"
        fileMode: Dialogs.FileDialog.SaveFile
        onAccepted: if (!blankCanvas.exportImage(selectedFile, format)) toolSheet.notice = blankCanvas.error
    }
    Connections {
        target: root.generationRuntime
        function onSubmissionQueued(ids) { if (root.submitting) root.editorJobIds = ids.slice() }
    }
    LV.Sheet {
        id: previewSheet
        objectName: "editorToolPreviewSheet"
        parent: root
        presentation: LV.Sheet.Mobile
        title: root.previewData.title || qsTr("Preview")
        description: root.previewData.description || ""
        contentPadding: 16
        contentComponent: Column {
            spacing: 12
            width: parent ? parent.width : 380
            Image {
                width: parent.width
                height: visible ? Math.min(320, width) : 0
                visible: source.toString().length > 0
                source: root.previewData.imageSource || ""
                fillMode: Image.PreserveAspectFit
                smooth: true
                cache: false
            }
            Repeater {
                model: root.previewData.items || []
                LV.ListItem {
                    required property var modelData
                    width: parent.width
                    type: LV.ListItem.Navigation
                    label: modelData.label || ""
                    description: modelData.description || ""
                    showDescription: description.length > 0
                    showLeadingIcon: false
                    enabled: modelData.enabled !== false && Boolean(modelData.action)
                    onClicked: {
                        if (modelData.action === "selectLayer") blankCanvas.selectLayer(modelData.id)
                        else if (modelData.action === "brushPreset") { toolSheet.setValue("field-1", modelData.value); toolSheet.setValue("field-5", modelData.value === "Paint" ? 60 : 100) }
                        else if (modelData.action === "filterPreset") toolSheet.setValue("field-1", modelData.value)
                        else if (modelData.action === "palette") toolSheet.setValue(modelData.field || "field-1", modelData.value)
                        else if (modelData.action === "editPath") root.selectedPath = modelData.index
                        else if (modelData.action === "job") root.generationRuntime.cancel(modelData.id)
                        else if (modelData.action === "region") { const region = {selector: "Region", "field-14": modelData.label, "field-15": modelData.label}; blankCanvas.configureTool("masking", region); blankCanvas.executeToolAction("masking", "field-13", region); blankCanvas.configureTool(toolSheet.toolId, toolSheet.values) }
                        else if (modelData.action === "importAsset") blankCanvas.importToolSource(modelData.source, "asset", toolSheet.values)
                        else if (modelData.action === "applyMask") blankCanvas.executeToolAction("layers", "field-23", toolSheet.values)
                        else if (modelData.action === "clearSelection") blankCanvas.clearAreaSelection()
                        else if (modelData.action === "playAudio") blankCanvas.playAudio()
                        else if (modelData.action === "stopAudio") blankCanvas.media.stop()
                        if (root.previewTool !== "generation-results") root.previewData = blankCanvas.toolPreview(root.previewTool, root.previewField, root.previewValues)
                    }
                }
            }
            LV.HStack {
                visible: root.selectedPath >= 0 && root.previewTool === "layers"
                width: parent.width
                spacing: 8
                LV.InputField { id: pathX; text: "0"; placeholderText: qsTr("X offset"); Layout.fillWidth: true }
                LV.InputField { id: pathY; text: "0"; placeholderText: qsTr("Y offset"); Layout.fillWidth: true }
                LV.LabelButton { text: qsTr("Move path"); onClicked: if (blankCanvas.moveVectorPath(root.selectedPath, Number(pathX.text), Number(pathY.text))) root.previewData = blankCanvas.toolPreview("layers", "field-15", toolSheet.values) }
            }
            LV.Slider {
                visible: root.previewTool === "color" && root.previewField === "field-44"
                width: parent.width
                from: -100; to: 100; value: 0
                onMoved: {
                    toolSheet.setValue("curveAmount", value)
                    root.previewData = blankCanvas.toolPreview("color", "field-44", toolSheet.values)
                }
            }
            Repeater {
                model: root.generationRuntime ? root.generationRuntime.completedResults.filter(function(result) { return root.editorJobIds.indexOf(result.id) >= 0 }) : []
                LV.ListItem {
                    required property var modelData
                    width: parent.width
                    type: LV.ListItem.Navigation
                    label: qsTr("Place variation: %1").arg(modelData.id)
                    showLeadingIcon: false
                    onClicked: if (root.generationTargetCanvas && root.generationTargetCanvas.placeGenerated(modelData.imageSource, root.generationOperation)) previewSheet.close()
                }
            }
            LV.LabelButton { text: qsTr("Close"); onClicked: previewSheet.close() }
        }
    }
    LV.LabelButton {
        objectName: "editorGenerationResults"
        anchors.right: workspace.right
        anchors.top: workspace.top
        anchors.topMargin: 44
        visible: root.editorJobIds.length > 0
        text: qsTr("Generation results")
        onClicked: { root.previewData = ({title: qsTr("Generation results"), description: toolSheet.notice, items: root.editorJobIds.map(function(id) { const job = root.generationRuntime.jobs.find(function(j) { return j.id === id }); const pending = job && ["queued", "running"].indexOf(String(job.state).toLowerCase()) >= 0; return {id: id, label: pending ? qsTr("Cancel %1").arg(id) : id, description: job ? job.state + (job.error ? ": " + job.error : "") : "", action: "job", enabled: pending} })}); root.previewTool = "generation-results"; previewSheet.open() }
    }
    LV.LabelButton {
        objectName: "editorAdvancedGeneration"
        anchors.right: workspace.right
        anchors.top: workspace.top
        anchors.topMargin: 72
        visible: toolSheet.toolId === "generative"
        enabled: Boolean(root.generationRuntime)
        text: qsTr("Advanced generation")
        onClicked: advancedSheet.open()
    }
    LV.Sheet {
        id: advancedSheet
        objectName: "editorAdvancedGenerationSheet"
        parent: root
        presentation: LV.Sheet.Mobile
        title: qsTr("Advanced generation")
        contentComponent: AdvancedGenerate {
            width: parent ? parent.width : 402
            models: root.generationRuntime ? root.generationRuntime.models : []
            vaes: root.generationRuntime ? root.generationRuntime.vaes : []
            onGenerateRequested: function(parameters) {
                root.generationTargetCanvas = blankCanvas
                root.generationOperation = "Text-to-image"
                root.submitting = true
                const id = root.generationRuntime.enqueueAdvanced(parameters)
                root.submitting = false
                toolSheet.notice = id.length ? qsTr("Generation queued. Open results to place a variation.") : root.generationRuntime.errorString
                if (id.length) advancedSheet.close()
            }
        }
    }
    Shortcut { sequence: StandardKey.Open; enabled: root.visible && !root.modalActive; onActivated: documentControls.requestOpen() }
    Shortcut { sequence: StandardKey.Save; enabled: root.visible && !root.modalActive; onActivated: documentControls.requestSave() }
    Shortcut { sequence: StandardKey.SaveAs; enabled: root.visible && !root.modalActive; onActivated: documentControls.requestSaveAs() }
    Shortcut { sequences: ["Alt+Left", "PgUp"]; enabled: root.visible && (root.activeFocus || blankCanvas.activeFocus) && !root.modalActive && canvasProject.currentIndex > 0; onActivated: canvasProject.setCurrentIndex(canvasProject.currentIndex - 1) }
    Shortcut { sequences: ["Alt+Right", "PgDown"]; enabled: root.visible && (root.activeFocus || blankCanvas.activeFocus) && !root.modalActive && canvasProject.currentIndex + 1 < canvasProject.canvasCount; onActivated: canvasProject.setCurrentIndex(canvasProject.currentIndex + 1) }
    Shortcut { sequences: [StandardKey.Undo]; enabled: root.visible && blankCanvas.activeFocus && !root.modalActive && blankCanvas.canUndo; onActivated: blankCanvas.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: root.visible && blankCanvas.activeFocus && !root.modalActive && blankCanvas.canRedo; onActivated: blankCanvas.redo() }
    Shortcut {
        sequence: "Escape"
        enabled: root.visible && !root.mobileLayout && root.desktopPanelExpanded && !root.modalActive
        onActivated: root.desktopPanelExpanded = false
    }
    DropArea {
        anchors.fill: workspace
        onDropped: function(drop) {
            if (drop.urls.length === 1 && root.openDocumentSource(drop.urls[0])) drop.acceptProposedAction()
        }
    }
    Keys.onEscapePressed: {
        if (previewSheet.visible) previewSheet.close()
        else if (advancedSheet.visible) advancedSheet.close()
        else if (sourceDialog.visible) sourceDialog.close()
        else if (exportDialog.visible) exportDialog.close()
        else if (toolSheet.visible) toolSheet.dismiss()
        else if (!root.mobileLayout && root.desktopPanelExpanded) root.desktopPanelExpanded = false
        else root.backRequested()
    }
}
