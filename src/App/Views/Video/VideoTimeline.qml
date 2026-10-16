pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import "../Editor" as Editor

FocusScope {
    id: root
    objectName:"videoTimeline"
    required property VideoTimelineState stateModel
    property var generation:null
    property bool playing:false
    property real timelineScale:1
    readonly property int gutter:width < 600 ? 88 : width < 1100 ? 144 : 192
    readonly property bool wideControls:width >= 1100
    readonly property real rulerY:tools.y+tools.height+3+(trackControls.visible ? trackControls.height+8 : 0)+(trackError.visible ? trackError.height+8 : 0)
    readonly property var trackLayout: {
        let top=24
        return stateModel.tracks.map(track=>{
            const height=track.type==="Shots" ? 56 : track.type==="Contents" ? 40+(stateModel.mode==="Curves" ? 80 : 0) : track.type==="Audio" ? 48 : 32
            const row=Object.assign({},track,{top:top,height:height}); top+=height; return row
        })
    }
    readonly property real lanesHeight:trackLayout.length ? 24+trackLayout.reduce((height,track)=>height+track.height,0) : 104
    signal chooseKeyframe(int frame, string trackId)
    readonly property int curveHeight:stateModel.mode==="Curves" ? stateModel.tracksOfType("Contents").length*80 : 0
    readonly property bool hasContent:stateModel.shots.length>0 || stateModel.keys.length>0
    property bool playbackAvailable:hasContent
    implicitHeight:footer.y + Math.max(17,footer.height) + 6
    function seekFrame(frame) { playing=false; stateModel.seek(frame) }
    function fitTimeline() { timelineScale=1; lanes.contentX=0; viewport.contentY=0 }
    onVisibleChanged: if(!visible) playing=false
    onEnabledChanged: if(!enabled) playing=false
    Connections { target:root.stateModel; function onResetCompleted() { root.playing=false; root.fitTimeline() } }
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
            if(next>=root.stateModel.totalFrames) { root.playing=false; root.stateModel.seek(0) }
            else root.stateModel.seek(next)
        }
    }
    component Button: LV.LabelButton {
        id: control
        property bool selectionControl:false
        property bool stateSelected:false
        height:22; verticalPadding:2; horizontalPadding:8
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
    component HeaderButton: Button { height:28 }
    Rectangle { anchors.fill:parent; color:LV.Theme.panelBackground08 }
    Flickable {
        id: viewport
        objectName:"videoTimelineViewport"
        anchors.fill:parent; contentWidth:width; contentHeight:root.implicitHeight
        flickableDirection:Flickable.VerticalFlick; boundsBehavior:Flickable.StopAtBounds; clip:true
        Controls.ScrollBar.vertical:Controls.ScrollBar { policy:Controls.ScrollBar.AsNeeded }
        Item {
            id: panel
            width:viewport.width; height:root.implicitHeight
            Flow {
                id: header
                objectName:"videoTimelineHeader"
                x:8; y:4; width:parent.width-16; spacing:4
                HeaderButton { objectName:"videoTimecode"; width:156; text:qsTr("%1 · F%2").arg((root.stateModel.playhead/root.stateModel.fps).toFixed(3)+" s").arg(String(root.stateModel.playhead).padStart(3,"0")) }
                HeaderButton { text:"‹"; tone:LV.AbstractButton.Borderless; Accessible.name:qsTr("Previous cut"); onClicked:{ root.playing=false; root.stateModel.jumpCut(-1) } }
                HeaderButton { objectName:"videoTimelinePlay"; text:root.playing ? "Ⅱ" : "▶"; enabled:root.playbackAvailable; Accessible.name:root.playing ? qsTr("Pause timeline") : qsTr("Play timeline"); onClicked:root.playing=!root.playing }
                HeaderButton { text:"›"; tone:LV.AbstractButton.Borderless; Accessible.name:qsTr("Next cut"); onClicked:{ root.playing=false; root.stateModel.jumpCut(1) } }
                HeaderButton { text:"−1f"; tone:LV.AbstractButton.Borderless; onClicked:root.seekFrame(root.stateModel.playhead-1) }
                HeaderButton { text:"+1f"; tone:LV.AbstractButton.Borderless; onClicked:root.seekFrame(root.stateModel.playhead+1) }
                Repeater { model:["Shots","Keyframes","Curves"]; delegate:HeaderButton { required property string modelData; objectName:"videoTimeline-mode-"+modelData; text:modelData; selectionControl:true; stateSelected:root.stateModel.mode===modelData; onClicked:root.stateModel.mode=modelData } }
                HeaderButton { objectName:"videoTimelineUndo"; text:qsTr("Undo"); tone:LV.AbstractButton.Borderless; enabled:root.stateModel.canUndo; onClicked:root.stateModel.undo() }
                HeaderButton { text:qsTr("Redo"); tone:LV.AbstractButton.Borderless; enabled:root.stateModel.canRedo; onClicked:root.stateModel.redo() }
                HeaderButton { objectName:"videoSplitShot"; text:qsTr("Split at head"); enabled:root.stateModel.shots.length>0; onClicked:root.stateModel.split() }
                HeaderButton { objectName:"videoExcludeShot"; text:qsTr("Exclude"); enabled:root.stateModel.selectedIds.length>0; onClicked:root.stateModel.toggleExclude() }
                HeaderButton { objectName:"videoRippleDelete"; text:qsTr("Ripple delete"); enabled:root.stateModel.selectedIds.length>0; onClicked:root.stateModel.rippleDelete() }
                HeaderButton { objectName:"videoAddTrack"; text:qsTr("+ Track"); onClicked:addTrackMenu.openFor(this,0,height) }
                HeaderButton { objectName:"videoAddShot"; text:qsTr("+ Shot"); onClicked:root.stateModel.addShot() }
                HeaderButton { text:"···"; tone:LV.AbstractButton.Borderless; Accessible.name:qsTr("More timeline actions"); onClicked:moreMenu.openFor(this,0,height) }
            }
            LV.ContextMenu { id:addTrackMenu; objectName:"videoAddTrackMenu"; items:root.stateModel.trackTypes; showIconSlot:false; onItemTriggered:function(index) { root.stateModel.addTrack(root.stateModel.trackTypes[index]) } }
            LV.ContextMenu { id:trackPositionMenu; items:root.stateModel.tracksOfType(root.stateModel.selectedTrack.type).map((track,index)=>String(index+1)+" · "+track.name); showIconSlot:false; onItemTriggered:function(index) { root.stateModel.moveTrack(root.stateModel.selectedTrackId,index) } }
            LV.ContextMenu {
                id:clipTrackMenu
                readonly property string trackType:root.stateModel.selectedShot.id ? "Shots" : "Contents"
                readonly property var destinations:root.stateModel.tracksOfType(trackType)
                items:destinations.map((track,index)=>String(index+1)+" · "+track.name); showIconSlot:false
                onItemTriggered:function(index) { const id=destinations[index]?.id; if(!id)return; if(root.stateModel.selectedShot.id)root.stateModel.assignShotTrack(root.stateModel.selectedShot.id,id); else root.stateModel.editKey("trackId",id) }
            }
            LV.ContextMenu { id:moreMenu; items:[qsTr("Duplicate shot"),qsTr("Select all"),qsTr("Deselect all")]; showIconSlot:false; onItemTriggered:function(index) { if(index===0) root.stateModel.duplicateShot(); else root.stateModel.selectedIds=index===1 ? root.stateModel.shots.map(shot=>shot.id) : [] } }
            LV.Label { x:4; y:tools.y+3; width:root.gutter-8; visible:root.wideControls; text:qsTr("Edit tools / selection"); style:body; elide:Text.ElideRight }
            Flow {
                id: tools
                objectName:"videoTimelineTools"
                x:root.wideControls ? root.gutter : 8; y:header.height+11; width:parent.width-x-8; spacing:4
                Repeater {
                    model:["Select","Blade","Trim","Ripple","Roll","Slip","Slide","Range"]
                    delegate:Button { required property string modelData; objectName:"videoTimeline-tool-"+modelData; text:(modelData==="Select" ? "V " : modelData==="Blade" ? "B " : modelData==="Trim" ? "T " : "")+modelData; selectionControl:true; stateSelected:root.stateModel.tool===modelData; onClicked:root.stateModel.tool=modelData }
                }
                Button { text:root.stateModel.multiSelect ? qsTr("Multi on") : qsTr("Multi"); selectionControl:true; stateSelected:root.stateModel.multiSelect; onClicked:root.stateModel.multiSelect=!root.stateModel.multiSelect }
                Button { text:"I In"; tone:LV.AbstractButton.Borderless; onClicked:root.stateModel.markIn=root.stateModel.playhead }
                Button { text:"O Out"; tone:LV.AbstractButton.Borderless; onClicked:root.stateModel.markOut=root.stateModel.playhead }
                Repeater { model:["All","Kept","Out"]; delegate:Button { required property string modelData; objectName:"videoTimeline-filter-"+modelData; text:modelData; selectionControl:true; stateSelected:root.stateModel.filter===modelData; onClicked:root.stateModel.filter=modelData } }
                Button { text:root.stateModel.snap ? "Snap on" : "Snap off"; selectionControl:true; stateSelected:root.stateModel.snap; onClicked:root.stateModel.snap=!root.stateModel.snap }
                Button { text:root.stateModel.linked ? "Link on" : "Link off"; selectionControl:true; stateSelected:root.stateModel.linked; onClicked:root.stateModel.linked=!root.stateModel.linked }
                Button { objectName:"videoTimelineZoom"; text:Math.round(root.timelineScale*100)+"%"; onClicked:root.timelineScale=root.timelineScale>=2 ? 1 : root.timelineScale+.25 }
                Button { objectName:"videoTimelineFit"; text:qsTr("Fit"); tone:LV.AbstractButton.Borderless; onClicked:root.fitTimeline() }
            }
            Flow {
                id:trackControls
                objectName:"videoTimelineTrackControls"
                x:8; y:tools.y+tools.height+6; width:parent.width-16; spacing:4
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
            LV.Label { x:4; y:root.rulerY+4; width:root.gutter-8; text:qsTr("%1 fps · frame number").arg(Number(root.stateModel.fps.toFixed(3))); style:body; elide:Text.ElideRight }
            Repeater { model:root.trackLayout; delegate:TrackHeading { required property var modelData; trackData:modelData; y:root.rulerY+modelData.top; height:modelData.height } }
            Flickable {
                id: lanes
                objectName:"videoTimelineLanes"
                x:root.gutter; y:root.rulerY; width:parent.width-x-8; height:root.lanesHeight
                contentWidth:width*root.timelineScale; contentHeight:height
                flickableDirection:Flickable.HorizontalFlick; boundsBehavior:Flickable.StopAtBounds; clip:true
                Controls.ScrollBar.horizontal:Controls.ScrollBar { policy:Controls.ScrollBar.AsNeeded }
                Item {
                    id: laneContent
                    width:lanes.contentWidth; height:lanes.height
                    Item {
                        id: frameRuler
                        objectName:"videoFrameRuler"
                        readonly property int divisionCount:Math.min(12,root.stateModel.totalFrames,Math.max(1,Math.floor(width/64)))
                        width:laneContent.width; height:24
                        Repeater { model:frameRuler.divisionCount; delegate:Item { required property int index; x:index*frameRuler.width/frameRuler.divisionCount; width:frameRuler.width/frameRuler.divisionCount; height:24; LV.Label { width:parent.width; text:"F"+String(Math.floor(index*root.stateModel.totalFrames/frameRuler.divisionCount)).padStart(3,"0"); style:caption; color:LV.Theme.textSecondary; elide:Text.ElideRight } Rectangle { y:16; height:6; width:1; color:LV.Theme.accentGray } } }
                        MouseArea { anchors.fill:parent; onPressed:function(mouse){ root.forceActiveFocus(); root.seekFrame(mouse.x/width*root.stateModel.totalFrames) }; onPositionChanged:function(mouse){ if(pressed) root.seekFrame(mouse.x/width*root.stateModel.totalFrames) } }
                    }
                    Item { y:24; width:parent.width; height:80; visible:root.stateModel.tracks.length===0; LV.Label { objectName:"videoTimelineEmptyState"; anchors.centerIn:parent; text:qsTr("No tracks"); style:body; color:LV.Theme.textSecondary } }
                    Repeater {
                        model:root.trackLayout
                        delegate:Rectangle {
                            id:trackRow
                            required property var modelData
                            objectName:"videoTrackRow-"+modelData.id
                            y:modelData.top; width:laneContent.width; height:modelData.height; color:LV.Theme.panelBackground02
                            MouseArea { anchors.fill:parent; onClicked:root.stateModel.selectTrack(trackRow.modelData.id) }
                            Repeater { model:frameRuler.divisionCount; delegate:Rectangle { required property int index; x:index*laneContent.width/frameRuler.divisionCount; width:1; height:trackRow.height; color:LV.Theme.panelBackground08 } }
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
                                        x:start/root.stateModel.totalFrames*shotLane.width+2
                                        width:Math.max(1,modelData.frames/root.stateModel.totalFrames*shotLane.width-4); height:50; y:3
                                        radius:12; clip:true
                                        visible:root.stateModel.filter==="All" || (root.stateModel.filter==="Out" ? modelData.excluded : !modelData.excluded)
                                        color:LV.Theme.panelBackground01; border.width:selected ? 2 : 1; border.color:selected ? LV.Theme.accent : LV.Theme.panelBackground12
                                        opacity:modelData.excluded ? .35 : 1
                                        Image { anchors.fill:parent; source:shot.referenceSource ? (root.generation ? root.generation.imagePreview(shot.referenceSource) : shot.referenceSource) : ""; fillMode:Image.PreserveAspectCrop; opacity:.18 }
                                        LV.Label { x:12; y:4; width:Math.max(0,parent.width-24); text:String(root.stateModel.shots.findIndex(entry=>entry.id===shot.modelData.id)+1).padStart(2,"0")+" / "+shot.modelData.name; style:body; elide:Text.ElideRight }
                                        LV.Label { x:12; y:27; width:Math.max(0,parent.width-24); text:"F"+String(shot.start).padStart(3,"0")+"–"+String(shot.start+shot.modelData.frames-1).padStart(3,"0")+" · "+shot.modelData.frames+"f"+(shot.modelData.excluded ? " · Excluded" : ""); style:caption; elide:Text.ElideRight }
                                        MouseArea {
                                            anchors.fill:parent
                                            property real pressX:0
                                            onPressed:function(mouse) { root.forceActiveFocus(); pressX=mouse.x; root.stateModel.selectShot(shot.modelData.id,!!(mouse.modifiers & Qt.ShiftModifier)); if(root.stateModel.tool==="Blade") { root.stateModel.seek(shot.start+mouse.x/shot.width*shot.modelData.frames); root.stateModel.split() } }
                                            onReleased:function(mouse) { const delta=Math.round((mouse.x-pressX)/shotLane.width*root.stateModel.totalFrames); if(root.stateModel.tool==="Slide") root.stateModel.nudgeShot(delta); else if(root.stateModel.tool==="Slip") root.stateModel.slipKeys(shot.modelData.id,delta); else if(root.stateModel.tool==="Range") { root.stateModel.markIn=shot.start; root.stateModel.markOut=shot.start+shot.modelData.frames-1; root.stateModel.selectedIds=root.stateModel.shots.filter(entry=>root.stateModel.startOf(entry.id)<=root.stateModel.markOut && root.stateModel.startOf(entry.id)+entry.frames>root.stateModel.markIn).map(entry=>entry.id) } }
                                            onDoubleClicked:{ root.stateModel.seek(shot.start); root.stateModel.mode="Shots" }
                                        }
                                        Repeater {
                                            model:["in","out"]
                                            delegate:Rectangle {
                                                id: handle
                                                required property string modelData
                                                x:modelData==="in" ? 0 : shot.width-4; y:4; height:42; width:4; color:LV.Theme.accent; visible:shot.selected
                                                MouseArea { anchors.fill:parent; anchors.margins:-4; cursorShape:Qt.SizeHorCursor; property real pressX:0; onPressed:function(mouse){ pressX=mapToItem(shotLane,mouse.x,0).x }; onReleased:function(mouse){ const boundary=shot.start+(handle.modelData==="out" ? shot.modelData.frames : 0); const delta=root.stateModel.snappedFrame(boundary+(mapToItem(shotLane,mouse.x,0).x-pressX)/shotLane.width*root.stateModel.totalFrames)-boundary; root.stateModel.trim(shot.modelData.id,handle.modelData,delta,root.stateModel.tool) } }
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
                                    x:Math.min(trackRow.width-width,key.modelData.frame/root.stateModel.totalFrames*laneContent.width); y:4; height:32; width:Math.min(184,trackRow.width); radius:6
                                    color:LV.Theme.panelBackground08; border.width:root.stateModel.selectedKeyId===modelData.id ? 1 : 0; border.color:LV.Theme.accent
                                    Image { objectName:"videoKeyImage"+key.modelData.id; x:4; y:2; width:32; height:28; source:root.generation ? root.generation.imagePreview(key.modelData.source) : key.modelData.source; fillMode:Image.PreserveAspectCrop }
                                    LV.Label { x:44; y:2; width:Math.max(0,key.width-48); text:key.modelData.name; style:body; elide:Text.ElideRight }
                                    LV.Label { x:44; y:17; width:Math.max(0,key.width-48); elide:Text.ElideRight; text:"F"+String(key.modelData.frame).padStart(3,"0")+" · weight "+key.modelData.value.toFixed(2); style:caption }
                                    MouseArea {
                                        anchors.fill:parent; property real pressX:0
                                        onPressed:function(mouse) { root.stateModel.selectedTrackId=key.modelData.trackId; root.stateModel.selectedIds=[]; root.stateModel.selectedKeyId=key.modelData.id; root.stateModel.mode="Keyframes"; pressX=mapToItem(laneContent,mouse.x,0).x }
                                        onReleased:function(mouse) { const delta=Math.round((mapToItem(laneContent,mouse.x,0).x-pressX)/laneContent.width*root.stateModel.totalFrames); if(delta) root.stateModel.editKey("frame",key.modelData.frame+delta) }
                                    }
                                }
                            }
                            Item {
                                y:40; width:parent.width; height:80; visible:trackRow.modelData.type==="Contents" && root.stateModel.mode==="Curves"
                                Rectangle { anchors.fill:parent; color:LV.Theme.panelBackground02 }
                                Canvas {
                                    id:curve; anchors.fill:parent
                                    Connections { target:root.stateModel; function onKeysChanged(){ curve.requestPaint() } function onTotalFramesChanged(){ curve.requestPaint() } }
                                    onWidthChanged:requestPaint()
                                    onPaint: { const c=getContext("2d"); c.clearRect(0,0,width,height); c.strokeStyle=LV.Theme.accent; c.lineWidth=2; c.beginPath(); const ordered=root.stateModel.keys.filter(key=>key.trackId===trackRow.modelData.id).sort((a,b)=>a.frame-b.frame); ordered.forEach((key,index)=>{ const x=key.frame/root.stateModel.totalFrames*width,y=(1-key.value)*height; if(index===0)c.moveTo(x,y); else c.lineTo(x,y) }); c.stroke() }
                                }
                                LV.Label { x:8; y:4; width:parent.width-16; text:qsTr("Image condition weight"); style:caption; elide:Text.ElideRight }
                            }
                        }
                    }
                    Rectangle { x:root.stateModel.playhead/root.stateModel.totalFrames*laneContent.width; width:1; height:parent.height; color:LV.Theme.accent }
                    Rectangle { x:Math.max(0,Math.min(laneContent.width-36,root.stateModel.playhead/root.stateModel.totalFrames*laneContent.width-18)); y:3; width:36; height:18; radius:6; color:LV.Theme.accent; LV.Label { anchors.centerIn:parent; text:String(root.stateModel.playhead).padStart(3,"0"); style:body } }
                }
            }
            LV.Label { x:4; y:selection.y+6; width:root.gutter-8; visible:root.wideControls; text:root.stateModel.mode==="Shots" ? qsTr("Shot selection") : qsTr("Key selection"); style:body }
            Item {
                id: selection
                y:root.rulerY+root.lanesHeight; width:parent.width
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
                x:4; y:selection.y+selection.height+3; width:parent.width-12; spacing:12
                LV.Label { width:Math.min(432,footer.width); text:qsTr("%1 tracks · %2 shots · %3 kept · %4 keys · %5 output frames").arg(root.stateModel.tracks.length).arg(root.stateModel.shots.length).arg(root.stateModel.shots.filter(shot=>!shot.excluded).length).arg(root.stateModel.keys.length).arg(root.stateModel.outputFrames); style:body; wrapMode:Text.Wrap }
                Item { width:Math.min(480,footer.width); height:17; visible:root.stateModel.shots.length>0; Row { width:parent.width; y:5; spacing:3; Repeater { model:root.stateModel.shots; delegate:Rectangle { required property var modelData; width:Math.max(0,modelData.frames/root.stateModel.totalFrames*parent.width-3); height:8; radius:2; color:root.stateModel.selectedIds.indexOf(modelData.id)>=0 ? LV.Theme.accent : LV.Theme.accentGray; opacity:modelData.excluded ? .3 : 1 } } } }
                LV.Label { width:Math.min(300,footer.width); text:qsTr("View F000–%1 · In %2 · Out %3").arg(root.stateModel.totalFrames-1).arg(root.stateModel.markIn).arg(root.stateModel.markOut); style:body; wrapMode:Text.Wrap }
                LV.Label { visible:root.width>=600; text:"V select · B blade · I/O · Space"; style:caption }
            }
        }
    }
}
