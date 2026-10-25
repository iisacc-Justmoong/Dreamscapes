pragma ComponentBehavior: Bound
import QtQuick
import ".." as Frames
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0
import "../Editor" as Editor

FocusScope {
    id: root
    readonly property Item panelFrame: Frames.PanelFrame { parent: root; panelId: "video-timeline" }
    objectName:"videoTimeline"
    CanvasPreview { id: displayPreviews }
    required property VideoTimelineState stateModel
    property var generation:null
    property bool playing:false
    property real timelineScale:1
    readonly property int gutter:width < 600 ? 88 : 192
    readonly property bool wideControls:width >= 700
    property bool loopPlayback:false
    property int snapStep:1
    readonly property real framePitch:114.6666666667/10
    readonly property real pixelsPerFrame:framePitch*Math.max(.0001,timelineScale)
    readonly property real rulerY:68+(tools.visible ? tools.height+4 : 0)+(trackControls.visible ? trackControls.height+4 : 0)+(trackError.visible ? trackError.height+4 : 0)
    readonly property var trackLayout: {
        let top=32
        return stateModel.tracks.map(track=>{
            const height=track.type==="Shots" ? 56 : track.type==="Contents" ? 40+(stateModel.mode==="Curves" ? 80 : 0) : track.type==="Audio" ? 48 : 32
            const row=Object.assign({},track,{top:top,height:height}); top+=height; return row
        })
    }
    readonly property real lanesHeight:trackLayout.length ? 32+trackLayout.reduce((height,track)=>height+track.height,0) : 196
    signal chooseKeyframe(int frame, string trackId)
    readonly property int curveHeight:stateModel.mode==="Curves" ? stateModel.tracksOfType("Contents").length*80 : 0
    readonly property bool hasContent:stateModel.shots.length>0 || stateModel.keys.length>0
    property bool playbackAvailable:hasContent
    implicitHeight:264
    readonly property real contentExtent:hasContent || stateModel.tracks.length ? footer.y+Math.max(17,footer.height)+6 : 264
    clip:true
    function timecode(frame) { const seconds=Math.floor(frame/stateModel.fps); return [Math.floor(seconds/3600),Math.floor(seconds/60)%60,seconds%60,frame%stateModel.fps].map(value=>String(value).padStart(2,"0")).join(":") }
    function frameLabel(frame) { return "F"+String(frame).padStart(3,"0") }
    function seekFrame(frame, snapPointer) { playing=false; stateModel.seek(snapPointer && stateModel.snap ? Math.round(frame/snapStep)*snapStep : frame) }
    function setRange(field, text) {
        const value=Number(String(text).replace(/^F/i,""))
        if(!Number.isInteger(value) || value<0 || value>=stateModel.totalFrames || (field==="markIn" ? value>stateModel.markOut : value<stateModel.markIn)) return false
        stateModel[field]=value; return true
    }
    function fitTimeline() { timelineScale=Math.max(1,lanes.width)/(root.stateModel.totalFrames*framePitch); lanes.contentX=0; viewport.contentY=0 }
    onVisibleChanged: if(!visible) playing=false
    onEnabledChanged: if(!enabled) playing=false
    Connections { target:root.stateModel; function onResetCompleted() { root.playing=false; root.timelineScale=1; lanes.contentX=0; viewport.contentY=0 } }
    Keys.onPressed:function(event) {
        if(event.key===Qt.Key_Space) { if(playbackAvailable) playing=!playing; event.accepted=true }
        else if(event.key===Qt.Key_Left || event.key===Qt.Key_Right) { seekFrame(stateModel.playhead+(event.key===Qt.Key_Left ? -1 : 1)); event.accepted=true }
        else if(event.key===Qt.Key_B) { stateModel.tool="Blade"; event.accepted=true }
        else if(event.key===Qt.Key_V) { stateModel.tool="Select"; event.accepted=true }
        else if(event.key===Qt.Key_I) { stateModel.markIn=stateModel.playhead; event.accepted=true }
        else if(event.key===Qt.Key_O) { stateModel.markOut=stateModel.playhead; event.accepted=true }
        else if(event.key===Qt.Key_Delete || event.key===Qt.Key_Backspace) { if(stateModel.mode==="Shots") stateModel.rippleDelete(); else stateModel.deleteKey(); event.accepted=true }
        else if(event.key===Qt.Key_Z && (event.modifiers & Qt.ControlModifier)) { if(event.modifiers & Qt.ShiftModifier) stateModel.redo(); else stateModel.undo(); event.accepted=true }
    }
    Timer {
        interval:Math.max(1,1000/root.stateModel.fps); repeat:true; running:root.playing && root.visible && root.enabled
        property int firstFrame:0
        property double startedAt:0
        onRunningChanged: if(running) { firstFrame=root.stateModel.playhead; startedAt=Date.now() }
        onTriggered:{
            const next=firstFrame+Math.floor((Date.now()-startedAt)/1000*root.stateModel.fps)
            if(next>=root.stateModel.totalFrames) { if(root.loopPlayback) root.stateModel.seek(next%root.stateModel.totalFrames); else { root.playing=false; root.stateModel.seek(0) } }
            else root.stateModel.seek(next)
        }
    }
    component Button: LV.LabelButton {
        id: control
        property bool selectionControl:false
        property bool stateSelected:false
        height:implicitHeight
        tone:LV.AbstractButton.Default
        Accessible.checkable:checkable
        Accessible.checked:checked
        Accessible.onToggleAction:click()
        Editor.EditorButtonState { button:control; selectable:control.selectionControl; selected:control.stateSelected }
    }
    component TrackHeading: Button {
        property var trackData: ({})
        objectName:"videoTrackHeading-"+trackData.id
        width:root.gutter-8; height:28; clip:true
        text:trackData.type+" "+(root.stateModel.trackPosition(trackData.id)+1)+(trackData.name!==trackData.type ? " · "+trackData.name : "")
        tone:LV.AbstractButton.Borderless
        selectionControl:true; stateSelected:root.stateModel.selectedTrackId===trackData.id
        Accessible.name:qsTr("%1 track: %2").arg(trackData.type).arg(trackData.name)
        onClicked:root.stateModel.selectTrack(trackData.id)
    }
    component HeaderButton: Button {}
    component TimelineIcon: Item {
        id:iconControl
        property string asset:""
        property string tooltip:""
        property bool selectable:false
        property bool selected:false
        signal clicked()
        implicitWidth:24; implicitHeight:24; width:24; height:24
        LV.IconButton {
            id:iconButton; objectName:iconControl.objectName+"Button"
            anchors.centerIn:parent; iconSize:16; iconSource:Qt.resolvedUrl("Assets/"+iconControl.asset+".svg")
            tone:LV.AbstractButton.Default; Accessible.name:iconControl.tooltip
            Editor.EditorButtonState { button:iconButton; selectable:iconControl.selectable; selected:iconControl.selected }
            onClicked:iconControl.clicked()
        }
        LV.Tooltip { target:iconButton; text:iconControl.tooltip }
    }
    Flickable {
        id: viewport
        objectName:"videoTimelineViewport"
        anchors.fill:parent; contentWidth:width; contentHeight:root.contentExtent
        flickableDirection:Flickable.VerticalFlick; boundsBehavior:Flickable.StopAtBounds; clip:true
        Controls.ScrollBar.vertical:Controls.ScrollBar { policy:Controls.ScrollBar.AsNeeded }
        Item {
            id: panel
            width:viewport.width; height:root.contentExtent
            Item {
                id:header; objectName:"videoTimelineHeader"; width:parent.width; height:40; clip:true
                RowLayout {
                    x:8; y:0; width:Math.max(775,parent.width-16); height:parent.height; spacing:8
                    LV.Label { text:qsTr("Timeline"); style:body }
                    HeaderButton { objectName:"videoAddShot"; Layout.preferredWidth:70; text:qsTr("+ Layer"); onClicked:root.stateModel.addShot() }
                    Rectangle { Layout.preferredWidth:1; Layout.preferredHeight:16; color:LV.Theme.panelBackground12 }
                    TimelineIcon { asset:"first"; tooltip:qsTr("First frame"); onClicked:root.seekFrame(0) }
                    TimelineIcon { asset:"previous"; tooltip:qsTr("Previous frame"); onClicked:root.seekFrame(root.stateModel.playhead-1) }
                    TimelineIcon { objectName:"videoTimelinePlay"; asset:"play"; tooltip:root.playing ? qsTr("Pause timeline") : qsTr("Play timeline"); enabled:root.playbackAvailable; onClicked:root.playing=!root.playing }
                    TimelineIcon { asset:"next"; tooltip:qsTr("Next frame"); onClicked:root.seekFrame(root.stateModel.playhead+1) }
                    Button { objectName:"videoTimecode"; text:qsTr("%1 · %2").arg(root.timecode(root.stateModel.playhead)).arg(root.frameLabel(root.stateModel.playhead)); tone:LV.AbstractButton.Borderless }
                    TimelineIcon { objectName:"videoTimelineLoop"; asset:"loop"; tooltip:qsTr("Loop playback"); selectable:true; selected:root.loopPlayback; onClicked:root.loopPlayback=!root.loopPlayback }
                    Item { Layout.fillWidth:true; Layout.minimumWidth:0 }
                    Row {
                        Layout.preferredWidth:implicitWidth; Layout.minimumWidth:implicitWidth; Layout.preferredHeight:28; spacing:4
                        Repeater { model:["Shots","Keyframes","Curves"]; delegate:HeaderButton { required property string modelData; objectName:"videoTimeline-mode-"+modelData; width:Math.max(76,implicitWidth); text:modelData==="Shots" ? qsTr("Layers") : modelData; selectionControl:true; stateSelected:root.stateModel.mode===modelData; onClicked:root.stateModel.mode=modelData } }
                    }
                    TimelineIcon { objectName:"videoTimelineSettings"; asset:"settings"; tooltip:qsTr("Timeline settings"); onClicked:timelineSettings.openFor(this,0,height) }
                }
            }
            Item {
                id:rangeBar; objectName:"videoTimelineRange"; y:40; width:parent.width; height:28; clip:true
                RowLayout {
                    x:8; y:0; width:Math.max(775,parent.width-16); height:parent.height; spacing:8
                    LV.Label { text:qsTr("In"); style:caption }
                    LV.InputField { objectName:"videoTimelineIn"; Layout.preferredWidth:68; text:root.frameLabel(root.stateModel.markIn); clearButtonVisible:false; onAccepted:function(value){root.setRange("markIn",value)} }
                    LV.Label { text:qsTr("Out"); style:caption }
                    LV.InputField { objectName:"videoTimelineOut"; Layout.preferredWidth:68; text:root.hasContent ? root.frameLabel(root.stateModel.markOut) : "—"; enabled:root.hasContent; clearButtonVisible:false; onAccepted:function(value){root.setRange("markOut",value)} }
                    LV.Label { text:qsTr("Duration"); style:caption }
                    LV.InputField { Layout.preferredWidth:68; text:root.hasContent ? String(root.stateModel.markOut-root.stateModel.markIn+1)+"f" : "—"; enabled:false; clearButtonVisible:false }
                    Rectangle { Layout.preferredWidth:1; Layout.preferredHeight:16; color:LV.Theme.panelBackground12 }
                    TimelineIcon { objectName:"videoTimelineSnap"; asset:"snap"; tooltip:qsTr("Snap to frames"); selectable:true; selected:root.stateModel.snap; onClicked:root.stateModel.snap=!root.stateModel.snap }
                    LV.ComboBox { objectName:"videoSnapInterval"; text:qsTr("%1 frame%2").arg(root.snapStep).arg(root.snapStep===1 ? "" : "s"); arrow:LV.Stepper.Down; tone:LV.AbstractButton.Borderless; onClicked:snapMenu.openFor(this,0,height) }
                    TimelineIcon { objectName:"videoTimelineLink"; asset:"link"; tooltip:qsTr("Link selection"); selectable:true; selected:root.stateModel.linked; onClicked:root.stateModel.linked=!root.stateModel.linked }
                    Item { Layout.fillWidth:true; Layout.minimumWidth:0 }
                    LV.Label { text:qsTr("Zoom"); style:caption }
                    LV.ComboBox { objectName:"videoTimelineZoom"; text:Math.round(root.timelineScale*100)+"%"; arrow:LV.Stepper.Down; tone:LV.AbstractButton.Borderless; onClicked:zoomMenu.openFor(this,0,height) }
                    Button { objectName:"videoTimelineFit"; text:qsTr("Fit"); onClicked:root.fitTimeline() }
                }
            }
            LV.ContextMenu { id:snapMenu; items:["1 frame","2 frames","5 frames","10 frames","12 frames","24 frames","30 frames"]; showIconSlot:false; onItemTriggered:function(index){root.snapStep=[1,2,5,10,12,24,30][index]} }
            LV.ContextMenu { id:zoomMenu; items:["25%","50%","75%","100%","150%","200%","400%"]; showIconSlot:false; onItemTriggered:function(index){root.timelineScale=[.25,.5,.75,1,1.5,2,4][index]} }
            LV.ContextMenu { id:timelineSettings; items:[qsTr("Add track"),qsTr("Undo"),qsTr("Redo"),qsTr("Fit timeline")]; showIconSlot:false; onItemTriggered:function(index){if(index===0)addTrackMenu.openFor(header,8,40);else if(index===1)root.stateModel.undo();else if(index===2)root.stateModel.redo();else root.fitTimeline()} }
            LV.ContextMenu { id:addTrackMenu; objectName:"videoAddTrackMenu"; items:root.stateModel.trackTypes.map(type=>type==="Shots" ? qsTr("Layers") : type==="Contents" ? qsTr("Images") : type); showIconSlot:false; onItemTriggered:function(index) { root.stateModel.addTrack(root.stateModel.trackTypes[index]) } }
            LV.ContextMenu { id:trackPositionMenu; items:root.stateModel.tracksOfType(root.stateModel.selectedTrack.type).map((track,index)=>String(index+1)+" · "+track.name); showIconSlot:false; onItemTriggered:function(index) { root.stateModel.moveTrack(root.stateModel.selectedTrackId,index) } }
            LV.ContextMenu {
                id:clipTrackMenu
                readonly property string trackType:root.stateModel.selectedShot.id ? "Shots" : "Contents"
                readonly property var destinations:root.stateModel.tracksOfType(trackType)
                items:destinations.map((track,index)=>String(index+1)+" · "+track.name); showIconSlot:false
                onItemTriggered:function(index) { const id=destinations[index]?.id; if(!id)return; if(root.stateModel.selectedShot.id)root.stateModel.assignShotTrack(root.stateModel.selectedShot.id,id); else root.stateModel.editKey("trackId",id) }
            }
            LV.ContextMenu { id:moreMenu; items:[qsTr("Duplicate shot"),qsTr("Select all"),qsTr("Deselect all")]; showIconSlot:false; onItemTriggered:function(index) { if(index===0) root.stateModel.duplicateShot(); else root.stateModel.selectedIds=index===1 ? root.stateModel.shots.map(shot=>shot.id) : [] } }
            Row {
                id: tools
                objectName:"videoTimelineTools"
                x:8; y:68; width:parent.width-16; height:28; spacing:4; visible:root.stateModel.tracks.length>0; clip:true
                Button { objectName:"videoTimelineUndo"; text:qsTr("Undo"); enabled:root.stateModel.canUndo; onClicked:root.stateModel.undo() }
                Button { text:qsTr("Redo"); enabled:root.stateModel.canRedo; onClicked:root.stateModel.redo() }
                Button { objectName:"videoSplitShot"; text:qsTr("Split at head"); enabled:root.stateModel.shots.length>0; onClicked:root.stateModel.split() }
                Button { objectName:"videoExcludeShot"; text:qsTr("Exclude"); enabled:root.stateModel.selectedIds.length>0; onClicked:root.stateModel.toggleExclude() }
                Button { objectName:"videoRippleDelete"; text:qsTr("Ripple delete"); enabled:root.stateModel.selectedIds.length>0; onClicked:root.stateModel.rippleDelete() }
                Repeater {
                    model:["Select","Blade","Trim","Ripple","Roll","Slip","Slide","Range"]
                    delegate:Button { required property string modelData; objectName:"videoTimeline-tool-"+modelData; text:(modelData==="Select" ? "V " : modelData==="Blade" ? "B " : modelData==="Trim" ? "T " : "")+modelData; selectionControl:true; stateSelected:root.stateModel.tool===modelData; onClicked:root.stateModel.tool=modelData }
                }
                Button { text:root.stateModel.multiSelect ? qsTr("Multi on") : qsTr("Multi"); selectionControl:true; stateSelected:root.stateModel.multiSelect; onClicked:root.stateModel.multiSelect=!root.stateModel.multiSelect }
                Button { text:"I In"; tone:LV.AbstractButton.Borderless; onClicked:root.stateModel.markIn=root.stateModel.playhead }
                Button { text:"O Out"; tone:LV.AbstractButton.Borderless; onClicked:root.stateModel.markOut=root.stateModel.playhead }
                Repeater { model:["All","Kept","Out"]; delegate:Button { required property string modelData; objectName:"videoTimeline-filter-"+modelData; text:modelData; selectionControl:true; stateSelected:root.stateModel.filter===modelData; onClicked:root.stateModel.filter=modelData } }

            }
            Row {
                id:trackControls
                objectName:"videoTimelineTrackControls"
                x:8; y:68+(tools.visible ? tools.height+4 : 0); width:parent.width-16; height:28; spacing:4; clip:true
                visible:!!root.stateModel.selectedTrack.id
                LV.Label { height:24; verticalAlignment:Text.AlignVCenter; text:root.stateModel.selectedTrack.type || ""; style:body }
                LV.InputField { id:trackName; objectName:"videoTrackName"; width:Math.min(160,trackControls.width); height:24; text:root.stateModel.selectedTrack.name || ""; clearButtonVisible:false; Accessible.name:qsTr("Track name"); onAccepted:function(value) { root.stateModel.renameTrack(root.stateModel.selectedTrackId,value) } }
                Button { objectName:"videoRenameTrack"; height:24; text:qsTr("Rename"); onClicked:root.stateModel.renameTrack(root.stateModel.selectedTrackId,trackName.text) }
                LV.ComboBox { objectName:"videoTrackPosition"; width:94; height:24; arrow:LV.Stepper.Down; text:qsTr("Position %1").arg(root.stateModel.trackPosition(root.stateModel.selectedTrackId)+1); onClicked:trackPositionMenu.openFor(this,0,height) }
                Button { objectName:"videoMoveTrackUp"; height:24; text:"↑"; Accessible.name:qsTr("Move track up"); enabled:root.stateModel.trackPosition(root.stateModel.selectedTrackId)>0; onClicked:root.stateModel.moveTrack(root.stateModel.selectedTrackId,root.stateModel.trackPosition(root.stateModel.selectedTrackId)-1) }
                Button { objectName:"videoMoveTrackDown"; height:24; text:"↓"; Accessible.name:qsTr("Move track down"); enabled:root.stateModel.trackPosition(root.stateModel.selectedTrackId)+1<root.stateModel.tracksOfType(root.stateModel.selectedTrack.type).length; onClicked:root.stateModel.moveTrack(root.stateModel.selectedTrackId,root.stateModel.trackPosition(root.stateModel.selectedTrackId)+1) }
                Button { objectName:"videoRemoveTrack"; height:24; text:qsTr("− Track"); Accessible.name:qsTr("Remove track"); onClicked:root.stateModel.removeTrack(root.stateModel.selectedTrackId) }
                LV.ComboBox { objectName:"videoClipTrackDestination"; width:160; height:24; arrow:LV.Stepper.Down; visible:!!root.stateModel.selectedShot.id || !!root.stateModel.selectedKey.id; text:qsTr("Track: %1").arg(root.stateModel.track(root.stateModel.selectedShot.trackId || root.stateModel.selectedKey.trackId).name || ""); onClicked:clipTrackMenu.openFor(this,0,height) }
            }
            LV.Label { id:trackError; objectName:"videoTrackError"; x:8; y:trackControls.visible ? trackControls.y+trackControls.height+4 : tools.y+tools.height+6; width:parent.width-16; visible:root.stateModel.error.length>0; text:root.stateModel.error; style:caption; color:LV.Theme.accentRed; wrapMode:Text.Wrap }
            LV.Label { x:8; y:root.rulerY+10; width:root.gutter-16; text:qsTr("%1 fps · Frames").arg(Number(root.stateModel.fps.toFixed(3))); style:caption; elide:Text.ElideRight }
            LV.Label { x:8; y:root.rulerY+46; visible:root.stateModel.tracks.length===0; text:qsTr("Layers  %1").arg(root.stateModel.tracks.length); style:body }
            LV.IconButton { objectName:"videoAddTrack"; x:root.gutter-30; y:root.rulerY+42; iconSize:16; iconSource:Qt.resolvedUrl("Assets/add.svg"); tone:LV.AbstractButton.Default; Accessible.name:qsTr("Add track"); onClicked:addTrackMenu.openFor(this,0,height) }
            Repeater { model:root.trackLayout; delegate:TrackHeading { required property var modelData; trackData:modelData; y:root.rulerY+modelData.top; height:modelData.height } }
            Flickable {
                id: lanes
                objectName:"videoTimelineLanes"
                x:root.gutter; y:root.rulerY; width:parent.width-x; height:root.lanesHeight
                contentWidth:Math.max(width,root.stateModel.totalFrames*root.framePitch*root.timelineScale); contentHeight:height
                flickableDirection:Flickable.HorizontalFlick; boundsBehavior:Flickable.StopAtBounds; clip:true
                Controls.ScrollBar.horizontal:Controls.ScrollBar { policy:Controls.ScrollBar.AsNeeded }
                Item {
                    id: laneContent
                    width:lanes.contentWidth; height:lanes.height
                    Item {
                        id: frameRuler
                        objectName:"videoFrameRuler"
                        readonly property int majorStep: { const needed=Math.max(10,64/root.pixelsPerFrame), magnitude=Math.pow(10,Math.floor(Math.log10(needed))); return [1,2,5,10].map(value=>value*magnitude).find(value=>value>=needed) }
                        readonly property int minorStep:Math.max(1,majorStep/10)
                        readonly property int firstFrame:Math.max(0,Math.floor(lanes.contentX/root.pixelsPerFrame/minorStep)*minorStep)
                        readonly property int tickCount:Math.max(0,Math.ceil(lanes.width/(root.pixelsPerFrame*minorStep))+2)
                        readonly property int firstMajor:Math.floor(firstFrame/majorStep)*majorStep
                        readonly property int gridCount:Math.max(0,Math.ceil(lanes.width/(root.pixelsPerFrame*majorStep))+2)
                        width:laneContent.width; height:32
                        Repeater {
                            model:frameRuler.tickCount
                            delegate:Item {
                                required property int index
                                readonly property int frame:frameRuler.firstFrame+index*frameRuler.minorStep
                                objectName:"videoTimelineTick-"+frame
                                x:frame*root.pixelsPerFrame; width:root.pixelsPerFrame; height:32
                                LV.Label { x:3; y:2; text:root.frameLabel(parent.frame); visible:parent.frame%frameRuler.majorStep===0; style:caption; color:LV.Theme.captionColor }
                                Rectangle { x:0; y:parent.frame%frameRuler.majorStep===0 ? 18 : 24; height:parent.frame%frameRuler.majorStep===0 ? 12 : 6; width:1; color:LV.Theme.captionColor }
                            }
                        }
                        Rectangle { objectName:"videoRulerBaseline"; y:30; width:parent.width; height:1; color:LV.Theme.accentYellow }

                        MouseArea { anchors.fill:parent; onPressed:function(mouse){ root.forceActiveFocus(); root.seekFrame(mouse.x/root.pixelsPerFrame,true) }; onPositionChanged:function(mouse){ if(pressed) root.seekFrame(mouse.x/root.pixelsPerFrame,true) } }
                    }
                    Item { y:32; width:parent.width; height:164; visible:root.stateModel.tracks.length===0
                        Repeater { model:frameRuler.gridCount; delegate:Rectangle { required property int index; x:(frameRuler.firstMajor+index*frameRuler.majorStep)*root.pixelsPerFrame; width:1; height:parent.height; color:LV.Theme.panelBackground08 } }
                        LV.Label { objectName:"videoTimelineEmptyState"; visible:false; text:qsTr("No tracks"); style:caption }
                    }
                    Repeater {
                        model:root.trackLayout
                        delegate:Rectangle {
                            id:trackRow
                            required property var modelData
                            objectName:"videoTrackRow-"+modelData.id
                            y:modelData.top; width:laneContent.width; height:modelData.height; color:LV.Theme.panelBackground02
                            MouseArea { anchors.fill:parent; onClicked:root.stateModel.selectTrack(trackRow.modelData.id) }
                            Repeater { model:frameRuler.gridCount; delegate:Rectangle { required property int index; objectName:"videoTrackGrid-"+trackRow.modelData.id+"-"+index; x:(frameRuler.firstMajor+index*frameRuler.majorStep)*root.pixelsPerFrame; width:1; height:trackRow.height; color:LV.Theme.panelBackground08 } }
                            Item {
                                id: shotLane
                                objectName:"videoShotLane-"+trackRow.modelData.id
                                width:parent.width; height:56; visible:trackRow.modelData.type==="Shots"
                                LV.Label { objectName:"videoShotEmptyState-"+trackRow.modelData.id; x:12; anchors.verticalCenter:parent.verticalCenter; visible:!root.stateModel.shots.some(shot=>shot.trackId===trackRow.modelData.id); text:qsTr("No shots"); style:caption; color:LV.Theme.textSecondary }
                                Repeater {
                                    model:root.stateModel.shots.filter(shot=>shot.trackId===trackRow.modelData.id)
                                    delegate:Rectangle {
                                        id: shot
                                        required property var modelData
                                        required property int index
                                        readonly property bool selected:root.stateModel.selectedIds.indexOf(modelData.id)>=0
                                        readonly property int start:root.stateModel.startOf(modelData.id)
                                        readonly property string referenceSource:(root.stateModel.keys.find(key=>key.frame>=start && key.frame<start+modelData.frames) || {}).source || ""
                                        x:start*root.pixelsPerFrame+2
                                        width:Math.max(1,modelData.frames*root.pixelsPerFrame-4); height:50; y:3
                                        radius:12; clip:true
                                        visible:root.stateModel.filter==="All" || (root.stateModel.filter==="Out" ? modelData.excluded : !modelData.excluded)
                                        color:LV.Theme.panelBackground01; border.width:selected ? 2 : 1; border.color:selected ? LV.Theme.accent : LV.Theme.panelBackground12
                                        opacity:modelData.excluded ? .35 : 1
                                        Image { objectName:"videoShotReference-"+shot.modelData.id; anchors.fill:parent; source:displayPreviews.sourceForUrl(shot.referenceSource || ""); asynchronous:true; sourceSize:Qt.size(Math.ceil(width*Screen.devicePixelRatio),Math.ceil(height*Screen.devicePixelRatio)); fillMode:Image.PreserveAspectCrop; opacity:.18 }
                                        LV.Label { x:12; y:4; width:Math.max(0,parent.width-24); text:String(root.stateModel.shots.findIndex(entry=>entry.id===shot.modelData.id)+1).padStart(2,"0")+" / "+shot.modelData.name; style:body; elide:Text.ElideRight }
                                        LV.Label { x:12; y:27; width:Math.max(0,parent.width-24); text:"F"+String(shot.start).padStart(3,"0")+"–"+String(shot.start+shot.modelData.frames-1).padStart(3,"0")+" · "+shot.modelData.frames+"f"+(shot.modelData.excluded ? " · Excluded" : ""); style:caption; elide:Text.ElideRight }
                                        MouseArea {
                                            anchors.fill:parent
                                            property real pressX:0
                                            onPressed:function(mouse) { root.forceActiveFocus(); pressX=mouse.x; root.stateModel.selectShot(shot.modelData.id,!!(mouse.modifiers & Qt.ShiftModifier)); if(root.stateModel.tool==="Blade") { root.stateModel.seek(shot.start+mouse.x/shot.width*shot.modelData.frames); root.stateModel.split() } }
                                            onReleased:function(mouse) { const delta=Math.round((mouse.x-pressX)/root.pixelsPerFrame); if(root.stateModel.tool==="Slide") root.stateModel.nudgeShot(delta); else if(root.stateModel.tool==="Slip") root.stateModel.slipKeys(shot.modelData.id,delta); else if(root.stateModel.tool==="Range") { root.stateModel.markIn=shot.start; root.stateModel.markOut=shot.start+shot.modelData.frames-1; root.stateModel.selectedIds=root.stateModel.shots.filter(entry=>root.stateModel.startOf(entry.id)<=root.stateModel.markOut && root.stateModel.startOf(entry.id)+entry.frames>root.stateModel.markIn).map(entry=>entry.id) } }
                                            onDoubleClicked:{ root.stateModel.seek(shot.start); root.stateModel.mode="Shots" }
                                        }
                                        Repeater {
                                            model:["in","out"]
                                            delegate:Rectangle {
                                                id: handle
                                                required property string modelData
                                                x:modelData==="in" ? 0 : shot.width-4; y:4; height:42; width:4; color:LV.Theme.accent; visible:shot.selected
                                                MouseArea { anchors.fill:parent; anchors.margins:-4; cursorShape:Qt.SizeHorCursor; property real pressX:0; onPressed:function(mouse){ pressX=mapToItem(shotLane,mouse.x,0).x }; onReleased:function(mouse){ const boundary=shot.start+(handle.modelData==="out" ? shot.modelData.frames : 0); const delta=root.stateModel.snappedFrame(boundary+(mapToItem(shotLane,mouse.x,0).x-pressX)/root.pixelsPerFrame)-boundary; root.stateModel.trim(shot.modelData.id,handle.modelData,delta,root.stateModel.tool) } }
                                            }
                                        }
                                    }
                                }
                            }
                            Button { x:8; anchors.verticalCenter:parent.verticalCenter; visible:trackRow.modelData.type==="Contents" && !root.stateModel.keys.some(key=>key.trackId===trackRow.modelData.id); text:qsTr("+ Image keyframe"); tone:LV.AbstractButton.Borderless; onClicked:root.chooseKeyframe(root.stateModel.playhead,trackRow.modelData.id) }
                            LV.Label { x:12; anchors.verticalCenter:parent.verticalCenter; text:trackRow.modelData.type==="Caption" ? qsTr("No captions") : qsTr("No audio"); style:caption; color:LV.Theme.textSecondary; visible:trackRow.modelData.type==="Audio" || trackRow.modelData.type==="Caption" }
                            Repeater {
                                model:root.stateModel.keys.filter(key=>key.trackId===trackRow.modelData.id)
                                delegate:Rectangle {
                                    id:key
                                    required property var modelData
                                    x:key.modelData.frame*root.pixelsPerFrame; y:4; height:32; width:Math.min(184,trackRow.width); radius:6
                                    color:LV.Theme.panelBackground08; border.width:root.stateModel.selectedKeyId===modelData.id ? 1 : 0; border.color:LV.Theme.accent
                                    Image { objectName:"videoKeyImage"+key.modelData.id; x:4; y:2; width:32; height:28; source:displayPreviews.sourceForUrl(key.modelData.source); asynchronous:true; sourceSize:Qt.size(Math.ceil(width*Screen.devicePixelRatio),Math.ceil(height*Screen.devicePixelRatio)); fillMode:Image.PreserveAspectCrop }
                                    LV.Label { x:44; y:2; width:Math.max(0,key.width-48); text:key.modelData.name; style:body; elide:Text.ElideRight }
                                    LV.Label { x:44; y:17; width:Math.max(0,key.width-48); elide:Text.ElideRight; text:"F"+String(key.modelData.frame).padStart(3,"0")+" · weight "+key.modelData.value.toFixed(2); style:caption }
                                    MouseArea {
                                        anchors.fill:parent; property real pressX:0
                                        onPressed:function(mouse) { root.stateModel.selectedTrackId=key.modelData.trackId; root.stateModel.selectedIds=[]; root.stateModel.selectedKeyId=key.modelData.id; root.stateModel.mode="Keyframes"; pressX=mapToItem(laneContent,mouse.x,0).x }
                                        onReleased:function(mouse) { const delta=Math.round((mapToItem(laneContent,mouse.x,0).x-pressX)/root.pixelsPerFrame); if(delta) root.stateModel.editKey("frame",key.modelData.frame+delta) }
                                    }
                                }
                            }
                            Item {
                                y:40; width:parent.width; height:80; visible:trackRow.modelData.type==="Contents" && root.stateModel.mode==="Curves"
                                Rectangle { anchors.fill:parent; color:LV.Theme.panelBackground02 }
                                Canvas {
                                    id:curve; anchors.fill:parent
                                    property real frameScale:root.pixelsPerFrame
                                    onFrameScaleChanged:requestPaint()
                                    Connections { target:root.stateModel; function onKeysChanged(){ curve.requestPaint() } function onTotalFramesChanged(){ curve.requestPaint() } }
                                    onWidthChanged:requestPaint()
                                    onPaint: { const c=getContext("2d"); c.clearRect(0,0,width,height); c.strokeStyle=LV.Theme.accent; c.lineWidth=2; c.beginPath(); const ordered=root.stateModel.keys.filter(key=>key.trackId===trackRow.modelData.id).sort((a,b)=>a.frame-b.frame); ordered.forEach((key,index)=>{ const x=key.frame*curve.frameScale,y=(1-key.value)*height; if(index===0)c.moveTo(x,y); else c.lineTo(x,y) }); c.stroke() }
                                }
                                LV.Label { x:8; y:4; width:parent.width-16; text:qsTr("Image condition weight"); style:caption; elide:Text.ElideRight }
                            }
                        }
                    }
                    Rectangle { objectName:"videoPlayheadLine"; x:root.stateModel.playhead*root.pixelsPerFrame-1; y:32; width:2; height:parent.height-32; color:LV.Theme.accentBlue }

                }
            }
            Image { objectName:"videoPlayheadMarker"; x:root.gutter+root.stateModel.playhead*root.pixelsPerFrame-lanes.contentX-11; y:root.rulerY+6; width:22; height:26; source:Qt.resolvedUrl("Assets/playhead.svg"); fillMode:Image.PreserveAspectFit; visible:x>=root.gutter-11 && x<root.width }
            LV.Label { x:4; y:selection.y+6; width:root.gutter-8; visible:root.wideControls; text:root.stateModel.mode==="Shots" ? qsTr("Shot selection") : qsTr("Key selection"); style:body }
            Item {
                id: selection
                y:root.rulerY+root.lanesHeight; width:parent.width; visible:root.hasContent || root.stateModel.tracks.length>0
                height:Math.max(34,(root.stateModel.mode==="Shots" ? shotControls.height : keyControls.height)+9)
                Flow {
                    id: shotControls
                    objectName:"videoShotSelectionControls"
                    x:root.wideControls ? root.gutter : 8; y:3; width:parent.width-x-8; spacing:4
                    visible:root.stateModel.mode==="Shots"
                    LV.Label { objectName:"videoShotSelectionEmptyState"; visible:!root.stateModel.selectedShot.id; text:qsTr("No shot selected"); style:caption; color:LV.Theme.textSecondary }
                    Button { visible:!!root.stateModel.selectedShot.id; height:24; text:"In F"+String(root.stateModel.selectedStart).padStart(3,"0") }
                    Button { visible:!!root.stateModel.selectedShot.id; height:24; text:"Out F"+String(root.stateModel.selectedEnd).padStart(3,"0") }
                    Button { visible:!!root.stateModel.selectedShot.id; height:24; text:(root.stateModel.selectedShot.frames || 0)+"f" }
                    Repeater { model:root.stateModel.selectedShot.id ? [-12,-1,1,12] : []; delegate:Button { required property int modelData; height:24; text:(modelData>0 ? "+" : "")+modelData+"f In"; onClicked:root.stateModel.trim(root.stateModel.selectedShot.id,"in",modelData,root.stateModel.tool) } }
                    Button { visible:!!root.stateModel.selectedShot.id; height:24; text:qsTr("Keep"); onClicked:root.stateModel.changeShot(root.stateModel.selectedShot.id,"excluded",false) }
                    Button { visible:!!root.stateModel.selectedShot.id; height:24; text:qsTr("Exclude"); onClicked:root.stateModel.changeShot(root.stateModel.selectedShot.id,"excluded",true) }
                    Button { visible:!!root.stateModel.selectedShot.id; height:24; text:root.stateModel.selectedShot.locked ? qsTr("Unlock") : qsTr("Lock"); onClicked:root.stateModel.changeShot(root.stateModel.selectedShot.id,"locked",!root.stateModel.selectedShot.locked) }
                    Button { visible:!!root.stateModel.selectedShot.id; height:24; text:qsTr("Duplicate"); onClicked:root.stateModel.duplicateShot() }
                }
                Flow {
                    id: keyControls
                    objectName:"videoKeySelectionControls"
                    x:root.wideControls ? root.gutter : 8; y:3; width:parent.width-x-8; spacing:4
                    visible:root.stateModel.mode!=="Shots"
                    LV.Label { visible:!root.stateModel.selectedKey.id; text:qsTr("No keyframe selected"); style:caption; color:LV.Theme.textSecondary }
                    Button { visible:!!root.stateModel.selectedKey.id; height:24; text:"Frame F"+root.stateModel.selectedKey.frame }
                    Button { visible:!!root.stateModel.selectedKey.id; height:24; text:"Value "+(root.stateModel.selectedKey.value || 0).toFixed(2); onClicked:root.stateModel.editKey("value",Math.min(1,root.stateModel.selectedKey.value+.05)) }
                    Repeater { model:root.stateModel.selectedKey.id ? [-12,-1,1,12] : []; delegate:Button { required property int modelData; height:24; text:(modelData>0 ? "+" : "")+modelData+"f"; onClicked:root.stateModel.editKey("frame",root.stateModel.selectedKey.frame+modelData) } }
                    Button { visible:!!root.stateModel.selectedKey.id; height:24; text:qsTr("− Weight"); onClicked:root.stateModel.editKey("value",Math.max(.001,root.stateModel.selectedKey.value-.05)) }
                    Button { visible:!!root.stateModel.selectedKey.id; height:24; text:qsTr("Copy"); onClicked:root.stateModel.copyKey() }
                    Button { visible:root.stateModel.clipboardKey.length>0; height:24; text:qsTr("Paste"); onClicked:root.stateModel.pasteKey() }
                    Button { objectName:"videoDeleteKey"; visible:!!root.stateModel.selectedKey.id; height:24; text:qsTr("Delete"); onClicked:root.stateModel.deleteKey() }
                }
            }
            Flow {
                id: footer
                objectName:"videoTimelineFooter"
                x:4; y:selection.y+selection.height+3; width:parent.width-12; spacing:12; visible:root.hasContent || root.stateModel.tracks.length>0
                LV.Label { width:Math.min(432,footer.width); text:qsTr("%1 tracks · %2 shots · %3 kept · %4 keys · %5 output frames").arg(root.stateModel.tracks.length).arg(root.stateModel.shots.length).arg(root.stateModel.shots.filter(shot=>!shot.excluded).length).arg(root.stateModel.keys.length).arg(root.stateModel.outputFrames); style:body; wrapMode:Text.Wrap }
                Item { width:Math.min(480,footer.width); height:17; visible:root.stateModel.shots.length>0; Row { width:parent.width; y:5; spacing:3; Repeater { model:root.stateModel.shots; delegate:Rectangle { required property var modelData; width:Math.max(0,modelData.frames/root.stateModel.totalFrames*parent.width-3); height:8; radius:2; color:root.stateModel.selectedIds.indexOf(modelData.id)>=0 ? LV.Theme.accent : LV.Theme.accentGray; opacity:modelData.excluded ? .3 : 1 } } } }
                LV.Label { width:Math.min(300,footer.width); text:qsTr("View F000–%1 · In %2 · Out %3").arg(root.stateModel.totalFrames-1).arg(root.stateModel.markIn).arg(root.stateModel.markOut); style:body; wrapMode:Text.Wrap }
                LV.Label { visible:root.width>=600; text:"V select · B blade · I/O · Space"; style:caption }
            }
        }
    }
}
