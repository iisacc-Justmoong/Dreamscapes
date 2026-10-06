pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

Item {
    id: root
    objectName:"videoGenerationWorkspace"
    required property var generation
    property var jobIds:[]
    property bool submitting:false
    property string requestError:""
    property string selectedResultId:""
    readonly property alias parameterPanel:parameters
    readonly property alias timelineState:timelineState
    readonly property alias timelinePanel:timeline
    readonly property var jobs:generation.jobs.filter(job=>root.jobIds.indexOf(job.id)>=0)
    readonly property var results:generation.connected ? generation.completedResults.filter(result=>root.jobIds.indexOf(result.id)>=0) : []
    readonly property var selectedResult:results.find(result=>result.id===selectedResultId) || results[results.length-1] || ({})
    readonly property bool pending:jobs.some(job=>["queued","running","connecting-host","downloading"].indexOf(job.state)>=0)
    readonly property string failure:requestError || (jobs.find(job=>job.state==="failed") || {}).error || ""
    readonly property bool canGenerate:!pending && !parameters.hasInvalidInputs && parameters.values.prompt.trim().length>0 && timelineState.keptFrames>0 && generation.videoModels.length>0
    readonly property string statusText:failure || (pending ? qsTr("%1 · %2 / %3").arg(generation.inferenceStatus.state || qsTr("Queued")).arg(generation.previewStep).arg(generation.previewTotalSteps) : results.length ? qsTr("%1 videos ready").arg(results.length) : qsTr("Reference preview · F%1 · %2 s").arg(String(timelineState.playhead).padStart(3,"0")).arg((timelineState.playhead/parameters.values.fps).toFixed(3)))
    function submit() {
        requestError=""
        if(parameters.hasInvalidInputs) { requestError=qsTr("Correct invalid video parameter values."); return }
        submitting=true; const id=generation.enqueueVideoRecipe(parameters.parameters()); submitting=false
        if(!id) requestError=generation.errorString
        else { selectedResultId=""; player.stop() }
    }
    function cancelBatch() { jobs.forEach(job=>{ if(["queued","running","connecting-host","downloading"].indexOf(job.state)>=0) generation.cancel(job.id) }) }
    Connections { target:root.generation; function onSubmissionQueued(ids) { if(root.submitting) root.jobIds=ids.slice() } }
    VideoTimelineState { id:timelineState }
    ImageFileExporter { id:exporter; onFailed:function(message){ root.requestError=message } }
    FileDialog { id:saveDialog; title:qsTr("Save video"); fileMode:FileDialog.SaveFile; defaultSuffix:"mp4"; nameFilters:[qsTr("MP4 video (*.mp4)")]; onAccepted:exporter.saveVideo(root.selectedResult.mediaSource,selectedFile) }
    MediaPlayer { id:player; objectName:"videoWorkspacePlayer"; source:root.selectedResult.mediaSource || ""; videoOutput:videoOutput; audioOutput:AudioOutput {} }
    Flickable {
        id:workspaceViewport
        objectName:"videoWorkspaceViewport"
        anchors.fill:parent; contentWidth:Math.max(width,720); contentHeight:Math.max(height,660+timeline.curveHeight)
        boundsBehavior:Flickable.StopAtBounds; clip:true
        Controls.ScrollBar.horizontal:Controls.ScrollBar { policy:Controls.ScrollBar.AsNeeded }
        Controls.ScrollBar.vertical:Controls.ScrollBar { policy:Controls.ScrollBar.AsNeeded }
        Item {
            id:workspace
            width:workspaceViewport.contentWidth; height:workspaceViewport.contentHeight
            readonly property real inset:16
            readonly property real panelGap:width>=1536 ? 116 : 24
            Item {
                id:top
                objectName:"videoUpperWorkspace"
                x:workspace.inset; y:16; width:workspace.width-32; height:workspace.height-timeline.height-44
                Item {
                    id:preview
                    objectName:"videoPreviewPanel"
                    width:top.width-parameters.width-workspace.panelGap; height:top.height
                    LV.HStack {
                        id:actions
                        objectName:"videoApplicationActions"
                        width:parent.width; height:44; spacing:8
                        LV.VStack {
                            Layout.fillWidth:true; Layout.leftMargin:12; spacing:4
                            LV.Label { Layout.fillWidth:true; text:qsTr("Dreamscapes · Video"); style:body; elide:Text.ElideRight }
                            LV.Label { Layout.fillWidth:true; text:qsTr("%1 · %2 s · %3 × %4").arg(timelineState.selectedShot.name || qsTr("Composition")).arg((timelineState.totalFrames/parameters.values.fps).toFixed(2)).arg(parameters.values.width).arg(parameters.values.height); style:caption; elide:Text.ElideRight }
                        }
                        LV.LabelButton { objectName:"videoSaveRecipe"; Layout.preferredWidth:96; Layout.preferredHeight:32; text:qsTr("Save recipe"); tone:LV.AbstractButton.Default; onClicked:parameters.savePreset() }
                        LV.LabelButton { objectName:"videoGenerateComposition"; Layout.preferredWidth:160; Layout.preferredHeight:32; text:qsTr("Generate · %1 videos").arg(parameters.values.outputCount); tone:LV.AbstractButton.Primary; enabled:root.canGenerate; onClicked:root.submit() }
                    }
                    Rectangle {
                        id:previewSurface
                        objectName:"videoPreviewSurface"
                        anchors.top:actions.bottom; anchors.topMargin:8; anchors.bottom:previewActions.top; anchors.bottomMargin:8
                        width:parent.width; color:LV.Theme.panelBackground01; clip:true
                        readonly property real fitScale:Math.min(width/parameters.values.width,height/parameters.values.height)
                        Item {
                            objectName:"videoPreviewAspectFrame"
                            x:(previewSurface.width-width)/2; y:(previewSurface.height-height)/2
                            width:parameters.values.width*previewSurface.fitScale; height:parameters.values.height*previewSurface.fitScale
                            Image { objectName:"videoReferencePreview"; anchors.fill:parent; source:root.selectedResult.imageSource || (timelineState.keys.slice().sort((a,b)=>b.frame-a.frame).find(key=>key.frame<=timelineState.playhead) || {}).source || Qt.resolvedUrl("Assets/coastal-reference.png"); fillMode:root.selectedResult.imageSource ? Image.PreserveAspectFit : Image.PreserveAspectCrop; visible:player.playbackState!==MediaPlayer.PlayingState }
                            VideoOutput { id:videoOutput; objectName:"videoWorkspaceOutput"; anchors.fill:parent; fillMode:VideoOutput.PreserveAspectFit; visible:!!root.selectedResult.mediaSource }
                        }
                        LV.Label { anchors.centerIn:parent; visible:root.pending; text:root.statusText; style:body; Rectangle { anchors.fill:parent; z:-1; radius:8; color:LV.Theme.panelBackground08; opacity:.9 } }
                    }
                    LV.HStack {
                        id:previewActions
                        objectName:"videoPreviewActions"
                        anchors.bottom:parent.bottom; width:parent.width; height:32; spacing:8
                        LV.LabelButton { objectName:"videoWorkspacePlay"; Layout.preferredWidth:80; Layout.preferredHeight:32; text:player.playbackState===MediaPlayer.PlayingState ? qsTr("Pause") : qsTr("Preview"); tone:LV.AbstractButton.Default; onClicked:{ if(root.selectedResult.mediaSource) { if(player.playbackState===MediaPlayer.PlayingState) player.pause(); else player.play() } else timeline.playing=!timeline.playing } }
                        LV.Label { Layout.fillWidth:true; text:root.statusText; style:body; elide:Text.ElideRight; color:root.failure ? LV.Theme.accentRed : LV.Theme.textPrimary }
                        LV.LabelButton { visible:root.pending; text:qsTr("Cancel"); tone:LV.AbstractButton.Default; onClicked:root.cancelBatch() }
                        LV.LabelButton { objectName:"videoWorkspaceSave"; visible:!!root.selectedResult.mediaSource; text:qsTr("Save video"); tone:LV.AbstractButton.Default; onClicked:saveDialog.open() }
                        LV.LabelButton { Layout.preferredWidth:56; Layout.preferredHeight:32; text:qsTr("Fit"); tone:LV.AbstractButton.Default; onClicked:{ timeline.timelineScale=1; workspaceViewport.contentX=0 } }
                    }
                }
                VideoParameterPanel { id:parameters; anchors.right:parent.right; width:300; height:parent.height; generation:root.generation; timeline:timelineState }
            }
            VideoTimeline {
                id:timeline
                x:16; width:workspace.width-32; height:implicitHeight; anchors.bottom:parent.bottom; anchors.bottomMargin:16
                stateModel:timelineState; onChooseKeyframe:function(frame){ parameters.chooseImage(frame) }
            }
        }
    }
    LV.ContextMenu {
        id:resultsMenu
        items:root.results.map((result,index)=>qsTr("Video %1 · %2 frames").arg(index+1).arg(result.generation?.video?.frame_count || 0))
        showIconSlot:false
        onItemTriggered:function(index){ root.selectedResultId=root.results[index].id }
    }
    LV.LabelButton { objectName:"videoResultSelector"; visible:root.results.length>1; anchors.right:parent.right; anchors.top:parent.top; anchors.rightMargin:332; anchors.topMargin:62; text:qsTr("Results · %1").arg(root.results.length); tone:LV.AbstractButton.Default; onClicked:resultsMenu.openFor(this,0,height) }
}
