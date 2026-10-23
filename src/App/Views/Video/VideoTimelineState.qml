pragma ComponentBehavior: Bound
import QtQuick

QtObject {
    id: root
    objectName: "videoTimelineState"
    property real fps: 24
    property int minimumFrames: 2
    property int playhead: 0
    property string mode: "Shots"
    property string tool: "Select"
    property string filter: "All"
    property bool snap: true
    property bool linked: true
    property bool multiSelect: false
    property int markIn: 0
    property int markOut: 119
    property int compositionFrames: 120
    readonly property var trackTypes: ["Shots", "Contents", "Audio", "Caption"]
    property var tracks: []
    property string selectedTrackId: ""
    property int nextTrackId: 1
    property string error: ""
    readonly property var selectedTrack: tracks.find(track => track.id === selectedTrackId) || ({})
    property var shots: []
    property var selectedIds: []
    property var keys: []
    property string selectedKeyId: ""
    property var undoStack: []
    property var redoStack: []
    property int nextId: 1
    readonly property int totalFrames: shots.length ? shots.reduce((sum, shot) => sum + shot.frames, 0) : compositionFrames
    readonly property int keptFrames: shots.filter(shot => !shot.excluded).reduce((sum, shot) => sum + shot.frames, 0)
    readonly property int outputFrames: shots.length ? keptFrames : compositionFrames
    readonly property var selectedShot: shots.find(shot => selectedIds.indexOf(shot.id) >= 0) || ({})
    readonly property var selectedKey: keys.find(key => key.id === selectedKeyId) || ({})
    readonly property int selectedStart: startOf(selectedShot.id)
    readonly property int selectedEnd: selectedStart + (selectedShot.frames || 0) - 1
    readonly property bool canUndo: undoStack.length > 0
    readonly property bool canRedo: redoStack.length > 0
    property string clipboardKey: ""
    signal resetCompleted()
    signal compositionRestored()

    function copy(value) { return JSON.parse(JSON.stringify(value)) }
    function snapshot() { return {tracks:copy(tracks),selectedTrackId:selectedTrackId,nextTrackId:nextTrackId,shots:copy(shots),keys:copy(keys),selectedIds:selectedIds.slice(),selectedKeyId:selectedKeyId,playhead:playhead,nextId:nextId,compositionFrames:compositionFrames,fps:fps,markIn:markIn,markOut:markOut} }
    function restore(value) {
        const restoresComposition=value.compositionFrames!==undefined && value.fps!==undefined
        if(restoresComposition) { compositionFrames=value.compositionFrames; fps=value.fps }
        tracks=copy(value.tracks); selectedTrackId=value.selectedTrackId; nextTrackId=value.nextTrackId; error=""
        shots=copy(value.shots); keys=copy(value.keys); selectedIds=value.selectedIds.slice()
        selectedKeyId=value.selectedKeyId; seek(value.playhead); nextId=value.nextId
        markIn=value.markIn ?? 0; markOut=value.markOut ?? totalFrames-1
        if(restoresComposition) compositionRestored()
    }
    function reset(frames, frameRate) {
        fps=frameRate; compositionFrames=Math.max(minimumFrames,Math.round(frames))
        tracks=[]; selectedTrackId=""; nextTrackId=1; error=""
        shots=[]; keys=[]; selectedIds=[]; selectedKeyId=""; playhead=0; nextId=1
        markIn=0; markOut=compositionFrames-1; undoStack=[]; redoStack=[]; clipboardKey=""
        mode="Shots"; tool="Select"; filter="All"; snap=true; linked=true; multiSelect=false
        resetCompleted()
    }
    function reject(message) { error=message; return false }
    function track(id) { return tracks.find(entry => entry.id === id) || ({}) }
    function tracksOfType(type) { return tracks.filter(entry => entry.type === type) }
    function trackPosition(id) { const entry=track(id); return tracksOfType(entry.type).findIndex(value => value.id === id) }
    function selectTrack(id) {
        const entry=track(id); if(!entry.id) return false
        selectedTrackId=id
        selectedIds=selectedIds.filter(shotId => shots.some(shot => shot.id===shotId && shot.trackId===id))
        if(selectedKey.trackId!==id) selectedKeyId=""
        return true
    }
    function insertTrack(type, name, position) {
        const group=tracksOfType(type)
        const before=group[position]?.id
        let index=before ? tracks.findIndex(entry=>entry.id===before) : tracks.findIndex(entry=>trackTypes.indexOf(entry.type)>trackTypes.indexOf(type))
        if(index<0) index=tracks.length
        const id="track-"+nextTrackId++
        const next=tracks.slice(); next.splice(index,0,{id:id,type:type,name:name}); tracks=next
        selectTrack(id); return id
    }
    function addTrack(type, name, position) {
        error=""
        if(trackTypes.indexOf(type)<0) { reject(qsTr("Choose a track type.")); return "" }
        const count=tracksOfType(type).length
        position=position===undefined || position===-1 ? count : Number(position)
        if(!Number.isInteger(position) || position<0 || position>count) { reject(qsTr("Choose a valid track position.")); return "" }
        name=String(name || type).trim()
        if(!name.length) { reject(qsTr("Enter a track name.")); return "" }
        checkpoint(); return insertTrack(type,name,position)
    }
    function renameTrack(id, name) {
        error=""; name=String(name).trim()
        if(!track(id).id || !name.length) return reject(qsTr("Enter a track name."))
        if(track(id).name===name) return true
        checkpoint(); tracks=tracks.map(entry=>entry.id===id ? Object.assign({},entry,{name:name}) : entry); return true
    }
    function moveTrack(id, position) {
        error=""; const entry=track(id), group=tracksOfType(entry.type)
        position=Number(position)
        if(!entry.id || !Number.isInteger(position) || position<0 || position>=group.length) return reject(qsTr("Choose a position within this track type."))
        if(trackPosition(id)===position) return true
        checkpoint(); const reordered=group.filter(value=>value.id!==id); reordered.splice(position,0,entry)
        let index=0; tracks=tracks.map(value=>value.type===entry.type ? reordered[index++] : value); selectTrack(id); return true
    }
    function removeTrack(id) {
        error=""; const index=tracks.findIndex(entry=>entry.id===id); if(index<0) return false
        const remainingShots=shots.filter(shot=>shot.trackId!==id), remainingKeys=keys.filter(key=>key.trackId!==id)
        const frames=remainingShots.length ? remainingShots.reduce((sum,shot)=>sum+shot.frames,0) : compositionFrames
        if(remainingKeys.some(key=>key.frame>=frames)) return reject(qsTr("Move image keyframes inside the remaining sequence before removing this track."))
        checkpoint(); shots=remainingShots; keys=remainingKeys; tracks=tracks.filter(entry=>entry.id!==id)
        selectedIds=selectedIds.filter(shotId=>shots.some(shot=>shot.id===shotId))
        if(!keys.some(key=>key.id===selectedKeyId)) selectedKeyId=""
        selectedTrackId=(tracks[Math.min(index,tracks.length-1)] || {}).id || ""
        seek(playhead); markIn=Math.min(markIn,totalFrames-1); markOut=Math.min(markOut,totalFrames-1); return true
    }
    function destinationTrack(type, id) {
        if(id) return track(id).type===type ? id : ""
        if(selectedTrack.type===type) return selectedTrackId
        return tracksOfType(type)[0]?.id || ""
    }
    function assignShotTrack(id, trackId) {
        error=""; if(track(trackId).type!=="Shots" || !shots.some(shot=>shot.id===id)) return false
        checkpoint(); shots=shots.map(shot=>shot.id===id ? Object.assign({},shot,{trackId:trackId}) : shot); selectedTrackId=trackId; return true
    }
    function checkpoint() { undoStack=undoStack.concat([snapshot()]).slice(-100); redoStack=[] }
    function undo() { if (!canUndo) return; redoStack=redoStack.concat([snapshot()]); const last=undoStack[undoStack.length-1]; undoStack=undoStack.slice(0,-1); restore(last) }
    function redo() { if (!canRedo) return; undoStack=undoStack.concat([snapshot()]); const last=redoStack[redoStack.length-1]; redoStack=redoStack.slice(0,-1); restore(last) }
    function startOf(id) { let frame=0; for (const shot of shots) { if (shot.id===id) return frame; frame+=shot.frames } return 0 }
    function seek(frame) { playhead=Math.max(0,Math.min(totalFrames-1,Math.round(frame))) }
    function selectShot(id, extend) {
        const shot=shots.find(entry=>entry.id===id); if(!shot) return
        selectedTrackId=shot.trackId; selectedKeyId=""
        if (extend || multiSelect) selectedIds=selectedIds.indexOf(id)>=0 ? selectedIds.filter(value=>value!==id) : selectedIds.concat([id])
        else selectedIds=[id]
    }
    function jumpCut(direction) {
        const boundaries=shots.map(shot=>startOf(shot.id)).concat([totalFrames-1])
        seek(direction>0 ? boundaries.find(frame=>frame>playhead) ?? totalFrames-1 : boundaries.slice().reverse().find(frame=>frame<playhead) ?? 0)
    }
    function changeShot(id, field, value) {
        const shot=shots.find(entry=>entry.id===id)
        if (!shot || (shot.locked && field!=="locked") || shot[field]===value) return false
        checkpoint(); shots=shots.map(entry=>entry.id===id ? Object.assign({},entry,{[field]:value}) : entry); return true
    }
    function toggleExclude() {
        if (!selectedIds.length) return
        checkpoint(); shots=shots.map(shot=>selectedIds.indexOf(shot.id)>=0 && !shot.locked ? Object.assign({},shot,{excluded:!shot.excluded}) : shot)
    }
    function split() {
        const index=shots.findIndex(shot=>playhead>startOf(shot.id) && playhead<startOf(shot.id)+shot.frames-1)
        if(index<0 || shots[index].locked) return false
        const source=shots[index], left=playhead-startOf(source.id)
        if(left<2 || source.frames-left<2) return false
        checkpoint(); const next=copy(shots), right=Object.assign({},source,{id:"shot-"+nextId++,name:source.name+" / B",frames:source.frames-left})
        next.splice(index,1,Object.assign({},source,{frames:left}),right); shots=next; selectedIds=[right.id]; return true
    }
    function addShot() { return addShotToTrack("") }
    function addShotToTrack(trackId) {
        error=""; let destination=destinationTrack("Shots",trackId)
        if(trackId && !destination) return reject(qsTr("Choose a Shots track."))
        checkpoint(); if(!destination) destination=insertTrack("Shots","Shots",tracksOfType("Shots").length)
        const shot={id:"shot-"+nextId++,trackId:destination,name:"New shot",frames:shots.length ? Math.max(minimumFrames,Math.round(fps)) : compositionFrames,excluded:false,locked:false,prompt:""}
        shots=shots.concat([shot]); selectedIds=[shot.id]; selectedTrackId=destination; selectedKeyId=""; seek(totalFrames-shot.frames); return true
    }
    function duplicateShot() {
        const source=selectedShot; if(!source.id) return
        checkpoint(); const next=copy(shots), index=shots.findIndex(shot=>shot.id===source.id)
        const duplicated=Object.assign({},source,{id:"shot-"+nextId++,name:source.name+" / copy",locked:false})
        next.splice(index+1,0,duplicated); shots=next; selectedIds=[duplicated.id]
    }
    function rippleDelete() {
        const removed=shots.filter(shot=>selectedIds.indexOf(shot.id)>=0 && !shot.locked)
        if(!removed.length) return false
        checkpoint(); const old=copy(shots)
        keys=keys.filter(key=>!removed.some(shot=>key.frame>=startOf(shot.id) && key.frame<startOf(shot.id)+shot.frames)).map(key=>{
            let shift=0; for(const shot of removed) if(startOf(shot.id)+shot.frames<=key.frame) shift+=shot.frames
            return Object.assign({},key,{frame:key.frame-shift})
        })
        shots=old.filter(shot=>removed.every(entry=>entry.id!==shot.id)); selectedIds=shots.length ? [shots[0].id] : []; selectedKeyId=keys.some(key=>key.id===selectedKeyId) ? selectedKeyId : ""; seek(shots.length ? playhead : 0); markIn=shots.length ? Math.min(markIn,totalFrames-1) : 0; markOut=totalFrames-1; return true
    }
    function resizeComposition(frames, frameRate) {
        error=""; frames=Number(frames); frameRate=Number(frameRate ?? fps)
        if(!Number.isInteger(frames) || frames<minimumFrames || frames>2147483647 || !Number.isFinite(frameRate) || frameRate<=0)
            return reject(qsTr("Choose a valid frame count and frame rate."))
        if(!Number.isInteger(totalFrames) || totalFrames<minimumFrames)
            return reject(qsTr("Reset the composition before changing its duration."))
        if(frames<shots.length*minimumFrames)
            return reject(qsTr("The duration must leave at least %1 frames for every shot.").arg(minimumFrames))
        const scale=frames/totalFrames, next=shots.map(shot=>Object.assign({},shot))
        let remaining=frames
        for(let i=0;i<next.length;i++) {
            const minimumAfter=(next.length-i-1)*minimumFrames
            next[i].frames=i===next.length-1 ? remaining : Math.min(remaining-minimumAfter,Math.max(minimumFrames,Math.round(next[i].frames*scale)))
            remaining-=next[i].frames
        }
        const positions=new Set()
        const nextKeys=keys.map(key=>Object.assign({},key,{frame:Math.min(frames-1,Math.round(key.frame*scale))}))
        for(const key of nextKeys) {
            if(positions.has(key.frame)) return reject(qsTr("Resizing would place image keyframes at the same frame. Move the keyframes or choose a longer duration."))
            positions.add(key.frame)
        }
        if(frames===totalFrames && frames===compositionFrames && frameRate===fps) return true
        // Validate the complete plan before publishing draft values or history.
        checkpoint(); compositionFrames=frames; fps=frameRate; shots=next; keys=nextKeys
        seek(playhead); markIn=Math.min(markIn,frames-1); markOut=frames-1; return true
    }
    function snappedFrame(frame) {
        if(!snap) return Math.round(frame)
        const guides=shots.map(shot=>startOf(shot.id)).concat(keys.map(key=>key.frame),[markIn,markOut,playhead])
        const closest=guides.reduce((best,guide)=>Math.abs(guide-frame)<Math.abs(best-frame) ? guide : best,guides.length ? guides[0] : frame)
        return Math.abs(closest-frame)<=2 ? Math.round(closest) : Math.round(frame)
    }
    function slipKeys(id, delta) {
        const shot=shots.find(entry=>entry.id===id); if(!shot || shot.locked) return false
        const start=startOf(id), end=start+shot.frames
        const members=keys.filter(key=>key.frame>=start && key.frame<end)
        if(!members.length || members.some(key=>key.frame+delta<start || key.frame+delta>=end)) return false
        checkpoint(); keys=keys.map(key=>members.some(member=>member.id===key.id) ? Object.assign({},key,{frame:key.frame+delta}) : key); return true
    }
    function trim(id, edge, delta, editTool) {
        const index=shots.findIndex(shot=>shot.id===id); if(index<0 || shots[index].locked) return false
        delta=Math.round(delta); if(!delta) return false
        const next=copy(shots), shot=next[index], neighbor=next[edge==="in" ? index-1 : index+1]
        const change=edge==="in" ? -delta : delta
        if(shot.frames+change<2 || (editTool==="Roll" && (!neighbor || neighbor.locked || neighbor.frames-change<2))) return false
        checkpoint(); const boundary=startOf(id)+(edge==="out" ? shot.frames : 0)
        shot.frames+=change
        if(editTool==="Roll") neighbor.frames-=change
        else if(linked) keys=keys.map(key=>key.frame>=boundary ? Object.assign({},key,{frame:key.frame+change}) : key).filter(key=>key.frame>=0)
        shots=next; keys=keys.filter(key=>key.frame<totalFrames); seek(playhead); return true
    }
    function nudgeShot(delta) {
        const index=shots.findIndex(shot=>shot.id===selectedShot.id)
        if(index<1 || index>=shots.length-1 || shots[index].locked || shots[index-1].locked || shots[index+1].locked) return false
        const next=copy(shots)
        if(next[index-1].frames+delta<2 || next[index+1].frames-delta<2) return false
        checkpoint(); if(linked) keys=keys.map(key=>key.frame>=selectedStart && key.frame<=selectedEnd ? Object.assign({},key,{frame:key.frame+delta}) : key); next[index-1].frames+=delta; next[index+1].frames-=delta; shots=next; return true
    }
    function addKey(source, frame) { return addKeyToTrack(source,frame,"") }
    function addKeyToTrack(source, frame, trackId) {
        error=""; source=String(source); if(!source.trim().length) return false
        frame=Math.max(0,Math.min(totalFrames-1,Math.round(frame)))
        let destination=destinationTrack("Contents",trackId)
        if(trackId && !destination) return reject(qsTr("Choose a Contents track."))
        if(keys.some(key=>key.frame===frame && key.trackId!==destination)) return reject(qsTr("An image keyframe already exists at this frame. Choose a different frame or move the existing keyframe."))
        checkpoint(); if(!destination) destination=insertTrack("Contents","Contents",tracksOfType("Contents").length)
        const id="key-"+nextId++
        keys=keys.filter(key=>key.frame!==frame).concat([{id:id,trackId:destination,frame:frame,value:1,source:source,name:source.split("/").pop(),curve:"Linear"}])
        selectedKeyId=id; selectedIds=[]; selectedTrackId=destination; mode="Keyframes"; return true
    }
    function editKey(field, value) {
        error=""; if(!selectedKey.id) return false
        if(field==="trackId" && track(value).type!=="Contents") return false
        if(field==="frame") { value=Math.round(value); if(value<0 || value>=totalFrames || keys.some(key=>key.id!==selectedKeyId && key.frame===value)) return false }
        if(field==="value" && (!Number.isFinite(value) || value<=0 || value>1)) return false
        checkpoint(); keys=keys.map(key=>key.id===selectedKeyId ? Object.assign({},key,{[field]:value}) : key); if(field==="trackId") selectedTrackId=value; return true
    }
    function deleteKey() { if(!selectedKeyId) return; checkpoint(); keys=keys.filter(key=>key.id!==selectedKeyId); selectedKeyId="" }
    function copyKey() { if(selectedKey.id) clipboardKey=JSON.stringify(selectedKey) }
    function pasteKey() { if(!clipboardKey) return; const key=JSON.parse(clipboardKey); if(addKeyToTrack(key.source,playhead,destinationTrack("Contents","") || key.trackId)) editKey("value",key.value) }
    function recipeShots(prompt) {
        const segments=shots.length ? shots.filter(shot=>!shot.excluded) : [{frames:compositionFrames,prompt:prompt}]
        return segments.map(shot=>{
            const start=shot.id ? startOf(shot.id) : 0
            return {frames:shot.frames,prompt:shot.prompt || prompt,conditions:keys.filter(key=>key.frame>=start && key.frame<start+shot.frames).map(key=>({image:key.source,frame:key.frame-start,strength:key.value}))}
        })
    }
}
