pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "imageGenerationWorkspace"
    required property var generation
    property var jobIds: []
    property string requestError: ""
    property bool invalidInputSubmission: false
    property string selectedResultId: ""
    property bool submitting: false
    readonly property alias parameterPanel: panel
    readonly property var jobs: generation.jobs.filter(job => root.jobIds.indexOf(job.id) >= 0)
    readonly property var results: generation.connected
        ? generation.completedResults.filter(result => root.jobIds.indexOf(result.id) >= 0) : []
    readonly property var activeJob: jobs.find(job => ["running", "connecting-host", "downloading"].indexOf(job.state) >= 0) || ({})
    readonly property bool pending: jobs.some(job => ["queued", "running", "connecting-host", "downloading"].indexOf(job.state) >= 0)
    readonly property var selectedResult: results.find(result => result.id === root.selectedResultId) || results[results.length - 1] || ({})
    readonly property int canvasPixelWidth: panel.values.width
    readonly property int canvasPixelHeight: panel.values.height
    readonly property bool showingPreview: !!activeJob.id && generation.previewJobId === activeJob.id
        && generation.previewImage.toString().length > 0
    readonly property url imageSource: showingPreview
        ? generation.previewImage : selectedResult.imageSource || ""
    readonly property string failure: requestError || (jobs.find(job => job.state === "failed") || {}).error || ""
    signal editImageRequested(url source, var result)

    function submit() {
        requestError = ""
        invalidInputSubmission = panel.hasInvalidInputs
        if (invalidInputSubmission) { requestError = qsTr("Correct invalid parameter input before generating."); return }
        submitting = true
        const firstId = generation.enqueueAdvanced(panel.parameters())
        submitting = false
        if (firstId.length === 0) requestError = generation.errorString
        else selectedResultId = ""
    }
    function cancelBatch() {
        jobs.forEach(job => { if (["queued", "running", "connecting-host", "downloading"].indexOf(job.state) >= 0) generation.cancel(job.id) })
    }

    // Own the accepted batch here, before the deferred worker starts. The app
    // shell identifies QuickGenerate submissions through the submitting composer.
    Connections {
        target: root.generation
        function onSubmissionQueued(ids) {
            if (root.submitting) root.jobIds = ids.slice()
        }
    }

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
                    anchors.top: parent.top
                    anchors.topMargin: LV.Theme.gap16
                    anchors.horizontalCenter: parent.horizontalCenter
                    // One fit scale preserves the document aspect ratio for an
                    // empty canvas, every streamed frame and the final image.
                    readonly property real fitScale: Math.min(
                        Math.max(0, canvas.width - LV.Theme.gap24) / root.canvasPixelWidth,
                        Math.max(0, canvas.height * 0.7 - anchors.topMargin) / root.canvasPixelHeight)
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
                                delegate: Image {
                                    required property var modelData
                                    Layout.preferredWidth: LV.Theme.scaleMetric(64)
                                    Layout.preferredHeight: LV.Theme.scaleMetric(64)
                                    source: modelData.imageSource
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    MouseArea { anchors.fill: parent; onClicked: root.selectedResultId = parent.modelData.id }
                                }
                            }
                        }
                    }
                    LV.LabelButton {
                        text: qsTr("Open in canvas")
                        visible: !!root.selectedResult.imageSource && !root.pending
                        onClicked: root.editImageRequested(root.selectedResult.imageSource, root.selectedResult)
                    }
                }
            }
            LV.VStack {
                objectName: "advancedParameterColumn"
                Layout.preferredWidth: LV.Theme.scaleMetric(402)
                Layout.fillHeight: true
                spacing: LV.Theme.gap8
                AdvancedGenerate {
                    id: panel
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    models: root.generation.models
                    vaes: root.generation.vaes
                    onHasInvalidInputsChanged: {
                        if (!hasInvalidInputs && root.invalidInputSubmission) {
                            root.invalidInputSubmission = false
                            root.requestError = ""
                        }
                    }
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
}
