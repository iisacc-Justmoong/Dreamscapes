pragma ComponentBehavior: Bound
import QtQuick

QtObject {
    id: root
    objectName: "videoTimelineState"
    property int fps: 24
    property int playhead: 60
    property string mode: "Shots"
    property string tool: "Select"
    property string filter: "All"
    property bool snap: true
    property bool linked: true
    property bool multiSelect: false
    property int markIn: 0
    property int markOut: 119
    property var shots: [
        {id:"shot-1",name:"Arrival",frames:24,excluded:false,locked:false,prompt:""},
        {id:"shot-2",name:"Facade detail",frames:24,excluded:false,locked:false,prompt:""},
        {id:"shot-3",name:"Coastal house",frames:36,excluded:false,locked:false,prompt:""},
        {id:"shot-4",name:"Ocean / take B",frames:36,excluded:true,locked:false,prompt:""}
    ]
    property var selectedIds: ["shot-3"]
    property var keys: []
    property string selectedKeyId: ""
    property var undoStack: []
    property var redoStack: []
    property int nextId: 5
    readonly property int totalFrames: shots.reduce((sum, shot) => sum + shot.frames, 0)
    readonly property int keptFrames: shots.filter(shot => !shot.excluded).reduce((sum, shot) => sum + shot.frames, 0)
    readonly property var selectedShot: shots.find(shot => selectedIds.indexOf(shot.id) >= 0) || ({})
    readonly property var selectedKey: keys.find(key => key.id === selectedKeyId) || ({})
    readonly property int selectedStart: startOf(selectedShot.id)
    readonly property int selectedEnd: selectedStart + (selectedShot.frames || 0) - 1
    readonly property bool canUndo: undoStack.length > 0
    readonly property bool canRedo: redoStack.length > 0
    property string clipboardKey: ""

    function copy(value) { return JSON.parse(JSON.stringify(value)) }
    function snapshot() { return {shots:copy(shots),keys:copy(keys),selectedIds:selectedIds.slice(),selectedKeyId:selectedKeyId,playhead:playhead,nextId:nextId} }
    function restore(value) {
        shots=copy(value.shots); keys=copy(value.keys); selectedIds=value.selectedIds.slice()
        selectedKeyId=value.selectedKeyId; playhead=value.playhead; nextId=value.nextId
    }
    function checkpoint() { undoStack=undoStack.concat([snapshot()]).slice(-100); redoStack=[] }
    function undo() { if (!canUndo) return; redoStack=redoStack.concat([snapshot()]); const last=undoStack[undoStack.length-1]; undoStack=undoStack.slice(0,-1); restore(last) }
    function redo() { if (!canRedo) return; undoStack=undoStack.concat([snapshot()]); const last=redoStack[redoStack.length-1]; redoStack=redoStack.slice(0,-1); restore(last) }
    function startOf(id) { let frame=0; for (const shot of shots) { if (shot.id===id) return frame; frame+=shot.frames } return 0 }
    function seek(frame) { playhead=Math.max(0,Math.min(totalFrames-1,Math.round(frame))) }
    function selectShot(id, extend) {
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
    function addShot() { checkpoint(); const shot={id:"shot-"+nextId++,name:"New shot",frames:fps,excluded:false,locked:false,prompt:""}; shots=shots.concat([shot]); selectedIds=[shot.id]; seek(totalFrames-shot.frames) }
    function duplicateShot() {
        const source=selectedShot; if(!source.id) return
        checkpoint(); const next=copy(shots), index=shots.findIndex(shot=>shot.id===source.id)
        const duplicated=Object.assign({},source,{id:"shot-"+nextId++,name:source.name+" / copy",locked:false})
        next.splice(index+1,0,duplicated); shots=next; selectedIds=[duplicated.id]
    }
    function rippleDelete() {
        const removed=shots.filter(shot=>selectedIds.indexOf(shot.id)>=0 && !shot.locked)
        if(!removed.length || removed.length===shots.length) return false
        checkpoint(); const old=copy(shots)
        keys=keys.filter(key=>!removed.some(shot=>key.frame>=startOf(shot.id) && key.frame<startOf(shot.id)+shot.frames)).map(key=>{
            let shift=0; for(const shot of removed) if(startOf(shot.id)+shot.frames<=key.frame) shift+=shot.frames
            return Object.assign({},key,{frame:key.frame-shift})
        })
        shots=old.filter(shot=>removed.every(entry=>entry.id!==shot.id)); selectedIds=[shots[0].id]; seek(playhead); return true
    }
    function resizeComposition(frames) {
        frames=Math.max(2,Math.round(frames)); if(frames===totalFrames) return
        checkpoint(); const scale=frames/totalFrames, next=copy(shots); let used=0
        for(let i=0;i<next.length;i++) { next[i].frames=i===next.length-1 ? frames-used : Math.max(2,Math.round(next[i].frames*scale)); used+=next[i].frames }
        if(next[next.length-1].frames<2) { undo(); return }
        shots=next; keys=keys.map(key=>Object.assign({},key,{frame:Math.min(frames-1,Math.round(key.frame*scale))})); seek(playhead); markOut=frames-1
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
    function addKey(source, frame) {
        frame=Math.max(0,Math.min(totalFrames-1,Math.round(frame)))
        checkpoint(); const id="key-"+nextId++
        keys=keys.filter(key=>key.frame!==frame).concat([{id:id,frame:frame,value:0.8,source:String(source),name:String(source).split("/").pop(),curve:"Linear",lane:keys.length%2}]); selectedKeyId=id; mode="Keyframes"
    }
    function editKey(field, value) {
        if(!selectedKey.id) return false
        if(field==="frame") { value=Math.round(value); if(value<0 || value>=totalFrames || keys.some(key=>key.id!==selectedKeyId && key.frame===value)) return false }
        if(field==="value" && (!Number.isFinite(value) || value<=0 || value>1)) return false
        checkpoint(); keys=keys.map(key=>key.id===selectedKeyId ? Object.assign({},key,{[field]:value}) : key); return true
    }
    function deleteKey() { if(!selectedKeyId) return; checkpoint(); keys=keys.filter(key=>key.id!==selectedKeyId); selectedKeyId="" }
    function copyKey() { if(selectedKey.id) clipboardKey=JSON.stringify(selectedKey) }
    function pasteKey() { if(!clipboardKey) return; const key=JSON.parse(clipboardKey); addKey(key.source,playhead); editKey("value",key.value) }
    function recipeShots(prompt) {
        return shots.filter(shot=>!shot.excluded).map(shot=>{
            const start=startOf(shot.id)
            return {frames:shot.frames,prompt:shot.prompt || prompt,conditions:keys.filter(key=>key.frame>=start && key.frame<start+shot.frames).map(key=>({image:key.source,frame:key.frame-start,strength:key.value}))}
        })
    }
}
