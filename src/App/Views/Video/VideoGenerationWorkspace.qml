pragma ComponentBehavior: Bound
import QtQuick
import ".." as Frames
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

Item {
    id: root
    objectName:"videoGenerationWorkspace"
    CanvasPreview { id: displayPreviews }
    required property var generation
    property var jobIds:[]
    property bool submitting:false
    property string requestError:""
    property string saveFeedback:""
    property string selectedResultId:""
    readonly property alias parameterPanel:parameters
    readonly property alias timelineState:timelineState
    readonly property alias timelinePanel:timeline
    NativeGenerationProjection {
        id: generationProjection
        jobs: root.generation.jobs
        results: root.generation.completedResults
        connected: root.generation.connected
        requestIds: root.jobIds
        selectedResultId: root.selectedResultId
    }
    readonly property alias jobs: generationProjection.submissionJobs
    readonly property alias results: generationProjection.submissionResults
    readonly property alias selectedResult: generationProjection.selectedResult
    readonly property alias pending: generationProjection.generationPending
    readonly property string failure: requestError || generationProjection.failure
    readonly property string referenceSource:(timelineState.keys.slice().sort((a,b)=>b.frame-a.frame).find(key=>key.frame<=timelineState.playhead) || {}).source || ""
    readonly property bool canGenerate:!pending && !parameters.hasInvalidInputs && parameters.values.prompt.trim().length>0 && timelineState.outputFrames>0 && generation.videoModels.length>0
    readonly property string statusText:failure || (pending ? qsTr("%1 · %2 / %3").arg(generation.inferenceStatus.state || qsTr("Queued")).arg(generation.previewStep).arg(generation.previewTotalSteps) : results.length ? qsTr("%1 videos ready").arg(results.length) : referenceSource ? qsTr("Reference preview · F%1 · %2 s").arg(String(timelineState.playhead).padStart(3,"0")).arg((timelineState.playhead/parameters.values.fps).toFixed(3)) : qsTr("Ready to generate"))
    function submit() {
        requestError=""
        saveFeedback=""
        if(parameters.hasInvalidInputs) { requestError=qsTr("Correct invalid video parameter values."); return }
        submitting=true; const id=generation.enqueueVideoRecipe(parameters.parameters()); submitting=false
        if(!id) requestError=generation.errorString
        else { selectedResultId=""; player.stop() }
    }
    function cancelBatch() { jobs.forEach(job=>{ if(["queued","running","connecting-host","downloading"].indexOf(job.state)>=0) generation.cancel(job.id) }) }
    Connections { target:root.generation; function onSubmissionQueued(ids) { if(root.submitting) root.jobIds=ids.slice() } }
    VideoTimelineState { id:timelineState }
    function saveVideoToFile() {
        if (!root.selectedResult.mediaSource || exporter.busy || saveDialog.visible) return
        root.saveFeedback=""
        saveDialog.sourceVideo=root.selectedResult.mediaSource
        saveDialog.open()
    }
    ImageFileExporter {
        id:exporter; objectName:"videoWorkspaceExporter"
        onSaved:root.saveFeedback=qsTr("File saved")
        onFailed:function(message){ root.saveFeedback=message; root.generation.explainOperationFailure("Save video", message, {source:root.selectedResult.mediaSource, destination:saveDialog.selectedFile}) }
        onRejected:function(message){ root.saveFeedback=message }
    }
    FileDialog {
        id:saveDialog; objectName:"videoWorkspaceSaveDialog"
        property url sourceVideo:""
        title:qsTr("Save video"); fileMode:FileDialog.SaveFile; defaultSuffix:"mp4"; nameFilters:[qsTr("MP4 video (*.mp4)")]
        onAccepted:{ root.saveFeedback=qsTr("Saving file…"); exporter.saveVideo(sourceVideo,selectedFile) }
    }
    MediaPlayer { id:player; objectName:"videoWorkspacePlayer"; source:root.selectedResult.mediaSource || ""; videoOutput:videoOutput; audioOutput:AudioOutput {} }
    property real previewZoom: 0
    Flickable {
        id:workspaceViewport
        objectName:"videoWorkspaceViewport"
        anchors.fill:parent
        contentWidth:Math.max(width,720); contentHeight:Math.max(height,600)
        boundsBehavior:Flickable.StopAtBounds; clip:true
        Controls.ScrollBar.horizontal:Controls.ScrollBar { policy:Controls.ScrollBar.AsNeeded }
        Controls.ScrollBar.vertical:Controls.ScrollBar { policy:Controls.ScrollBar.AsNeeded }
        Item {
            id:workspace
            width:workspaceViewport.contentWidth; height:workspaceViewport.contentHeight
            Item {
                id:leftWorkspace
                objectName:"videoPreviewAndTimeline"
                width:workspace.width-parameters.width; height:workspace.height
                Item {
                    id:top
                    objectName:"videoUpperWorkspace"
                    width:parent.width; height:parent.height-timeline.height
                    Item {
                        id:preview
                        objectName:"videoPreviewPanel"; anchors.fill:parent
                        RowLayout {
                            id:actions; objectName:"videoApplicationActions"
                            width:parent.width; height:46; spacing:8
                            LV.Label { text:qsTr("Video"); style:body; Layout.leftMargin:12 }
                            Item { Layout.fillWidth:true }
                            LV.LabelButton { objectName:"videoPreviewZoomOut"; text:"−"; Layout.preferredWidth:30; tone:LV.AbstractButton.Default; Accessible.name:qsTr("Zoom out"); onClicked:root.previewZoom=Math.max(.1,previewSurface.effectiveScale/1.25) }
                            LV.LabelButton { objectName:"videoPreviewFit"; text:qsTr("Fit"); Layout.preferredWidth:44; tone:LV.AbstractButton.Default; onClicked:root.previewZoom=0 }
                            LV.LabelButton { objectName:"videoPreviewActualSize"; text:"100%"; Layout.preferredWidth:54; tone:LV.AbstractButton.Default; onClicked:root.previewZoom=1 }
                            LV.LabelButton { objectName:"videoPreviewZoomIn"; text:"+"; Layout.preferredWidth:30; Layout.rightMargin:12; tone:LV.AbstractButton.Default; Accessible.name:qsTr("Zoom in"); onClicked:root.previewZoom=Math.min(4,previewSurface.effectiveScale*1.25) }
                        }
                        Item {
                            id:previewSurface; objectName:"videoPreviewSurface"
                            anchors.top:actions.bottom; anchors.bottom:previewActions.top
                            width:parent.width; clip:true
                            readonly property real fitScale:Math.min(Math.max(1,width-24)/parameters.values.width,Math.max(1,height-24)/parameters.values.height,600/parameters.values.width)
                            readonly property real effectiveScale:root.previewZoom>0 ? root.previewZoom : fitScale
                            Item {
                                objectName:"videoPreviewAspectFrame"
                                x:(previewSurface.width-width)/2; y:(previewSurface.height-height)/2
                                width:parameters.values.width*previewSurface.effectiveScale; height:parameters.values.height*previewSurface.effectiveScale
                                Image { objectName:"videoReferencePreview"; anchors.fill:parent; source:root.selectedResult.imageSource || displayPreviews.sourceForUrl(root.referenceSource); asynchronous:true; sourceSize:Qt.size(Math.ceil(width*Screen.devicePixelRatio),Math.ceil(height*Screen.devicePixelRatio)); fillMode:root.selectedResult.imageSource ? Image.PreserveAspectFit : Image.PreserveAspectCrop; visible:source.toString().length>0 && player.playbackState!==MediaPlayer.PlayingState }
                                VideoOutput { id:videoOutput; objectName:"videoWorkspaceOutput"; anchors.fill:parent; fillMode:VideoOutput.PreserveAspectFit; visible:!!root.selectedResult.mediaSource }
                            }
                            LV.Label { objectName:"videoPreviewEmptyState"; anchors.centerIn:parent; width:parent.width-32; horizontalAlignment:Text.AlignHCenter; visible:!root.pending && !root.referenceSource && !root.selectedResult.imageSource && !root.selectedResult.mediaSource; text:qsTr("Enter a prompt to generate a video."); style:caption; color:LV.Theme.textSecondary; wrapMode:Text.Wrap }
                            LV.Label { anchors.centerIn:parent; width:parent.width-24; horizontalAlignment:Text.AlignHCenter; wrapMode:Text.Wrap; visible:root.pending; text:root.statusText; style:body }
                        }
                        RowLayout {
                            id:previewActions; objectName:"videoPreviewActions"
                            anchors.bottom:statusStrip.top; width:parent.width; height:46; spacing:8
                            LV.LabelButton { objectName:"videoWorkspacePlay"; Layout.preferredWidth:52; Layout.leftMargin:12; text:player.playbackState===MediaPlayer.PlayingState ? qsTr("Pause") : qsTr("Play"); tone:LV.AbstractButton.Default; enabled:!!root.selectedResult.mediaSource || !!root.referenceSource; onClicked:{ if(root.selectedResult.mediaSource) { if(player.playbackState===MediaPlayer.PlayingState) player.pause(); else player.play() } else timeline.playing=!timeline.playing } }
                            LV.Label { Layout.fillWidth:true; Layout.minimumWidth:0; text:root.saveFeedback || root.statusText; style:caption; elide:Text.ElideRight; color:root.failure ? LV.Theme.accentRed : LV.Theme.textSecondary }
                            LV.LabelButton { visible:root.pending; text:qsTr("Cancel"); tone:LV.AbstractButton.Default; onClicked:root.cancelBatch() }
                            LV.LabelButton { objectName:"videoResultSelector"; visible:root.results.length>1; text:qsTr("Results · %1").arg(root.results.length); tone:LV.AbstractButton.Default; onClicked:resultsMenu.openFor(this,0,height) }
                            LV.LabelButton { objectName:"videoWorkspaceSave"; Layout.rightMargin:12; visible:!!root.selectedResult.mediaSource; enabled:!exporter.busy; text:qsTr("Save video"); tone:LV.AbstractButton.Default; onClicked:root.saveVideoToFile() }
                        }
                        RowLayout {
                            id:statusStrip; objectName:"videoStatusStrip"
                            anchors.bottom:parent.bottom; width:parent.width; height:28; spacing:8
                            LV.Label { objectName:"videoStatusModel"; Layout.leftMargin:12; Layout.maximumWidth:statusStrip.width*.38; text:qsTr("Model · %1").arg((root.generation.videoModels.find(model=>model.id===root.generation.selectedVideoModel) || {}).name || qsTr("None")); style:caption; elide:Text.ElideRight }
                            LV.Label { objectName:"videoStatusDevice"; text:qsTr("Device · %1").arg(parameters.values.device==="auto" ? qsTr("Auto") : parameters.values.device); style:caption }
                            Item { Layout.fillWidth:true; Layout.minimumWidth:0 }
                            LV.Label { objectName:"videoStatusProcessing"; text:root.pending ? root.statusText : root.generation.connected ? qsTr("Ready · %1 queued").arg(root.jobs.filter(job=>job.state==="queued").length) : qsTr("Storage disconnected"); style:caption; elide:Text.ElideRight; Layout.maximumWidth:statusStrip.width*.3 }
                            LV.Label { objectName:"videoStatusOutput"; Layout.rightMargin:12; text:root.saveFeedback || (root.results.length ? qsTr("Output · %1 ready").arg(root.results.length) : qsTr("Save · No output yet")); style:caption; Layout.maximumWidth:statusStrip.width*.25; elide:Text.ElideRight }
                        }
                    }
                }
                VideoTimeline {
                    id:timeline; generation:root.generation
                    width:parent.width; height:264; anchors.bottom:parent.bottom
                    stateModel:timelineState; onChooseKeyframe:function(frame,trackId){ parameters.chooseImage(frame,trackId) }
                }
            }
            VideoParameterPanel {
                id:parameters; anchors.right:parent.right
                width:Math.min(implicitWidth,Math.max(300,workspace.width*.4)); height:parent.height
                generation:root.generation; timeline:timelineState; canGenerate:root.canGenerate; pending:root.pending
                onGenerateRequested:root.submit()
            }
        }
    }
    LV.ContextMenu {
        id:resultsMenu
        items:root.results.map((result,index)=>qsTr("Video %1 · %2 frames").arg(index+1).arg(result.generation?.video?.frame_count || 0))
        showIconSlot:false
        onItemTriggered:function(index){ root.selectedResultId=root.results[index].id }
    }
    readonly property Item previewPanelFrame: Frames.PanelFrame { parent: preview; panelId: "video-preview" }
}
