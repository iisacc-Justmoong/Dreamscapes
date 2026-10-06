pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV

FocusScope {
    id: root
    objectName:"videoTimeline"
    required property VideoTimelineState stateModel
    property bool playing:false
    property real timelineScale:1
    readonly property int gutter:192
    signal chooseKeyframe(int frame)
    readonly property int curveHeight:stateModel.mode==="Curves" ? 80 : 0
    implicitHeight:406+curveHeight
    Keys.onPressed:function(event) {
        if(event.key===Qt.Key_Space) { playing=!playing; event.accepted=true }
        else if(event.key===Qt.Key_Left || event.key===Qt.Key_Right) { stateModel.seek(stateModel.playhead+(event.key===Qt.Key_Left ? -1 : 1)); event.accepted=true }
        else if(event.key===Qt.Key_B) { stateModel.tool="Blade"; event.accepted=true }
        else if(event.key===Qt.Key_V) { stateModel.tool="Select"; event.accepted=true }
        else if(event.key===Qt.Key_I) { stateModel.markIn=stateModel.playhead; event.accepted=true }
        else if(event.key===Qt.Key_O) { stateModel.markOut=stateModel.playhead; event.accepted=true }
        else if(event.key===Qt.Key_Delete || event.key===Qt.Key_Backspace) { if(stateModel.mode==="Shots") stateModel.rippleDelete(); else stateModel.deleteKey(); event.accepted=true }
        else if(event.key===Qt.Key_Z && (event.modifiers & Qt.ControlModifier)) { if(event.modifiers & Qt.ShiftModifier) stateModel.redo(); else stateModel.undo(); event.accepted=true }
    }
    Timer { interval:1000/root.stateModel.fps; repeat:true; running:root.playing; onTriggered:{ if(root.stateModel.playhead>=root.stateModel.totalFrames-1) { root.playing=false; root.stateModel.seek(0) } else root.stateModel.seek(root.stateModel.playhead+1) } }
    component Button: LV.LabelButton {
        height:22; verticalPadding:2; horizontalPadding:8
        tone:LV.AbstractButton.Default
    }
    component TrackHeading: Item {
        id: heading
        property string label:""
        property bool locked:false
        property bool muted:false
        property bool solo:false
        width:184
        LV.Label { x:4; anchors.verticalCenter:parent.verticalCenter; width:108; text:heading.label; elide:Text.ElideRight; style:body }
        Row {
            x:116; anchors.verticalCenter:parent.verticalCenter
            Button { width:22; text:"L"; horizontalPadding:0; enabled:false; tone:heading.locked ? LV.AbstractButton.Primary : LV.AbstractButton.Borderless; Accessible.name:qsTr("Track lock") }
            Button { width:22; text:"S"; horizontalPadding:0; enabled:false; tone:LV.AbstractButton.Borderless; Accessible.name:qsTr("Track solo") }
            Button { width:22; text:"M"; horizontalPadding:0; enabled:false; tone:LV.AbstractButton.Borderless; Accessible.name:qsTr("Track mute") }
        }
    }
    Rectangle { anchors.fill:parent; color:LV.Theme.panelBackground08 }
    Flickable {
        id: viewport
        objectName:"videoTimelineViewport"
        anchors.fill:parent; contentWidth:Math.max(width,1568)*root.timelineScale; contentHeight:root.implicitHeight
        flickableDirection:Flickable.HorizontalFlick; boundsBehavior:Flickable.StopAtBounds; clip:true
        Controls.ScrollBar.horizontal:Controls.ScrollBar { policy:Controls.ScrollBar.AsNeeded }
        Item {
            id: panel
            width:viewport.contentWidth; height:root.implicitHeight
            readonly property real laneWidth:width-root.gutter
            Row {
                id: header
                objectName:"videoTimelineHeader"
                height:36; spacing:8
                Button { objectName:"videoTimecode"; width:156; height:28; y:4; text:qsTr("%1 · F%2").arg((root.stateModel.playhead/root.stateModel.fps).toFixed(3)+" s").arg(String(root.stateModel.playhead).padStart(3,"0")) }
                Button { y:7; text:"‹"; tone:LV.AbstractButton.Borderless; onClicked:root.stateModel.jumpCut(-1) }
                Button { objectName:"videoTimelinePlay"; y:7; text:root.playing ? "Ⅱ" : "▶"; onClicked:root.playing=!root.playing }
                Button { y:7; text:"›"; tone:LV.AbstractButton.Borderless; onClicked:root.stateModel.jumpCut(1) }
                Button { y:7; text:"−1f"; tone:LV.AbstractButton.Borderless; onClicked:root.stateModel.seek(root.stateModel.playhead-1) }
                Button { y:7; text:"+1f"; tone:LV.AbstractButton.Borderless; onClicked:root.stateModel.seek(root.stateModel.playhead+1) }
                Repeater { model:["Shots","Keyframes","Curves"]; delegate:Button { required property string modelData; y:7; text:modelData; tone:root.stateModel.mode===modelData ? LV.AbstractButton.Primary : LV.AbstractButton.Default; onClicked:root.stateModel.mode=modelData } }
            }
            Row {
                anchors.right:parent.right; y:7; spacing:8
                Button { objectName:"videoTimelineUndo"; text:qsTr("Undo"); tone:LV.AbstractButton.Borderless; enabled:root.stateModel.canUndo; onClicked:root.stateModel.undo() }
                Button { text:qsTr("Redo"); tone:LV.AbstractButton.Borderless; enabled:root.stateModel.canRedo; onClicked:root.stateModel.redo() }
                Button { objectName:"videoSplitShot"; text:qsTr("Split at head"); onClicked:root.stateModel.split() }
                Button { objectName:"videoExcludeShot"; text:qsTr("Exclude"); onClicked:root.stateModel.toggleExclude() }
                Button { objectName:"videoRippleDelete"; text:qsTr("Ripple delete"); onClicked:root.stateModel.rippleDelete() }
                Button { objectName:"videoAddShot"; text:qsTr("+ Shot"); onClicked:root.stateModel.addShot() }
                Button { text:"···"; tone:LV.AbstractButton.Borderless; onClicked:moreMenu.openFor(this,0,height) }
            }
            LV.ContextMenu { id:moreMenu; items:[qsTr("Duplicate shot"),qsTr("Select all"),qsTr("Deselect all")]; showIconSlot:false; onItemTriggered:function(index) { if(index===0) root.stateModel.duplicateShot(); else root.stateModel.selectedIds=index===1 ? root.stateModel.shots.map(shot=>shot.id) : [] } }
            LV.Label { x:4; y:42; width:184; text:qsTr("Edit tools / selection"); style:body }
            Row {
                x:root.gutter; y:39; spacing:4
                Repeater {
                    model:["Select","Blade","Trim","Ripple","Roll","Slip","Slide","Range"]
                    delegate:Button { required property string modelData; text:(modelData==="Select" ? "V " : modelData==="Blade" ? "B " : modelData==="Trim" ? "T " : "")+modelData; tone:root.stateModel.tool===modelData ? LV.AbstractButton.Primary : LV.AbstractButton.Default; onClicked:root.stateModel.tool=modelData }
                }
                Button { text:root.stateModel.multiSelect ? qsTr("Multi on") : qsTr("Multi"); tone:root.stateModel.multiSelect ? LV.AbstractButton.Primary : LV.AbstractButton.Default; onClicked:root.stateModel.multiSelect=!root.stateModel.multiSelect }
                Button { text:"I In"; tone:LV.AbstractButton.Borderless; onClicked:root.stateModel.markIn=root.stateModel.playhead }
                Button { text:"O Out"; tone:LV.AbstractButton.Borderless; onClicked:root.stateModel.markOut=root.stateModel.playhead }
            }
            Row {
                anchors.right:parent.right; y:39; spacing:8
                Repeater { model:["All","Kept","Out"]; delegate:Button { required property string modelData; text:modelData; tone:root.stateModel.filter===modelData ? LV.AbstractButton.Primary : LV.AbstractButton.Default; onClicked:root.stateModel.filter=modelData } }
                Button { text:root.stateModel.snap ? "Snap on" : "Snap off"; tone:root.stateModel.snap ? LV.AbstractButton.Primary : LV.AbstractButton.Default; onClicked:root.stateModel.snap=!root.stateModel.snap }
                Button { text:root.stateModel.linked ? "Link on" : "Link off"; onClicked:root.stateModel.linked=!root.stateModel.linked }
                Button { text:Math.round(root.timelineScale*100)+"%"; onClicked:root.timelineScale=root.timelineScale>=2 ? 1 : root.timelineScale+.25 }
                Button { text:qsTr("Fit"); tone:LV.AbstractButton.Borderless; onClicked:{ root.timelineScale=1; viewport.contentX=0 } }
            }
            LV.Label { x:4; y:68; width:184; text:qsTr("%1 fps · frame number").arg(root.stateModel.fps); style:body }
            Item {
                objectName:"videoFrameRuler"
                x:root.gutter; y:64; width:panel.laneWidth; height:24
                Repeater { model:12; delegate:Item { required property int index; x:index*panel.laneWidth/12; width:panel.laneWidth/12; height:24; LV.Label { text:"F"+String(Math.round(index*root.stateModel.totalFrames/12)).padStart(3,"0"); style:caption; color:LV.Theme.textSecondary } Rectangle { y:16; height:6; width:1; color:LV.Theme.accentGray } } }
                MouseArea { anchors.fill:parent; onPressed:function(mouse){ root.forceActiveFocus(); root.stateModel.seek(mouse.x/width*root.stateModel.totalFrames) }; onPositionChanged:function(mouse){ if(pressed) root.stateModel.seek(mouse.x/width*root.stateModel.totalFrames) } }
            }
            TrackHeading { y:88; height:56; label:qsTr("Shots") }
            Item {
                id: shotLane
                objectName:"videoShotLane"
                x:root.gutter; y:88; width:panel.laneWidth; height:56
                Repeater {
                    model:root.stateModel.shots
                    delegate:Rectangle {
                        id: shot
                        required property var modelData
                        required property int index
                        readonly property bool selected:root.stateModel.selectedIds.indexOf(modelData.id)>=0
                        readonly property int start:root.stateModel.startOf(modelData.id)
                        x:start/root.stateModel.totalFrames*shotLane.width+2
                        width:modelData.frames/root.stateModel.totalFrames*shotLane.width-4; height:50; y:3
                        radius:12; clip:true
                        visible:root.stateModel.filter==="All" || (root.stateModel.filter==="Out" ? modelData.excluded : !modelData.excluded)
                        color:LV.Theme.panelBackground01; border.width:selected ? 2 : 1; border.color:selected ? LV.Theme.accent : LV.Theme.panelBackground12
                        opacity:modelData.excluded ? .35 : 1
                        Image { anchors.fill:parent; source:Qt.resolvedUrl("Assets/coastal-reference.png"); fillMode:Image.PreserveAspectCrop; opacity:.18 }
                        LV.Label { x:12; y:4; width:parent.width-24; text:String(shot.index+1).padStart(2,"0")+" / "+shot.modelData.name; style:body; elide:Text.ElideRight }
                        LV.Label { x:12; y:27; width:parent.width-24; text:"F"+String(shot.start).padStart(3,"0")+"–"+String(shot.start+shot.modelData.frames-1).padStart(3,"0")+" · "+shot.modelData.frames+"f"+(shot.modelData.excluded ? " · Excluded" : ""); style:caption; elide:Text.ElideRight }
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
            Repeater {
                model:[{label:qsTr("Contents"),y:144,height:40,lane:0},{label:qsTr("Contents"),y:184,height:40,lane:1},{label:qsTr("Audio"),y:224,height:48,lane:2},{label:qsTr("Audio"),y:272,height:48,lane:3},{label:qsTr("Caption"),y:320,height:26,lane:4}]
                delegate:Item {
                    id: track
                    required property var modelData
                    y:modelData.y+(modelData.lane>=2 ? root.curveHeight : 0); width:panel.width; height:modelData.height
                    TrackHeading { height:parent.height; label:track.modelData.label }
                    Rectangle {
                        x:root.gutter; width:panel.laneWidth; height:parent.height; color:LV.Theme.panelBackground02
                        Repeater { model:12; delegate:Rectangle { required property int index; x:index*panel.laneWidth/12; width:1; height:parent.height; color:LV.Theme.panelBackground08 } }
                        Button { x:8; anchors.verticalCenter:parent.verticalCenter; visible:track.modelData.lane<2 && !root.stateModel.keys.some(key=>key.lane===track.modelData.lane); text:qsTr("+ Image keyframe"); tone:LV.AbstractButton.Borderless; onClicked:root.chooseKeyframe(root.stateModel.playhead) }
                        LV.Label { x:12; anchors.verticalCenter:parent.verticalCenter; text:track.modelData.lane===4 ? qsTr("No captions") : qsTr("No audio"); style:caption; color:LV.Theme.textSecondary; visible:track.modelData.lane>=2 }
                        Repeater {
                            model:root.stateModel.keys.filter(key=>key.lane===track.modelData.lane)
                            delegate:Rectangle {
                                id: key
                                required property var modelData
                                x:Math.min(parent.width-width,key.modelData.frame/root.stateModel.totalFrames*panel.laneWidth); y:4; height:32; width:184; radius:6
                                color:LV.Theme.panelBackground08; border.width:root.stateModel.selectedKeyId===modelData.id ? 1 : 0; border.color:LV.Theme.accent
                                Image { x:4; y:2; width:32; height:28; source:key.modelData.source; fillMode:Image.PreserveAspectCrop }
                                LV.Label { x:44; y:2; width:136; text:key.modelData.name; style:body; elide:Text.ElideRight }
                                LV.Label { x:44; y:17; text:"F"+String(key.modelData.frame).padStart(3,"0")+" · weight "+key.modelData.value.toFixed(2); style:caption }
                                MouseArea { anchors.fill:parent; property real pressX:0; onPressed:function(mouse){ root.stateModel.selectedKeyId=key.modelData.id; root.stateModel.mode="Keyframes"; pressX=mapToItem(shotLane,mouse.x,0).x }; onReleased:function(mouse){ const delta=Math.round((mapToItem(shotLane,mouse.x,0).x-pressX)/panel.laneWidth*root.stateModel.totalFrames); if(delta) root.stateModel.editKey("frame",key.modelData.frame+delta) } }
                            }
                        }
                    }
                }
            }
            Item {
                x:root.gutter; y:224; width:panel.laneWidth; height:80; visible:root.stateModel.mode==="Curves"
                Rectangle { anchors.fill:parent; color:LV.Theme.panelBackground02 }
                Canvas {
                    id: curve
                    anchors.fill:parent
                    Connections { target:root.stateModel; function onKeysChanged(){ curve.requestPaint() } }
                    onWidthChanged:requestPaint()
                    onPaint:{ const c=getContext("2d"); c.clearRect(0,0,width,height); c.strokeStyle=LV.Theme.accent; c.lineWidth=2; c.beginPath(); const ordered=root.stateModel.keys.slice().sort((a,b)=>a.frame-b.frame); ordered.forEach((key,index)=>{ const x=key.frame/root.stateModel.totalFrames*width,y=(1-key.value)*height; if(index===0)c.moveTo(x,y); else c.lineTo(x,y) }); c.stroke() }
                }
                LV.Label { x:8; y:4; text:qsTr("Image condition weight"); style:caption }
            }
            LV.Label { x:4; y:352+root.curveHeight; text:root.stateModel.mode==="Shots" ? qsTr("Shot selection") : qsTr("Key selection"); style:body }
            Row {
                x:root.gutter; y:349+root.curveHeight; spacing:4; visible:root.stateModel.mode==="Shots"
                Button { height:24; text:"In F"+String(root.stateModel.selectedStart).padStart(3,"0") }
                Button { height:24; text:"Out F"+String(root.stateModel.selectedEnd).padStart(3,"0") }
                Button { height:24; text:(root.stateModel.selectedShot.frames || 0)+"f" }
                Repeater { model:[-12,-1,1,12]; delegate:Button { required property int modelData; height:24; text:(modelData>0 ? "+" : "")+modelData+"f In"; enabled:!!root.stateModel.selectedShot.id; onClicked:root.stateModel.trim(root.stateModel.selectedShot.id,"in",modelData,root.stateModel.tool) } }
                Button { height:24; text:qsTr("Keep"); onClicked:root.stateModel.changeShot(root.stateModel.selectedShot.id,"excluded",false) }
                Button { height:24; text:qsTr("Exclude"); onClicked:root.stateModel.changeShot(root.stateModel.selectedShot.id,"excluded",true) }
                Button { height:24; text:root.stateModel.selectedShot.locked ? qsTr("Unlock") : qsTr("Lock"); onClicked:root.stateModel.changeShot(root.stateModel.selectedShot.id,"locked",!root.stateModel.selectedShot.locked) }
                Button { height:24; text:qsTr("Duplicate"); onClicked:root.stateModel.duplicateShot() }
            }
            Row {
                x:root.gutter; y:349+root.curveHeight; spacing:4; visible:root.stateModel.mode!=="Shots"
                Button { height:24; text:root.stateModel.selectedKey.id ? "Frame F"+root.stateModel.selectedKey.frame : qsTr("Choose a keyframe") }
                Button { height:24; text:"Value "+(root.stateModel.selectedKey.value || 0).toFixed(2); enabled:!!root.stateModel.selectedKey.id; onClicked:root.stateModel.editKey("value",Math.min(1,root.stateModel.selectedKey.value+.05)) }
                Repeater { model:[-12,-1,1,12]; delegate:Button { required property int modelData; height:24; text:(modelData>0 ? "+" : "")+modelData+"f"; enabled:!!root.stateModel.selectedKey.id; onClicked:root.stateModel.editKey("frame",root.stateModel.selectedKey.frame+modelData) } }
                Button { height:24; text:qsTr("− Weight"); enabled:!!root.stateModel.selectedKey.id; onClicked:root.stateModel.editKey("value",Math.max(.001,root.stateModel.selectedKey.value-.05)) }
                Button { height:24; text:qsTr("Copy"); onClicked:root.stateModel.copyKey() }
                Button { height:24; text:qsTr("Paste"); enabled:root.stateModel.clipboardKey.length>0; onClicked:root.stateModel.pasteKey() }
                Button { objectName:"videoDeleteKey"; height:24; text:qsTr("Delete"); enabled:!!root.stateModel.selectedKey.id; onClicked:root.stateModel.deleteKey() }
            }
            LV.Label { x:4; y:383+root.curveHeight; width:432; text:qsTr("%1 shots · %2 kept · %3 keys · %4 output frames").arg(root.stateModel.shots.length).arg(root.stateModel.shots.filter(shot=>!shot.excluded).length).arg(root.stateModel.keys.length).arg(root.stateModel.keptFrames); style:body; elide:Text.ElideRight }
            Row { x:440; y:388+root.curveHeight; spacing:3; Repeater { model:root.stateModel.shots; delegate:Rectangle { required property var modelData; width:modelData.frames/root.stateModel.totalFrames*500-3; height:8; radius:2; color:root.stateModel.selectedIds.indexOf(modelData.id)>=0 ? LV.Theme.accent : LV.Theme.accentGray; opacity:modelData.excluded ? .3 : 1 } } }
            LV.Label { x:948; y:383+root.curveHeight; text:qsTr("View F000–%1 · In %2 · Out %3").arg(root.stateModel.totalFrames-1).arg(root.stateModel.markIn).arg(root.stateModel.markOut); style:body }
            LV.Label { anchors.right:parent.right; y:383+root.curveHeight; text:"V select · B blade · I/O · Space"; style:caption }
            Rectangle { x:root.gutter+root.stateModel.playhead/root.stateModel.totalFrames*panel.laneWidth; y:64; width:1; height:282+root.curveHeight; color:LV.Theme.accent }
            Rectangle { x:root.gutter+root.stateModel.playhead/root.stateModel.totalFrames*panel.laneWidth-18; y:67; width:36; height:18; radius:6; color:LV.Theme.accent; LV.Label { anchors.centerIn:parent; text:String(root.stateModel.playhead).padStart(3,"0"); style:body } }
        }
    }
}
