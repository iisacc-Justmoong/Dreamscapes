pragma ComponentBehavior: Bound
import QtQuick
import ".." as Frames
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import LVRS 1.0 as LV
import ".." as Views
import Dreamscapes.Storage 1.0
import "../Home"

Item {
    id: root
    objectName: "canvasEditor"
    property var generationRuntime: null
    property alias editorJobIds: nativeWorkspace.editorJobIds
    property alias submitting: nativeWorkspace.submitting
    property alias generationOperation: nativeWorkspace.generationOperation
    property alias previewData: nativeWorkspace.previewData
    property alias previewTool: nativeWorkspace.previewTool
    property alias previewField: nativeWorkspace.previewField
    property alias previewValues: nativeWorkspace.previewValues
    property bool applicationModalActive: false
    property bool applicationSearchActive: false
    readonly property bool modalActive: applicationModalActive || viewBar.modalActive || toolSheet.modalActive || documentControls.modalActive || previewSheet.visible || sourceDialog.visible || exportDialog.visible || advancedSheet.visible || editorTimeline.modalActive || (desktopDock.item ? desktopDock.item.backgroundModalActive || desktopDock.item.layerModalActive || desktopDock.item.graphicModalActive || desktopDock.item.elementsModalActive || desktopDock.item.photoModalActive : false)
    property alias selectedPath: nativeWorkspace.selectedPath
    property alias generationTargetCanvas: nativeWorkspace.generationTargetCanvas
    property url imageSource: ""
    property var generationResult: ({})
    property var generationResults: []
    property var canvasSpecification: ({})
    property bool initializeBlankCanvas: true
    readonly property alias project: canvasProject
    readonly property var blankCanvas: canvasProject.currentCanvas
    readonly property string documentError: canvasProject.error
    property alias generationResultsByArtboard: nativeWorkspace.generationResultsByArtboard
    readonly property var sourceArtboards: canvasProject ? canvasProject.artboards : []
    readonly property string activeArtboardId: canvasProject && sourceArtboards[canvasProject.currentIndex] ? sourceArtboards[canvasProject.currentIndex].id : ""
    readonly property alias activeGenerationResult: nativeWorkspace.activeGenerationResult
    NativeEditorWorkspace {
        id: nativeWorkspace
        objectName: "editorNativeWorkspace"
        project: canvasProject
        imageImporter: root.imageImportRequest
        toolState: toolSheet.nativeModel
        generation: root.generationRuntime
        generationResult: root.generationResult
        generationResults: root.generationResults
        onSourceRequested: (tool, values, directory, filters) => {
            sourceDialog.targetTool = tool
            sourceDialog.settings = values
            if (directory.toString().length) sourceDialog.currentFolder = directory
            sourceDialog.nameFilters = filters
            sourceDialog.open()
        }
        onPreviewRequested: previewSheet.open()
        onPreviewCloseRequested: previewSheet.close()
        onOpenDocumentRequested: (copy, directory) => documentControls.requestOpen(copy, directory)
        onSaveAsRequested: (name, directory) => documentControls.requestSaveAs(name, directory)
        onPaintColorRequested: documentControls.showPaintColor()
        onExportRequested: (format, name, directory) => {
            if (directory.toString().length) exportDialog.currentFolder = directory
            exportDialog.nameFilters = [format + " (*." + format.toLowerCase() + ")"]
            exportDialog.format = format
            exportDialog.selectedFile = name + "." + format.toLowerCase()
            exportDialog.open()
        }
    }
    function syncGenerationMetadata() { nativeWorkspace.syncGenerationMetadata() }
    function createBlankCanvas(specification) { return nativeWorkspace.createCanvas(specification) }
    function openDocumentSource(source, asCopy) { return nativeWorkspace.openDocument(source,Boolean(asCopy)) }
    readonly property bool documentOpening: canvasProject.opening
    function cancelDocumentOpen() { canvasProject.cancelDocumentOpen() }
    function saveDocumentAs(destination) { return canvasProject.saveDocumentAs(destination) }
    function openImages(sources, entries) { return nativeWorkspace.openImages(sources,entries || []) }
    function localFileSource(path) { return blankCanvas.localFileSource(path) }
    property bool mobileLayout: false
    readonly property string selectedTool: toolToolbar.currentTool
    property bool desktopPanelExpanded: true
    // Retain the requested width when a smaller window temporarily clamps it.
    readonly property real desktopPanelDefaultWidth: 354
    property real desktopPanelWidth: desktopPanelDefaultWidth
    readonly property real desktopPanelMinimumWidth: 280
    readonly property real desktopPanelMaximumWidth: Math.min(720,
        Math.max(desktopPanelMinimumWidth, width - sidebar.width - toolToolbar.implicitWidth - 320),
        Math.max(0, width - sidebar.width - toolToolbar.implicitWidth))
    readonly property bool compactSidebar: width < LV.Theme.scaleMetric(700)
    signal backRequested()
    signal publishRequested()
    signal navigationRequested(string action)
    signal toolSelected(string toolId)
    signal toolActionRequested(string toolId, string fieldId, var values)
    signal documentOpened(url source)
    signal documentOpenFailed(string message)
    signal documentOpenCancelled()
    signal imagesImported(var entries, var sources, bool attached)
    signal imageImportFailed(string message)
    EditorProject {
        id: canvasProject
        objectName: "editorProject"
        container: root.generationRuntime ? root.generationRuntime.container : null
        onDocumentOpened: function(source) { root.documentOpened(source) }
        onDocumentOpenFailed: function(message) { root.documentOpenFailed(message) }
        onDocumentOpenCancelled: root.documentOpenCancelled()
    }
    readonly property alias imageImportRequest: imageImporter
    EditorImageImportRequest {
        id: imageImporter
        objectName: "editorImageImportRequest"
        project: canvasProject
        container: root.generationRuntime ? root.generationRuntime.container : null
        onReady: function(entries, sources, attached) { root.imagesImported(entries, sources, attached) }
        onFailed: function(message) { root.imageImportFailed(message) }
        onRejected: function(message) { root.imageImportFailed(message) }
    }
    function syncPagePresentation() {
        root.blankCanvas.pagePresentation = canvasProject.canvasCount > 1 && !root.blankCanvas.infiniteCanvas
    }
    function mountCanvas() {
        syncPagePresentation()
        canvasProject.attachCanvas(nativeCanvasHost)
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
        function onCurrentCanvasChanged() {
            nativeWorkspace.syncGenerationMetadata()
            root.mountCanvas()
            Qt.callLater(root.syncGenerationMetadata)
        }
        function onProjectChanged() { Qt.callLater(root.syncPagePresentation) }
    }
    Connections {
        target: blankCanvas
        function onHistoryRestored(settings) { toolSheet.nativeModel.restoreSettings(settings) }
    }

    function requestSource(tool, values) { nativeWorkspace.requestSource(tool, values) }
    function showPreview(tool, field, values) { nativeWorkspace.showPreview(tool, field, values) }
    function submitGeneration(tool, field, values) { nativeWorkspace.submitGeneration(tool, field, values) }
    function handleToolAction(toolId, fieldId, values) { toolActionRequested(toolId, fieldId, values); nativeWorkspace.handleToolAction(toolId, fieldId, values) }
    Component.onCompleted: {
        if (initializeBlankCanvas && !blankCanvas.documentReady) createBlankCanvas({width: 1024, height: 1024, unit: "px", background: "White"})
        blankCanvas.configureTool(toolSheet.toolId, toolSheet.values)
        mountCanvas()
        syncGenerationMetadata()
    }
    onVisibleChanged: if (!visible) { canvasProject.cancelDocumentOpen(); imageImporter.cancel(); toolSheet.dismiss(); editorTimeline.playing = false; blankCanvas.media.stop(); blankCanvas.photoSources.cancel() }
    onMobileLayoutChanged: if (!mobileLayout) toolSheet.dismiss()

    DesktopHomeSidebar {
        id: sidebar
        objectName: "editorSidebar"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: listWidth
        visible: !root.mobileLayout
        compact: root.compactSidebar
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

    EditorViewBar {
        id: viewBar
        anchors.top: root.top; anchors.left: sidebar.right; anchors.right: root.right
        visible: !root.mobileLayout
        height: root.mobileLayout ? 0 : implicitHeight
        canvas: root.blankCanvas
        onPublishRequested: root.publishRequested()
    }
    Item {
        id: workspace
        objectName: "editorWorkspace"
        anchors.top: root.mobileLayout ? toolbar.bottom : viewBar.bottom
        anchors.bottom: timelineDock.top
        anchors.left: root.mobileLayout ? parent.left : sidebar.right
        anchors.right: root.mobileLayout ? parent.right : toolToolbar.left
        anchors.margins: root.mobileLayout ? LV.Theme.gap16 : 0
        clip: true

        EditorDocumentControls {
            id: documentControls
            objectName: "editorDocumentControls"
            defaultSaveDirectory: root.generationRuntime ? root.generationRuntime.filesDirectory : ""
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            anchors.topMargin: height > 0 ? 8 : 0
            height: implicitHeight
            canvas: blankCanvas
            project: canvasProject
            imageImporter: root.imageImportRequest
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
            anchors.bottom: pageStrip.top
            anchors.left: parent.left
            anchors.right: parent.right
            clip: true
            Rectangle {
                objectName: "editorWorkAreaBackground"
                anchors.fill: parent
                visible: !root.mobileLayout
                color: LV.Theme.panelBackground05
            }
            EditorPageDeck {
                anchors.fill: parent
                project: canvasProject
                canvas: root.blankCanvas
                activePage: activePageClip
                navigationEnabled: !root.modalActive
            }
            Rectangle {
                z: 199
                x: root.blankCanvas.panX + (root.blankCanvas.editingOrigin.x - root.blankCanvas.canvasOriginX) * root.blankCanvas.zoom
                y: root.blankCanvas.panY + (root.blankCanvas.editingOrigin.y - root.blankCanvas.canvasOriginY) * root.blankCanvas.zoom
                width: root.blankCanvas.editingWidth * root.blankCanvas.zoom
                height: root.blankCanvas.editingHeight * root.blankCanvas.zoom
                color: LV.Theme.surfaceInput
                Image { anchors.fill: parent; visible: root.blankCanvas.viewOptions.display.transparency; source: "qrc:/editor/view-options/checker.svg"; fillMode: Image.Tile }
            }
            Item {
                id: activePageClip
                objectName: "editorActivePageClip"
                z: 200
                x: root.blankCanvas.pagePresentation ? root.blankCanvas.panX + (root.blankCanvas.editingOrigin.x - root.blankCanvas.canvasOriginX) * root.blankCanvas.zoom : 0
                y: root.blankCanvas.pagePresentation ? root.blankCanvas.panY + (root.blankCanvas.editingOrigin.y - root.blankCanvas.canvasOriginY) * root.blankCanvas.zoom : 0
                width: root.blankCanvas.pagePresentation ? root.blankCanvas.editingWidth * root.blankCanvas.zoom : canvasSurface.width
                height: root.blankCanvas.pagePresentation ? root.blankCanvas.editingHeight * root.blankCanvas.zoom : canvasSurface.height
                clip: root.blankCanvas.pagePresentation
                Rectangle {
                    anchors.fill: parent
                    visible: root.blankCanvas.pagePresentation
                    color: root.blankCanvas.viewOptions.display.transparency ? "transparent" : LV.Theme.panelBackground06
                }
                // The host remains viewport-sized. Its offset cancels the clip
                // position, preserving native input and overlay coordinates.
                Item {
                    id: nativeCanvasHost
                    objectName: "editorNativeCanvasHost"
                    x: -activePageClip.x
                    y: -activePageClip.y
                    width: canvasSurface.width
                    height: canvasSurface.height
                    opacity: root.blankCanvas.viewOptions.display.outlines || (root.blankCanvas.graphicsBackend === "software" && root.blankCanvas.viewOptions.display.channel !== "RGB") ? 0 : 1
                    layer.enabled: root.blankCanvas.graphicsBackend !== "software" && root.blankCanvas.viewOptions.display.channel !== "RGB"
                    layer.effect: ShaderEffect {
                        property var source
                        property int channel: NativeData.presentation.indexOf(["RGB","Red","Green","Blue","Alpha"], root.blankCanvas.viewOptions.display.channel)
                        property vector4d canvasRect: Qt.vector4d(
                            (root.blankCanvas.panX + (root.blankCanvas.editingOrigin.x - root.blankCanvas.canvasOriginX) * root.blankCanvas.zoom) / Math.max(1, width),
                            (root.blankCanvas.panY + (root.blankCanvas.editingOrigin.y - root.blankCanvas.canvasOriginY) * root.blankCanvas.zoom) / Math.max(1, height),
                            root.blankCanvas.editingWidth * root.blankCanvas.zoom / Math.max(1, width),
                            root.blankCanvas.editingHeight * root.blankCanvas.zoom / Math.max(1, height))
                        fragmentShader: "qrc:/editor/shaders/channel.frag.qsb"
                    }
                }
                EditorAlignmentOverlay {
                    objectName: "editorSoftwareChannelOverlay"
                    x: -activePageClip.x; y: -activePageClip.y
                    width: canvasSurface.width; height: canvasSurface.height
                    visible: root.blankCanvas.graphicsBackend === "software" && root.blankCanvas.viewOptions.display.channel !== "RGB" && !root.blankCanvas.viewOptions.display.outlines
                    canvas: root.blankCanvas; family: "channel"
                }
            }
            Binding { target: root.blankCanvas; property: "smoothRendering"; value: !root.blankCanvas.viewOptions.display["pixel-preview"] }
            Binding { target: root.blankCanvas; property: "enabled"; value: !root.modalActive }
            EditorAlignmentOverlay { objectName: "editorGridOverlay"; anchors.fill: parent; z: 208; canvas: root.blankCanvas; family: "grid" }
            EditorAlignmentOverlay {
                id: guidesOverlay
                objectName: "editorGuidesOverlay"; anchors.fill: parent
                z: root.blankCanvas.viewOptions.guides.above ? 216 : 199.5
                canvas: root.blankCanvas; family: "guides"
            }
            EditorAlignmentOverlay { objectName: "editorOutlinesOverlay"; anchors.fill: parent; z: 209; canvas: root.blankCanvas; family: "outlines" }
            EditorAlignmentOverlay { objectName: "editorSnapOverlay"; anchors.fill: parent; z: 217; canvas: root.blankCanvas; family: "snap" }
            EditorAlignmentOverlay { objectName: "editorGuideInput"; anchors.fill: parent; z: 219; enabled: !root.modalActive; canvas: root.blankCanvas; family: "guide-input" }
            Repeater {
                model: NativeData.presentation.concatenate(root.blankCanvas.alignmentFeedback, guidesOverlay.labels)
                LV.Label {
                    required property var modelData
                    visible: Boolean(modelData.label)
                    x: Math.min(canvasSurface.width - width, Math.max(0, modelData.position ? modelData.position.x : 0))
                    y: Math.min(canvasSurface.height - height, Math.max(0, modelData.position ? modelData.position.y : 0))
                    z: modelData.guide ? guidesOverlay.z + 0.1 : 218
                    text: modelData.label || ""; style: caption; color: modelData.color || LV.Theme.primary
                }
            }
            EditorCanvasRulers { objectName: "editorCanvasRulers"; anchors.fill: parent; z: 220; canvas: root.blankCanvas }
            EditorElementTransform {
                anchors.fill: parent
                z: 214
                canvas: root.blankCanvas
            }
            EditorCanvasFeedback {
                objectName: "editorCanvasFeedback"
                anchors.fill: parent
                z: 215
                canvas: root.blankCanvas
                visible: !root.modalActive
            }
            Image {
                objectName: "editorSelectionOverlay"
                z: 210
                x: blankCanvas.panX + (blankCanvas.editingOrigin.x - blankCanvas.canvasOriginX) * blankCanvas.zoom
                y: blankCanvas.panY + (blankCanvas.editingOrigin.y - blankCanvas.canvasOriginY) * blankCanvas.zoom
                width: blankCanvas.editingWidth * blankCanvas.zoom
                height: blankCanvas.editingHeight * blankCanvas.zoom
                source: blankCanvas.selectionOverlay
                visible: source.toString().length > 0
                cache: false
                smooth: true
            }
            Image {
                z: 211
                x: blankCanvas.panX + (blankCanvas.editingOrigin.x - blankCanvas.canvasOriginX) * blankCanvas.zoom; y: blankCanvas.panY + (blankCanvas.editingOrigin.y - blankCanvas.canvasOriginY) * blankCanvas.zoom
                width: blankCanvas.editingWidth * blankCanvas.zoom; height: blankCanvas.editingHeight * blankCanvas.zoom
                source: blankCanvas.toolState.eraserOverlay || ""
                visible: root.selectedTool === "eraser"
                smooth: true
            }
            EditorClippingOverlay {
                objectName: "editorClippingOverlay"
                z: 212
                anchors.fill: parent
                canvas: root.blankCanvas
                visible: root.selectedTool === "color" || root.selectedTool === "layers"
                onPreviewFailed: message => toolSheet.notice = message
            }
            Rectangle {
                objectName: "editorCanvasGuide"
                z: 213
                readonly property var guide: blankCanvas.toolState.canvasGuide || ({})
                readonly property real insetX: guide.mode === "Safe" ? guide.safeX || 0 : guide.mode === "Bleed" ? -(guide.bleed || 0) : 0
                readonly property real insetY: guide.mode === "Safe" ? guide.safeY || 0 : guide.mode === "Bleed" ? -(guide.bleed || 0) : 0
                x: blankCanvas.panX + (blankCanvas.editingOrigin.x - blankCanvas.canvasOriginX) * blankCanvas.zoom + insetX * blankCanvas.zoom
                y: blankCanvas.panY + (blankCanvas.editingOrigin.y - blankCanvas.canvasOriginY) * blankCanvas.zoom + insetY * blankCanvas.zoom
                width: Math.max(0, blankCanvas.editingWidth - insetX * 2) * blankCanvas.zoom
                height: Math.max(0, blankCanvas.editingHeight - insetY * 2) * blankCanvas.zoom
                visible: root.selectedTool === "canvas"
                color: "transparent"
                border.width: 1
                border.color: guide.mode === "Bleed" ? "#FF5F57" : guide.mode === "Safe" ? "#28C840" : "#8B7CFF"
            }
            Item {
                x: activePageClip.x; y: activePageClip.y
                width: activePageClip.width; height: activePageClip.height
                z: 205
                visible: blankCanvas.pagePresentation
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: 2
                    border.color: LV.Theme.accent
                }
                LV.LabelButton {
                    objectName: "editorArtboardLabel" + canvasProject.currentIndex
                    x: Math.max(0, -parent.x)
                    y: -26
                    width: Math.max(48, Math.min(parent.width, 200))
                    height: 22
                    text: qsTr("%1 · %2").arg(canvasProject.currentIndex + 1).arg((canvasProject.artboards[canvasProject.currentIndex] || {}).name || "")
                    onClicked: blankCanvas.forceActiveFocus()
                }
            }
            MouseArea {
                objectName: "editorPageViewportNavigation"
                anchors.fill: parent
                z: 230
                enabled: root.blankCanvas.pagePresentation && !root.modalActive && !root.blankCanvas.liveStrokeActive
                acceptedButtons: Qt.MiddleButton
                preventStealing: true
                property point lastPosition: Qt.point(0, 0)
                onPressed: function(mouse) { lastPosition = Qt.point(mouse.x, mouse.y); root.blankCanvas.forceActiveFocus() }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    root.blankCanvas.panBy(mouse.x - lastPosition.x, mouse.y - lastPosition.y)
                    lastPosition = Qt.point(mouse.x, mouse.y)
                }
                onWheel: function(wheel) {
                    if (!(wheel.modifiers & Qt.ControlModifier)) {
                        const delta = wheel.pixelDelta.x || ((wheel.modifiers & Qt.ShiftModifier)
                            ? wheel.pixelDelta.y || wheel.angleDelta.y / 2 : wheel.angleDelta.x / 2)
                        if (delta) { root.blankCanvas.scrollHorizontally(-delta); wheel.accepted = true; return }
                    }
                    if (wheel.angleDelta.y) {
                        root.blankCanvas.zoomAt(Math.pow(1.0015, wheel.angleDelta.y), Qt.point(wheel.x, wheel.y))
                        wheel.accepted = true
                    } else wheel.accepted = false
                }
            }
            MouseArea {
                objectName: "editorCanvasContextArea"
                anchors.fill: parent
                z: 240
                enabled: !root.mobileLayout && !root.modalActive
                acceptedButtons: Qt.RightButton
                onClicked: function(mouse) { documentControls.openContextMenu(canvasSurface, mouse.x, mouse.y) }
            }
            onWidthChanged: root.resizeCanvas()
            onHeightChanged: root.resizeCanvas()
        }
        EditorPageStrip {
            id: pageStrip
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: implicitHeight
            project: canvasProject
            canvas: blankCanvas
            navigationEnabled: !root.modalActive
            sharedBorders: !root.mobileLayout
        }
    }
    Item {
        id: timelineDock
        objectName: "editorTimelineDock"
        anchors.left: workspace.left
        anchors.right: workspace.right
        anchors.bottom: root.mobileLayout ? toolToolbar.top : parent.bottom
        anchors.bottomMargin: root.mobileLayout ? LV.Theme.gap16 : 0
        height: Math.min(editorTimeline.implicitHeight,
            Math.max(160, (root.height - (root.mobileLayout ? toolToolbar.height + toolbar.height + 48 : 0)) * 0.48))
        EditorTimeline {
            id: editorTimeline
            width: parent.width
            height: parent.height
            canvas: root.blankCanvas
            generation: root.generationRuntime
            compactLayout: !root.mobileLayout
            sharedBorders: !root.mobileLayout
            enabled: !root.modalActive
            onPreviewError: function(message) { toolSheet.notice = message }
        }
    }
    Loader {
        id: desktopDock
        anchors.top: viewBar.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: Math.min(root.desktopPanelMaximumWidth,
            Math.max(root.desktopPanelMinimumWidth, root.desktopPanelWidth))
        visible: !root.mobileLayout && root.desktopPanelExpanded
        active: !root.mobileLayout
        sourceComponent: EditorDesktopPanel {
            toolState: toolSheet
            sharedBorders: true
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
        onDoubleClicked: root.desktopPanelWidth = root.desktopPanelDefaultWidth
    }
    EditorToolbar {
        id: toolToolbar
        anchors.right: !root.mobileLayout && root.desktopPanelExpanded ? desktopDock.left : parent.right
        anchors.top: root.mobileLayout ? undefined : viewBar.bottom
        anchors.bottom: root.mobileLayout ? parent.bottom : undefined
        anchors.rightMargin: root.mobileLayout ? LV.Theme.gap8 : 0
        anchors.bottomMargin: root.mobileLayout ? LV.Theme.gap8 : 0
        width: root.mobileLayout ? Math.max(0, root.width - LV.Theme.gap8 * 2) : implicitWidth
        height: root.mobileLayout ? implicitHeight : Math.max(0, root.height - viewBar.height)
        mobileLayout: root.mobileLayout
        sharedBorders: !root.mobileLayout
        onToolSelected: function(toolId) {
            if (toolId !== "layers") { blankCanvas.setLayerHistogramClipping(false, false); blankCanvas.cancelLayerColorSample() }
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
        projectRuntime: canvasProject
        generationRuntime: root.generationRuntime
        onClosed: if (root.mobileLayout && root.visible) Qt.callLater(toolToolbar.focusCurrent)
        onToolIdChanged: {
            if (toolToolbar.currentTool === toolId) return
            const index = NativeData.presentation.indexOf(toolToolbar.tools, toolSheet.toolId, "key")
            if (index >= 0) toolToolbar.syncTool(index)
        }
        onSourceRequested: (toolId, values) => root.requestSource(toolId, values)
        onActionRequested: function(toolId, fieldId, values) { root.handleToolAction(toolId, fieldId, values) }
    }
    Dialogs.FileDialog {
        id: sourceDialog
        objectName: "editorSourceDialog"
        property string targetTool: "file"
        property var settings: ({})
        onAccepted: nativeWorkspace.importSource(selectedFile, targetTool, settings)
        onRejected: nativeWorkspace.sourceRejected(targetTool)
    }
    Dialogs.FileDialog {
        id: exportDialog
        objectName: "editorExportDialog"
        property string format: "PNG"
        fileMode: Dialogs.FileDialog.SaveFile
        onAccepted: nativeWorkspace.exportImage(selectedFile, format)
        onRejected: blankCanvas.cancelImageExport()
    }
    Connections {
        target: blankCanvas.frameExporter
        function onSaved(destination) {
            if (!blankCanvas.frameExporter.busy) toolSheet.notice = qsTr("Frame exported.")
        }
        function onFailed(destination, message) {
            if (!blankCanvas.frameExporter.busy) toolSheet.notice = message
        }
    }
    Connections {
        target: blankCanvas.media.audioPlayback
        function onFailed(id, message) { toolSheet.notice = message }
    }
    Frames.PanelSheet {
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
                Views.PanelRow {
                    required property var modelData
                    width: parent.width
                    type: LV.ListItem.Navigation
                    label: modelData.label || ""
                    description: modelData.description || ""
                    showDescription: description.length > 0
                    showLeadingIcon: false
                    enabled: modelData.enabled !== false && Boolean(modelData.action)
                    onClicked: nativeWorkspace.activatePreview(modelData)
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
                model: nativeWorkspace.completedResults
                Views.PanelRow {
                    required property var modelData
                    width: parent.width
                    type: LV.ListItem.Navigation
                    label: qsTr("Place variation: %1").arg(modelData.id)
                    showLeadingIcon: false
                    onClicked: nativeWorkspace.placeVariation(modelData)
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
        onClicked: nativeWorkspace.showGenerationResults()
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
    Frames.PanelSheet {
        id: advancedSheet
        objectName: "editorAdvancedGenerationSheet"
        parent: root
        presentation: LV.Sheet.Mobile
        title: qsTr("Advanced generation")
        contentComponent: AdvancedGenerate {
            width: parent ? parent.width : 402
            models: root.generationRuntime ? root.generationRuntime.models : []
            vaes: root.generationRuntime ? root.generationRuntime.vaes : []
            onGenerateRequested: parameters => { if (nativeWorkspace.submitAdvanced(parameters)) advancedSheet.close() }
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
        enabled: root.visible && !root.mobileLayout && root.desktopPanelExpanded && !root.modalActive && !root.applicationSearchActive
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
    Item {
        id: panelBounds
        objectName: "editorPanelBounds"
        anchors.left: sidebar.right; anchors.right: root.right
        anchors.top: viewBar.bottom; anchors.bottom: root.bottom
        visible: !root.mobileLayout
        enabled: false
    }
    Item {
        id: canvasColumnBounds
        anchors.left: workspace.left; anchors.right: workspace.right
        anchors.top: panelBounds.top; anchors.bottom: panelBounds.bottom
        visible: !root.mobileLayout
        enabled: false
    }
    readonly property Item editorPanelFrame: Frames.PanelFrame { parent: root; target: panelBounds; panelId: "editor-body" }
    readonly property Item canvasPanelFrame: Frames.PanelFrame {
        parent: root; target: canvasColumnBounds; panelId: "editor-canvas"
        topEdge: false; bottomEdge: false; leftEdge: false
    }
}
