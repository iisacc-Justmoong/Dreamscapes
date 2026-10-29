pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import ".." as Frames
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "imageGenerationWorkspace"
    required property var generation
    NativeGenerationSubmission {
        id: nativeSubmission
        objectName: "imageNativeSubmission"
        generation: root.generation
        mediaType: "Image"
        imageParameters: panel.parameterStore
        onSelectionResetRequested: imageSelection.reset()
    }
    property alias jobIds: nativeSubmission.jobIds
    property alias requestError: nativeSubmission.requestError
    property alias invalidInputSubmission: nativeSubmission.invalidInputSubmission
    property alias selectedResultId: nativeSubmission.selectedResultId
    property alias selectedImageSource: nativeSubmission.selectedImageSource
    property alias submitting: nativeSubmission.submitting
    readonly property alias parameterPanel: panel
    NativeGenerationProjection {
        id: generationProjection
        jobs: root.generation.jobs
        results: root.generation.completedResults
        connected: root.generation.connected
        requestIds: root.jobIds
        selectedResultId: root.selectedResultId
        selectedImageSource: root.selectedImageSource
    }
    readonly property alias jobs: generationProjection.submissionJobs
    readonly property alias results: generationProjection.submissionResults
    readonly property alias activeJob: generationProjection.activeGeneration
    readonly property alias pending: generationProjection.generationPending
    readonly property alias selectedResult: generationProjection.selectedResult
    readonly property int canvasPixelWidth: panel.values.width
    readonly property int canvasPixelHeight: panel.values.height
    readonly property bool showingPreview: !!activeJob.id && generation.previewJobId === activeJob.id
        && generation.previewImage.toString().length > 0
    readonly property url imageSource: showingPreview
        ? generation.previewImage : selectedResult.imageSource || ""
    readonly property string failure: requestError || generationProjection.failure
    signal editImageRequested(url source, var result)
    signal editImagesRequested(var images)

    function submit() { nativeSubmission.submit() }
    function cancelBatch() { nativeSubmission.cancelBatch() }
    // The canvas and parameter viewport have independent vertical extents.
    // Narrow desktop windows retain both regions in a horizontal workspace.
    Flickable {
        id: workspaceViewport
        anchors.fill: parent
        contentWidth: Math.max(width, LV.Theme.scaleMetric(720))
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Controls.ScrollBar.horizontal: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
        LV.HStack {
            width: workspaceViewport.contentWidth
            height: workspaceViewport.height
            spacing: 0
            Item {
                id: canvas
                objectName: "advancedImageCanvas"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Rectangle {
                    id: surface
                    objectName: "advancedImageCanvasSurface"
                    x: (canvas.width - width) / 2
                    y: (canvas.height - height) / 2
                    // One fit scale preserves the document aspect ratio for an
                    // empty canvas, every streamed frame and the final image.
                    readonly property real fitScale: Math.min(
                        Math.max(0, canvas.width - LV.Theme.gap24) / root.canvasPixelWidth,
                        Math.max(0, canvas.height * 0.7 - LV.Theme.gap16) / root.canvasPixelHeight)
                    width: root.canvasPixelWidth * fitScale
                    height: root.canvasPixelHeight * fitScale
                    color: "white"
                    Accessible.name: qsTr("Generation canvas, %1 × %2 pixels").arg(root.canvasPixelWidth).arg(root.canvasPixelHeight)
                    Image {
                        objectName: "advancedGeneratedImage"
                        anchors.fill: parent
                        source: root.imageSource
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                    }
                }
                LV.Label {
                    objectName: "advancedCanvasResolution"
                    anchors.top: surface.bottom
                    anchors.topMargin: LV.Theme.gap8
                    anchors.horizontalCenter: surface.horizontalCenter
                    text: qsTr("%1 × %2").arg(root.canvasPixelWidth).arg(root.canvasPixelHeight)
                    style: caption
                }
                LV.VStack {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: LV.Theme.gap16
                    spacing: LV.Theme.gap8
                    LV.Label {
                        objectName: "advancedGenerationStatus"
                        Layout.fillWidth: true
                        text: root.failure || (root.pending ? (root.activeJob.id
                            ? qsTr("%1 · %2 / %3").arg(root.generation.inferenceStatus.state || qsTr("Generating"))
                                .arg(root.generation.previewStep).arg(root.generation.previewTotalSteps)
                            : qsTr("Queued")) : root.results.length > 0 ? qsTr("%1 images ready").arg(root.results.length) : "")
                        visible: text.length > 0
                        wrapMode: Text.Wrap
                        sizeToContentHeight: true
                        style: caption
                    }
                    Flickable {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.results.length > 1 ? LV.Theme.scaleMetric(64) : 0
                        visible: root.results.length > 1
                        contentWidth: thumbnails.implicitWidth
                        contentHeight: height
                        clip: true
                        LV.HStack {
                            id: thumbnails
                            spacing: LV.Theme.gap6
                            Repeater {
                                model: root.results
                                delegate: LV.AbstractButton {
                                    id: resultThumbnail
                                    required property int index
                                    required property var modelData
                                    objectName: "advancedResultThumbnail" + index
                                    implicitWidth: LV.Theme.scaleMetric(64)
                                    implicitHeight: LV.Theme.scaleMetric(64)
                                    Layout.minimumWidth: implicitWidth
                                    Layout.maximumWidth: implicitWidth
                                    Layout.minimumHeight: implicitHeight
                                    Layout.maximumHeight: implicitHeight
                                    activeFocusOnTab: true
                                    Accessible.name: qsTr("Generated image %1").arg(index + 1)
                                    contentItem: Image {
                                        source: resultThumbnail.modelData.imageSource
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                    }
                                    background: Rectangle {
                                        color: "transparent"
                                        border.width: imageSelection.isSelected(resultThumbnail.modelData) ? 2 : 0
                                        border.color: LV.Theme.accent
                                        z: 1
                                    }
                                    onClicked: {
                                        if (imageSelection.selectionMode) imageSelection.toggle(modelData)
                                        else { root.selectedResultId = modelData.id; root.selectedImageSource = modelData.imageSource.toString() }
                                    }
                                }
                            }
                        }
                    }
                    LV.HStack {
                        spacing: LV.Theme.gap8
                        ImageSelectionBar {
                            id: imageSelection
                            objectName: "advancedImageSelection"
                            entries: root.results
                            onOpenRequested: function(images) { root.editImagesRequested(images) }
                        }
                        LV.LabelButton {
                            objectName: "advancedOpenCanvas"
                            text: qsTr("Open in canvas")
                            visible: !!root.selectedResult.imageSource && !root.pending && !imageSelection.selectionMode
                            onClicked: root.editImageRequested(root.selectedResult.imageSource, root.selectedResult)
                        }
                    }
                }
            }
            LV.VStack {
                objectName: "advancedParameterColumn"
                Layout.preferredWidth: LV.Theme.scaleMetric(402)
                Layout.fillHeight: true
                spacing: LV.Theme.gap8
                AdvancedGenerate {
                    generation: root.generation
                    id: panel
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    models: root.generation.models
                    vaes: root.generation.vaes
                }
                LV.HStack {
                    Layout.fillWidth: true
                    Layout.leftMargin: LV.Theme.gap16
                    Layout.rightMargin: LV.Theme.gap16
                    Layout.bottomMargin: LV.Theme.gap12
                    spacing: LV.Theme.gap8
                    LV.LabelButton {
                        objectName: "advancedReset"
                        text: qsTr("Reset")
                        onClicked: { panel.resetDraft(); root.requestError = "" }
                    }
                    Item { Layout.fillWidth: true }
                    LV.LabelButton {
                        objectName: "advancedCancel"
                        visible: root.pending
                        text: qsTr("Cancel batch")
                        onClicked: root.cancelBatch()
                    }
                    LV.LabelButton {
                        objectName: "advancedSubmit"
                        text: qsTr("Generate")
                        tone: LV.AbstractButton.Primary
                        enabled: !root.submitting && !root.pending && !panel.hasInvalidInputs && panel.values.prompt.trim().length > 0
                        onClicked: root.submit()
                    }
                }
            }
        }
    }
    readonly property Item canvasPanelFrame: Frames.PanelFrame { parent: canvas; panelId: "image-preview" }
}
