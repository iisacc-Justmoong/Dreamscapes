pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LVRS 1.0 as LV
import ".." as Views
import "../Home"

Item {
    id: root
    objectName: "videoParameterPanel"
    required property var generation
    required property VideoTimelineState timeline
    implicitWidth: LV.Theme.scaleMetric(402)
    clip: true
    property string tab: "Generation"
    property bool expanded: true
    property bool canGenerate: false
    property bool pending: false
    property string selectedPreset: qsTr("Default")
    property string presetFeedback: ""
    readonly property var presetNames: [qsTr("Default")].concat(generation.videoPresetNames || [])
    readonly property var cameraNames: [qsTr("None"),qsTr("Static"),qsTr("Dolly in"),qsTr("Dolly out"),qsTr("Pan left"),qsTr("Pan right"),qsTr("Tilt up"),qsTr("Tilt down"),qsTr("Orbit left"),qsTr("Orbit right"),qsTr("Crane up"),qsTr("Crane down"),qsTr("Zoom in"),qsTr("Zoom out"),qsTr("Handheld"),qsTr("Tracking"),qsTr("Dolly zoom")]
    readonly property var cameraValues: ["none","static","dolly-in","dolly-out","pan-left","pan-right","tilt-up","tilt-down","orbit-left","orbit-right","crane-up","crane-down","zoom-in","zoom-out","handheld","tracking","dolly-zoom"]
    readonly property string aspectRatio: { const ratios=["1:1","4:3","3:4","16:9","9:16"]; return ratios.find(r=>{const n=r.split(":"); return values.width*Number(n[1])===values.height*Number(n[0])}) || qsTr("Custom") }
    property var invalidInputs: ({})
    readonly property bool hasInvalidInputs: Object.keys(invalidInputs).length>0 || !cameraSelectionValid
    readonly property bool cameraSelectionValid: {
        const motions=[values.camera1,values.camera2,values.camera3].filter(value=>value!=="none")
        if(new Set(motions).size!==motions.length || (motions.length>1 && motions.indexOf("static")>=0)) return false
        return ![["pan-left","pan-right"],["tilt-up","tilt-down"],["dolly-in","dolly-out"],["orbit-left","orbit-right"],["crane-up","crane-down"],["zoom-in","zoom-out"]].some(pair=>motions.indexOf(pair[0])>=0 && motions.indexOf(pair[1])>=0)
    }
    property var values: defaults()
    readonly property var modelNames: generation.videoModels.map(model=>model.name || model.id)
    signal generateRequested()
    function defaults() {
        const prompts = generation.defaultGenerationPrompts ? generation.defaultGenerationPrompts.Video : null
        return {prompt:prompts ? prompts.prompt : "",negativePrompt:prompts ? prompts.negativePrompt : "",width:1024,height:576,duration:5,fps:24,outputCount:1,steps:30,cfgScale:3,seed:-1,randomSeed:true,interpolationFactor:2,decodeTimestep:0.05,decodeNoiseScale:0.025,imageConditionNoise:0,device:"auto",precision:"auto",offload:"auto",cpuTextEncoding:true,vaeTiling:true,crf:18,encodingPreset:"medium",camera1:"none",camera2:"none",camera3:"none"}
    }
    function edit(key, value) {
        if(key==="aspectRatio") {
            const sizes={"1:1":[512,512],"4:3":[640,480],"3:4":[480,640],"16:9":[1024,576],"9:16":[576,1024]}, size=sizes[value]
            if(!size) return false
            const valid=Object.assign({},invalidInputs); delete valid.width; delete valid.height
            invalidInputs=valid; values=Object.assign({},values,{width:size[0],height:size[1]}); return true
        }
        const ranges={width:[32,4096],height:[32,4096],duration:[1,30],fps:[12,120],outputCount:[1,1000],steps:[1,1000],cfgScale:[0,100],seed:[0,4294967295],interpolationFactor:[2,8],decodeTimestep:[0,1],decodeNoiseScale:[0,1],imageConditionNoise:[0,1],crf:[0,51]}
        const valid=Object.assign({},invalidInputs), range=ranges[key]
        if(range && (!Number.isFinite(value) || value<range[0] || value>range[1] || (["width","height","duration","fps","outputCount","steps","seed","interpolationFactor","crf"].indexOf(key)>=0 && Math.round(value)!==value) || (["width","height"].indexOf(key)>=0 && value%32!==0) || (key==="fps" && [12,24,30,60,120].indexOf(value)<0))) { valid[key]=true; invalidInputs=valid; return false }
        delete valid[key]
        if(key==="randomSeed") { if(value || values.seed>=0) delete valid.seed; else valid.seed=true }
        const next=Object.assign({},values,{[key]:value})
        if((key==="duration" || key==="fps") && !timeline.resizeComposition(next.duration*next.fps,next.fps)) {
            valid[key]=true; invalidInputs=valid; return false
        }
        invalidInputs=valid; values=next
        return true
    }
    function parameters() {
        const camera=[values.camera1,values.camera2,values.camera3].filter(motion=>motion!=="none")
        return Object.assign({},values,{model:generation.selectedVideoModel,seed:values.randomSeed ? -1 : values.seed,camera:camera.length ? camera : ["none"],shots:timeline.recipeShots(values.prompt)})
    }
    function savePreset(name) {
        if(hasInvalidInputs || !generation.saveVideoPreset(name,Object.assign({},values,{model:generation.selectedVideoModel}))) { presetFeedback=generation.videoPresetError; return false }
        selectedPreset=name.trim(); presetFeedback=qsTr("Preset saved"); return true
    }
    function loadPreset(name) {
        if(name===qsTr("Default")) { const next=defaults(); if(!timeline.resizeComposition(next.duration*next.fps,next.fps)) return false; values=next }
        else {
            const saved=generation.videoPreset(name)
            if(!saved || !Object.keys(saved).length) { presetFeedback=generation.videoPresetError; return false }
            const next=Object.assign(defaults(),saved)
            if(!timeline.resizeComposition(next.duration*next.fps,next.fps)) { presetFeedback=timeline.error; return false }
            values=next
            if(saved.model && generation.videoModels.some(model=>model.id===saved.model)) generation.selectedVideoModel=saved.model
        }
        invalidInputs=({}); selectedPreset=name; presetFeedback=""; return true
    }
    function focusPrompt() { tab="Generation"; viewport.contentY=0; prompt.forceActiveFocus() }
    function resetDraft() { values=defaults(); invalidInputs=({}); timeline.reset(values.duration*values.fps,values.fps); tab="Generation"; selectedPreset=qsTr("Default"); presetFeedback="" }
    Connections {
        target:root.timeline
        function onSelectedShotChanged() { if(!root.timeline.selectedShot.id) root.tab="Generation" }
        function onCompositionRestored() {
            const valid=Object.assign({},root.invalidInputs); delete valid.duration; delete valid.fps
            root.invalidInputs=valid
            root.values=Object.assign({},root.values,{duration:root.timeline.compositionFrames/root.timeline.fps,fps:root.timeline.fps})
        }
    }
    FileDialog {
        id: imageDialog
        property int targetFrame: 0
        property string targetTrackId: ""
        title: qsTr("Choose an image keyframe")
        nameFilters: [qsTr("Images and canvases (*.iisc *.png *.jpg *.jpeg *.webp *.heic *.avif)")]
        onAccepted: root.addImageKeyToTrack(selectedFile,targetFrame,targetTrackId)
    }
    function addImageKey(source, frame) { return addImageKeyToTrack(source,frame,"") }
    function addImageKeyToTrack(source, frame, trackId) {
        if(trackId && timeline.track(trackId).type!=="Contents") return false
        const document = generation.imageDocument(source)
        if (!String(document).length) return false
        return timeline.addKeyToTrack(document,frame,trackId)
    }
    function chooseImage(frame, trackId) { imageDialog.targetFrame=frame; imageDialog.targetTrackId=trackId || ""; imageDialog.open() }
    component Section: Views.PanelRow {
        type: LV.ListItem.Navigation; compact:true; Layout.fillWidth:true; Layout.minimumWidth:0
        showLeadingIcon:false; showValue:false; showTrailingIcon:false; showDescription:true
        navigationItemWidth:300
    }
    component NumberRow: Views.PanelRow {
        id: row
        property string parameterKey:""
        property bool invalidEdit:false
        readonly property bool editing:inputControl.inputItem.activeFocus
        readonly property string unitSuffix:parameterKey==="width" || parameterKey==="height" ? " px" : parameterKey==="duration" ? " s" : ""
        type:LV.ListItem.InlineEdit; Layout.fillWidth:true; Layout.minimumWidth:0
        compact:true; fillControl:false; showLeadingIcon:false; showDescription:false; showPrimaryAction:false; inputWidth:140
        input1:({clearButtonVisible:false})
        Binding { target:row; property:"inputText1"; value:String(root.values[row.parameterKey])+row.unitSuffix; when:!row.invalidEdit && !row.editing; restoreMode:Binding.RestoreNone }
        Connections {
            target:row.inputControl.inputItem
            function onActiveFocusChanged() { if(row.editing && !row.invalidEdit) row.inputText1=String(root.values[row.parameterKey]) }
        }
        Connections { target:root; function onInvalidInputsChanged() { row.invalidEdit=!!root.invalidInputs[row.parameterKey] } }
        onEdited:function(field,value) { if(field==="inputText1") invalidEdit=!root.edit(parameterKey,String(value).trim()==="" ? NaN : Number(String(value).replace(/\s*(px|s)$/, ""))) }
        LV.Tooltip { target:row.inputControl; automatic:false; visible:row.invalidEdit && row.inputControl.hovered; text:qsTr("Enter a valid value") }
    }
    component SelectRow: Views.PanelRow {
        id: row
        property string parameterKey:""
        property var options:[]
        property var optionValues:options
        type:LV.ListItem.Select; compact:true; fillControl:true; Layout.fillWidth:true; Layout.minimumWidth:0; showLeadingIcon:false; showDescription:false
        selector:({items:row.options})
        Binding { target:row; property:"selectorIndex"; value:Math.max(0,row.optionValues.indexOf(row.parameterKey==="aspectRatio" ? root.aspectRatio : root.values[row.parameterKey])) }
        onEdited:function(field,value) { if(field==="selectorIndex") root.edit(parameterKey,optionValues[value]) }
    }
    component ToggleRow: Views.PanelRow {
        id: row
        property string parameterKey:""
        type:LV.ListItem.Toggle; compact:true; Layout.fillWidth:true; Layout.minimumWidth:0; showLeadingIcon:false; showDescription:false
        Binding { target:row; property:"checked"; value:root.values[row.parameterKey] }
        onEdited:function(field,value) { if(field==="checked") root.edit(parameterKey,value) }
    }
    readonly property string summary: qsTr("%1 video%2 · %3 × %4 px · %5 s · %6 FPS").arg(values.outputCount).arg(values.outputCount===1 ? "" : "s").arg(values.width).arg(values.height).arg(values.duration).arg(values.fps)
    LV.LabelSegmentedControl {
        id: tabs
        objectName:"videoInspectorTabs"
        width:parent.width; height:29
        forceBorderlessTone:false
        LV.LabelButton { id:generationTab; width:(tabs.width-10)/2; text:qsTr("Generation"); tone:root.tab==="Generation" ? LV.AbstractButton.Primary : LV.AbstractButton.Borderless; onClicked:root.tab="Generation" }
        LV.LabelButton { id:shotTab; width:(tabs.width-10)/2; text:qsTr("Shot"); enabled:!!root.timeline.selectedShot.id; tone:root.tab==="Shot" ? LV.AbstractButton.Primary : LV.AbstractButton.Borderless; onClicked:root.tab="Shot" }
    }
    Flickable {
        id: viewport
        objectName:"videoParameterViewport"
        anchors.top:tabs.bottom; anchors.topMargin:4; anchors.bottom:persistentActions.top; anchors.bottomMargin:4
        anchors.left:parent.left; anchors.right:parent.right
        contentWidth:width; contentHeight:content.implicitHeight; clip:true; boundsBehavior:Flickable.StopAtBounds
        flickableDirection:Flickable.VerticalFlick
        Controls.ScrollBar.vertical: Controls.ScrollBar { policy:Controls.ScrollBar.AsNeeded }
        LV.VStack {
            id: content
            width:viewport.width; spacing:8; alignment:Qt.AlignLeft
            LV.VStack {
                visible:root.tab==="Generation"; Layout.fillWidth:true; Layout.minimumWidth:0; spacing:8
                LV.VStack {
                    Layout.fillWidth:true; Layout.minimumWidth:0; Layout.leftMargin:8; Layout.rightMargin:8; Layout.topMargin:4; spacing:4; alignment:Qt.AlignLeft
                    LV.Label { text:qsTr("Prompt"); style:caption; Layout.preferredHeight:LV.Theme.textCaptionLineHeight; Layout.maximumHeight:LV.Theme.textCaptionLineHeight }
                    PromptField { id:prompt; objectName:"videoPrompt"; Layout.fillWidth:true; Layout.minimumWidth:0; text:root.values.prompt; placeholderText:qsTr("Describe the scene, action and atmosphere."); onTextChanged:if(text!==root.values.prompt) root.edit("prompt",text) }
                    LV.Label { text:qsTr("Negative Prompt"); style:caption; Layout.topMargin:8; Layout.preferredHeight:LV.Theme.textCaptionLineHeight; Layout.maximumHeight:LV.Theme.textCaptionLineHeight }
                    PromptField { objectName:"videoNegativePrompt"; Layout.fillWidth:true; Layout.minimumWidth:0; text:root.values.negativePrompt; onTextChanged:if(text!==root.values.negativePrompt) root.edit("negativePrompt",text) }
                }
                LV.VStack {
                    Layout.fillWidth:true; Layout.minimumWidth:0; spacing:0
                    Views.PanelRow {
                        id:modelSelector; objectName:"videoModelSelector"; type:LV.ListItem.Select; compact:true; fillControl:true; Layout.fillWidth:true; Layout.minimumWidth:0
                        label:qsTr("Model"); showLeadingIcon:false; showDescription:false
                        selector:({items:root.modelNames,text:qsTr("Add LTX model in Society")})
                        enabled:root.modelNames.length>0
                        Binding { target:modelSelector; property:"selectorIndex"; value:Math.max(0,root.generation.videoModels.findIndex(model=>model.id===root.generation.selectedVideoModel)) }
                        onEdited:function(field,value) { if(field==="selectorIndex") root.generation.selectedVideoModel=root.generation.videoModels[value].id }
                    }
                    Views.PanelRow {
                        id:presetSelector; objectName:"videoPresetSelector"; type:LV.ListItem.Select; compact:true; fillControl:true
                        Layout.fillWidth:true; Layout.minimumWidth:0; label:qsTr("Select preset"); showLeadingIcon:false; showDescription:false
                        selector:({items:root.presetNames}); Binding { target:presetSelector; property:"selectorIndex"; value:Math.max(0,root.presetNames.indexOf(root.selectedPreset)) }
                        onEdited:function(field,value) { if(field==="selectorIndex") root.loadPreset(root.presetNames[value]) }
                    }
                    Section { label:qsTr("Essentials"); description:qsTr("%1 seconds · %2 FPS · %3 frames").arg(root.values.duration).arg(root.values.fps).arg(root.timeline.totalFrames) }
                    GridLayout {
                        objectName:"videoEssentialsGrid"; Layout.fillWidth:true; Layout.minimumWidth:0
                        columns:width>=360 ? 2 : 1; uniformCellWidths:true; columnSpacing:4; rowSpacing:0
                        NumberRow { objectName:"videoWidth"; fillControl:true; label:qsTr("Width"); parameterKey:"width" }
                        NumberRow { objectName:"videoHeight"; fillControl:true; label:qsTr("Height"); parameterKey:"height" }
                        NumberRow { objectName:"videoDuration"; fillControl:true; label:qsTr("Duration"); parameterKey:"duration" }
                        SelectRow { objectName:"videoFps"; label:qsTr("Frame rate"); parameterKey:"fps"; options:["12 FPS","24 FPS","30 FPS","60 FPS","120 FPS"]; optionValues:[12,24,30,60,120] }
                        SelectRow { objectName:"videoAspectRatio"; label:qsTr("Aspect ratio"); parameterKey:"aspectRatio"; options:["1:1","4:3","3:4","16:9","9:16"].concat(root.aspectRatio===qsTr("Custom") ? [qsTr("Custom")] : []) }
                        Views.PanelRow {
                            id:outputCountRow; objectName:"videoOutputCount"; compact:true; fillControl:true; type:LV.ListItem.Select
                            Layout.fillWidth:true; Layout.minimumWidth:0; label:qsTr("Output count"); showLeadingIcon:false; showDescription:false
                            readonly property var counts: { const common=[1,2,3,4,5,6,7,8,12,16,24,32,64,128,256,512,1000]; return common.indexOf(root.values.outputCount)>=0 ? common : common.concat([root.values.outputCount]).sort((a,b)=>a-b) }
                            selector:({items:counts.map(count=>qsTr("%1 video%2").arg(count).arg(count===1 ? "" : "s")).concat([qsTr("Custom…")])})
                            Binding { target:outputCountRow; property:"selectorIndex"; value:Math.max(0,outputCountRow.counts.indexOf(root.values.outputCount)) }
                            onEdited:function(field,value) { if(field!=="selectorIndex") return; if(value===counts.length) { outputCountInput.text=String(root.values.outputCount); outputCountPopup.open() } else root.edit("outputCount",counts[value]) }
                        }
                    }
                }
                Section { Layout.topMargin:4; label:qsTr("Advanced options"); description:qsTr("All video parameters visible"); showValue:true; value:root.expanded ? qsTr("Expanded") : qsTr("Collapsed"); onClicked:root.expanded=!root.expanded }
                LV.VStack {
                    visible:root.expanded; Layout.fillWidth:true; Layout.minimumWidth:0; Layout.bottomMargin:4; spacing:8
                    LV.VStack {
                        Layout.fillWidth:true; Layout.minimumWidth:0; spacing:0
                        Section { label:qsTr("Camera"); description:qsTr("Combine up to 3 camera movements") }
                        Repeater { model:3; delegate:SelectRow { required property int index; objectName:"videoCameraMotion"+(index+1); label:qsTr("Camera motion %1").arg(index+1); parameterKey:"camera"+(index+1); options:root.cameraNames; optionValues:root.cameraValues } }
                    }
                    LV.VStack {
                        Layout.fillWidth:true; Layout.minimumWidth:0; spacing:0
                        Section { label:qsTr("Image conditions"); description:qsTr("First, last or specific frame") }
                        Repeater {
                            model:[{label:qsTr("First frame image"),action:qsTr("Choose"),frame:0},{label:qsTr("Last frame image"),action:qsTr("Choose"),frame:root.timeline.totalFrames-1},{label:qsTr("Additional keyframe"),action:qsTr("Add"),frame:root.timeline.playhead}]
                            delegate:Views.PanelRow {
                                required property var modelData
                                type:LV.ListItem.Action; compact:true; Layout.topMargin:4; Layout.fillWidth:true; Layout.minimumWidth:0; label:modelData.label; showLeadingIcon:false; showDescription:false
                                primaryAction:({text:modelData.action,tone:LV.AbstractButton.Default})
                                onActionTriggered:root.chooseImage(modelData.frame)
                            }
                        }
                    }
                    LV.VStack {
                        Layout.fillWidth:true; Layout.minimumWidth:0; spacing:0
                        Section { label:qsTr("Sampling"); description:qsTr("Reproduce and refine the generation") }
                        NumberRow { objectName:"videoSteps"; label:qsTr("Steps"); parameterKey:"steps" }
                        NumberRow { objectName:"videoCfgScale"; label:qsTr("CFG scale"); parameterKey:"cfgScale" }
                        ToggleRow { label:qsTr("Random seed"); parameterKey:"randomSeed" }
                        NumberRow { objectName:"videoSeed"; label:qsTr("Seed"); parameterKey:"seed"; enabled:!root.values.randomSeed }
                    }
                    LV.VStack {
                        Layout.fillWidth:true; Layout.minimumWidth:0; spacing:0
                        Section { label:qsTr("Frame interpolation"); description:qsTr("Automatically adjusted for higher FPS") }
                        NumberRow { label:qsTr("Interpolation factor"); parameterKey:"interpolationFactor" }
                    }
                    LV.VStack {
                        Layout.fillWidth:true; Layout.minimumWidth:0; spacing:0
                        Section { label:qsTr("Temporal decoding"); description:qsTr("LTX video and image conditioning") }
                        NumberRow { label:qsTr("Decode timestep"); parameterKey:"decodeTimestep" }
                        NumberRow { label:qsTr("Decode noise scale"); parameterKey:"decodeNoiseScale" }
                        NumberRow { label:qsTr("Image condition noise"); parameterKey:"imageConditionNoise" }
                    }
                    LV.VStack {
                        Layout.fillWidth:true; Layout.minimumWidth:0; spacing:0
                        Section { label:qsTr("Execution"); description:qsTr("Local model and memory settings") }
                        SelectRow { label:qsTr("Device"); parameterKey:"device"; options:["Auto","CPU","Metal","CUDA","ROCm"]; optionValues:["auto","cpu","mps","cuda","rocm"] }
                        SelectRow { label:qsTr("Precision"); parameterKey:"precision"; options:["Auto","Float 32","Float 16","BFloat 16"]; optionValues:["auto","float32","float16","bfloat16"] }
                        SelectRow { label:qsTr("Offload"); parameterKey:"offload"; options:["Auto","None","Model","Sequential"]; optionValues:["auto","none","model","sequential"] }
                        ToggleRow { label:qsTr("CPU text encoding"); parameterKey:"cpuTextEncoding" }
                        ToggleRow { label:qsTr("VAE tiling"); parameterKey:"vaeTiling" }
                    }
                    LV.VStack {
                        Layout.fillWidth:true; Layout.minimumWidth:0; spacing:0
                        Section { label:qsTr("Video output"); description:qsTr("MP4 · H.264") }
                        NumberRow { label:qsTr("Encoding quality · CRF"); parameterKey:"crf" }
                        SelectRow { label:qsTr("Encoding preset"); parameterKey:"encodingPreset"; options:["Medium","Fast","Slow","Very fast","Very slow"]; optionValues:["medium","fast","slow","veryfast","veryslow"] }
                    }
                }
            }
            Views.PanelRow {
                id:saveRow; objectName:"videoSavePreset"; visible:root.tab==="Generation"; compact:true; type:LV.ListItem.Action
                Layout.fillWidth:true; Layout.minimumWidth:0; label:qsTr("Save to preset"); showLeadingIcon:false; showDescription:false
                primaryAction:({text:qsTr("Save"),tone:LV.AbstractButton.Default,enabled:!root.hasInvalidInputs})
                onActionTriggered:{ presetName.text=""; presetPopup.open() }
            }
            Views.PanelRow {
                objectName:"videoGenerateRow"; visible:root.tab==="Generation"; compact:true; type:LV.ListItem.Action
                Layout.fillWidth:true; Layout.minimumWidth:0; label:qsTr("Generate video"); showLeadingIcon:false; showDescription:true
                description:root.summary; primaryAction:({text:qsTr("Generate"),tone:LV.AbstractButton.Primary,enabled:root.canGenerate})
                onActionTriggered:root.generateRequested()
            }
            LV.Label { visible:root.presetFeedback.length>0; text:root.presetFeedback; style:caption; Layout.fillWidth:true; Layout.leftMargin:8; wrapMode:Text.Wrap }
            LV.VStack {
                visible:root.tab==="Shot"; Layout.fillWidth:true; Layout.minimumWidth:0; spacing:8
                Section { label:qsTr("Shot %1").arg(root.timeline.shots.findIndex(shot=>shot.id===root.timeline.selectedShot.id)+1); description:qsTr("F%1–%2 · %3 frames").arg(root.timeline.selectedStart).arg(root.timeline.selectedEnd).arg(root.timeline.selectedShot.frames || 0) }
                Views.PanelRow { compact:true; type:LV.ListItem.InlineEdit; Layout.fillWidth:true; Layout.minimumWidth:0; showLeadingIcon:false; showDescription:false; showPrimaryAction:false; label:qsTr("Name"); inputText1:root.timeline.selectedShot.name || ""; onEdited:function(field,value) { if(field==="inputText1") root.timeline.changeShot(root.timeline.selectedShot.id,"name",value) } }
                Views.PanelRow { compact:true; type:LV.ListItem.Toggle; Layout.fillWidth:true; Layout.minimumWidth:0; showLeadingIcon:false; showDescription:false; label:qsTr("Exclude from generation"); checked:!!root.timeline.selectedShot.excluded; onEdited:function(field,value) { if(field==="checked") root.timeline.changeShot(root.timeline.selectedShot.id,"excluded",value) } }
                Views.PanelRow { compact:true; type:LV.ListItem.Toggle; Layout.fillWidth:true; Layout.minimumWidth:0; showLeadingIcon:false; showDescription:false; label:qsTr("Lock"); checked:!!root.timeline.selectedShot.locked; onEdited:function(field,value) { if(field==="checked") root.timeline.changeShot(root.timeline.selectedShot.id,"locked",value) } }
                LV.Label { text:qsTr("Shot prompt"); style:caption; Layout.leftMargin:16 }
                PromptField { objectName:"videoShotPrompt"; Layout.fillWidth:true; Layout.minimumWidth:0; Layout.leftMargin:16; Layout.rightMargin:16; text:root.timeline.selectedShot.prompt || ""; placeholderText:qsTr("Use the composition prompt"); onTextChanged:if(text!==(root.timeline.selectedShot.prompt || "")) root.timeline.changeShot(root.timeline.selectedShot.id,"prompt",text) }
                Views.PanelRow { compact:true; type:LV.ListItem.Action; Layout.fillWidth:true; Layout.minimumWidth:0; label:qsTr("Duplicate shot"); showLeadingIcon:false; showDescription:false; primaryAction:({text:qsTr("Duplicate"),tone:LV.AbstractButton.Default}); onActionTriggered:root.timeline.duplicateShot() }
            }
            LV.Label { Layout.fillWidth:true; Layout.minimumWidth:0; Layout.leftMargin:16; Layout.rightMargin:16; Layout.bottomMargin:16; visible:root.hasInvalidInputs; text:qsTr("Correct invalid parameter values before generating."); style:caption; wrapMode:Text.Wrap; sizeToContentHeight:true }
        }
    }
    ColumnLayout {
        id:persistentActions; objectName:"videoPersistentActions"
        anchors.left:parent.left; anchors.right:parent.right; anchors.bottom:parent.bottom
        spacing:8
        LV.Label { text:root.summary; style:caption; Layout.fillWidth:true; Layout.preferredHeight:LV.Theme.textCaptionLineHeight; Layout.maximumHeight:LV.Theme.textCaptionLineHeight; Layout.topMargin:12; Layout.leftMargin:12; Layout.rightMargin:12; elide:Text.ElideRight }
        RowLayout {
            Layout.fillWidth:true; Layout.leftMargin:12; Layout.rightMargin:12; Layout.bottomMargin:12; spacing:8
            LV.LabelButton { objectName:"videoResetDefaults"; Layout.preferredWidth:64; text:qsTr("Reset"); tone:LV.AbstractButton.Default; onClicked:root.resetDraft() }
            Item { Layout.fillWidth:true }
            LV.LabelButton { objectName:"videoGenerateComposition"; Layout.preferredWidth:164; text:qsTr("Generate · %1 video%2").arg(root.values.outputCount).arg(root.values.outputCount===1 ? "" : "s"); tone:LV.AbstractButton.Primary; enabled:root.canGenerate && !root.pending; onClicked:root.generateRequested() }
        }
    }
    Controls.Popup {
        id:presetPopup; objectName:"videoPresetPopup"; anchors.centerIn:parent; modal:true; focus:true; padding:LV.Theme.gap16
        width:Math.min(320,root.width-16)
        background:Rectangle { color:LV.Theme.panelBackground10; radius:LV.Theme.radiusLg }
        contentItem:ColumnLayout {
            spacing:8
            LV.Label { text:qsTr("Preset name"); style:body }
            LV.InputField { id:presetName; objectName:"videoPresetName"; Layout.fillWidth:true; clearButtonVisible:false; placeholderText:qsTr("Name") }
            RowLayout {
                Layout.fillWidth:true
                LV.LabelButton { text:qsTr("Cancel"); tone:LV.AbstractButton.Default; onClicked:presetPopup.close() }
                Item { Layout.fillWidth:true }
                LV.LabelButton { objectName:"videoConfirmPresetSave"; text:qsTr("Save"); tone:LV.AbstractButton.Default; enabled:presetName.text.trim().length>0 && !root.hasInvalidInputs; onClicked:if(root.savePreset(presetName.text)) presetPopup.close() }
            }
        }
    }

    Controls.Popup {
        id:outputCountPopup; onClosed:outputCountRow.selectorIndex=Math.max(0,outputCountRow.counts.indexOf(root.values.outputCount)); objectName:"videoOutputCountPopup"; anchors.centerIn:parent; modal:true; focus:true; padding:LV.Theme.gap16
        width:Math.min(300,root.width-16)
        background:Rectangle { color:LV.Theme.panelBackground10; radius:LV.Theme.radiusLg }
        contentItem:ColumnLayout {
            spacing:8
            LV.Label { text:qsTr("Output count · 1–1000"); style:body }
            LV.InputField { id:outputCountInput; objectName:"videoCustomOutputCount"; Layout.fillWidth:true; clearButtonVisible:false }
            RowLayout {
                Layout.fillWidth:true
                LV.LabelButton { text:qsTr("Cancel"); tone:LV.AbstractButton.Default; onClicked:outputCountPopup.close() }
                Item { Layout.fillWidth:true }
                LV.LabelButton { text:qsTr("Apply"); tone:LV.AbstractButton.Default; enabled:/^[0-9]+$/.test(outputCountInput.text) && Number(outputCountInput.text)>=1 && Number(outputCountInput.text)<=1000; onClicked:if(root.edit("outputCount",Number(outputCountInput.text))) outputCountPopup.close() }
            }
        }
    }

}
