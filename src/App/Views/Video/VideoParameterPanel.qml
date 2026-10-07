pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import LVRS 1.0 as LV
import ".." as Views
import "../Home"

Item {
    id: root
    objectName: "videoParameterPanel"
    required property var generation
    required property VideoTimelineState timeline
    property string tab: "Generation"
    property bool expanded: true
    property var invalidInputs: ({})
    readonly property bool hasInvalidInputs: Object.keys(invalidInputs).length>0
    property var values: defaults()
    property string message: ""
    readonly property var modelNames: generation.videoModels.map(model=>model.name || model.id)
    signal generateRequested()
    signal recipeSaved()
    function defaults() { return {prompt:"",negativePrompt:"Blur, flicker, inconsistent motion",width:1024,height:576,duration:5,fps:24,outputCount:1,steps:30,cfgScale:3,seed:42,randomSeed:true,interpolationFactor:2,decodeTimestep:0.05,decodeNoiseScale:0.025,imageConditionNoise:0,device:"auto",precision:"auto",offload:"auto",cpuTextEncoding:true,vaeTiling:true,crf:18,encodingPreset:"medium"} }
    function edit(key, value) {
        const ranges={width:[32,4096],height:[32,4096],duration:[1,30],fps:[12,30],outputCount:[1,1000],steps:[1,1000],cfgScale:[0,100],seed:[0,4294967295],interpolationFactor:[2,8],decodeTimestep:[0,1],decodeNoiseScale:[0,1],imageConditionNoise:[0,1],crf:[0,51]}
        const valid=Object.assign({},invalidInputs), range=ranges[key]
        if(range && (!Number.isFinite(value) || value<range[0] || value>range[1] || (["width","height","duration","fps","outputCount","steps","seed","interpolationFactor","crf"].indexOf(key)>=0 && Math.round(value)!==value) || (["width","height"].indexOf(key)>=0 && value%32!==0))) { valid[key]=true; invalidInputs=valid; return false }
        delete valid[key]; invalidInputs=valid; values=Object.assign({},values,{[key]:value})
        if(key==="duration" || key==="fps") { timeline.fps=values.fps; timeline.resizeComposition(values.duration*values.fps) }
        return true
    }
    function parameters() { return Object.assign({},values,{seed:values.randomSeed ? -1 : values.seed,shots:timeline.recipeShots(values.prompt)}) }
    function focusPrompt() { tab="Generation"; viewport.contentY=0; prompt.forceActiveFocus() }
    function resetDraft() { values=defaults(); invalidInputs=({}); message=""; timeline.fps=24; timeline.resizeComposition(120) }
    function savePreset() { if(hasInvalidInputs) return false; presets.savedRecipe=JSON.stringify({parameters:values,timeline:timeline.snapshot()}); message=qsTr("Recipe saved"); recipeSaved(); return true }
    function loadPreset() {
        if(!presets.savedRecipe) return false
        try { const saved=JSON.parse(presets.savedRecipe); values=Object.assign(defaults(),saved.parameters); timeline.restore(saved.timeline); timeline.fps=values.fps; invalidInputs=({}); message=qsTr("Saved recipe loaded"); return true } catch(error) { message=qsTr("Cannot load this recipe"); return false }
    }
    Settings { id: presets; category: "VideoGeneration"; property string savedRecipe: "" }
    FileDialog {
        id: imageDialog
        property int targetFrame: 0
        title: qsTr("Choose an image keyframe")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.heic *.avif)")]
        onAccepted: root.timeline.addKey(selectedFile,targetFrame)
    }
    function chooseImage(frame) { imageDialog.targetFrame=frame; imageDialog.open() }
    component Section: Views.PanelRow {
        type: LV.ListItem.Navigation; Layout.fillWidth:true
        showLeadingIcon:false; showValue:false; showTrailingIcon:false; showDescription:true
        navigationItemWidth:300
    }
    component NumberRow: Views.PanelRow {
        id: row
        property string parameterKey:""
        property bool invalidEdit:false
        type:LV.ListItem.InlineEdit; Layout.fillWidth:true
        showLeadingIcon:false; showDescription:false; showPrimaryAction:false; inputWidth:140
        input1:({clearButtonVisible:false})
        Binding { target:row; property:"inputText1"; value:String(root.values[row.parameterKey]); when:!row.invalidEdit; restoreMode:Binding.RestoreNone }
        Connections { target:root; function onInvalidInputsChanged() { if(!root.invalidInputs[row.parameterKey]) row.invalidEdit=false } }
        onEdited:function(field,value) { if(field==="inputText1") invalidEdit=!root.edit(parameterKey,String(value).trim()==="" ? NaN : Number(value)) }
        LV.Tooltip { target:row.inputControl; automatic:false; visible:row.invalidEdit && row.inputControl.hovered; text:qsTr("Enter a valid value") }
    }
    component SelectRow: Views.PanelRow {
        id: row
        property string parameterKey:""
        property var options:[]
        property var optionValues:options
        type:LV.ListItem.Select; Layout.fillWidth:true; showLeadingIcon:false; showDescription:false
        selector:({items:row.options})
        Binding { target:row; property:"selectorIndex"; value:Math.max(0,row.optionValues.indexOf(root.values[row.parameterKey])) }
        onEdited:function(field,value) { if(field==="selectorIndex") root.edit(parameterKey,optionValues[value]) }
    }
    component ToggleRow: Views.PanelRow {
        id: row
        property string parameterKey:""
        type:LV.ListItem.Toggle; Layout.fillWidth:true; showLeadingIcon:false; showDescription:false
        Binding { target:row; property:"checked"; value:root.values[row.parameterKey] }
        onEdited:function(field,value) { if(field==="checked") root.edit(parameterKey,value) }
    }
    LV.LabelSegmentedControl {
        id: tabs
        objectName:"videoInspectorTabs"
        width:parent.width; height:29
        forceBorderlessTone:false
        LV.LabelButton { width:(tabs.width-10)/2; height:22; text:qsTr("Generation"); tone:root.tab==="Generation" ? LV.AbstractButton.Primary : LV.AbstractButton.Borderless; onClicked:root.tab="Generation" }
        LV.LabelButton { width:(tabs.width-10)/2; height:22; text:qsTr("Shot"); tone:root.tab==="Shot" ? LV.AbstractButton.Primary : LV.AbstractButton.Borderless; onClicked:root.tab="Shot" }
    }
    Flickable {
        id: viewport
        objectName:"videoParameterViewport"
        anchors.top:tabs.bottom; anchors.topMargin:8; anchors.bottom:parent.bottom; width:parent.width
        contentWidth:width; contentHeight:content.implicitHeight; clip:true; boundsBehavior:Flickable.StopAtBounds
        Controls.ScrollBar.vertical: Controls.ScrollBar { policy:Controls.ScrollBar.AsNeeded }
        LV.VStack {
            id: content
            width:viewport.width; spacing:12; alignment:Qt.AlignLeft
            LV.VStack {
                visible:root.tab==="Generation"; Layout.fillWidth:true; spacing:12
                LV.VStack {
                    Layout.fillWidth:true; Layout.leftMargin:16; Layout.rightMargin:16; Layout.topMargin:8; spacing:8; alignment:Qt.AlignLeft
                    LV.Label { text:qsTr("Prompt"); style:caption }
                    PromptField { id:prompt; objectName:"videoPrompt"; Layout.fillWidth:true; text:root.values.prompt; placeholderText:qsTr("Describe the scene, action and atmosphere."); onTextChanged:if(text!==root.values.prompt) root.edit("prompt",text) }
                    LV.Label { text:qsTr("Negative Prompt"); style:caption; Layout.topMargin:6 }
                    PromptField { objectName:"videoNegativePrompt"; Layout.fillWidth:true; text:root.values.negativePrompt; onTextChanged:if(text!==root.values.negativePrompt) root.edit("negativePrompt",text) }
                }
                LV.VStack {
                    Layout.fillWidth:true; spacing:0
                    Views.PanelRow {
                        objectName:"videoModelSelector"; type:LV.ListItem.Select; Layout.fillWidth:true
                        label:qsTr("Model"); showLeadingIcon:false; showDescription:false
                        selector:({items:root.modelNames,text:qsTr("Add LTX model in Society")})
                        enabled:root.modelNames.length>0
                        selectorIndex:Math.max(0,root.generation.videoModels.findIndex(model=>model.id===root.generation.selectedVideoModel))
                        onEdited:function(field,value) { if(field==="selectorIndex") root.generation.selectedVideoModel=root.generation.videoModels[value].id }
                    }
                    Views.PanelRow {
                        type:LV.ListItem.Select; Layout.fillWidth:true; label:qsTr("Select preset"); showLeadingIcon:false; showDescription:false
                        selector:({items:[qsTr("Default"),qsTr("Saved recipe")]})
                        onEdited:function(field,value) { if(field==="selectorIndex") { if(value===1) root.loadPreset(); else root.resetDraft() } }
                    }
                    Section { label:qsTr("Essentials"); description:qsTr("%1 seconds · %2 FPS · %3 frames").arg(root.values.duration).arg(root.values.fps).arg(root.timeline.totalFrames) }
                    NumberRow { objectName:"videoWidth"; label:qsTr("Width"); parameterKey:"width" }
                    NumberRow { objectName:"videoHeight"; label:qsTr("Height"); parameterKey:"height" }
                    NumberRow { objectName:"videoDuration"; label:qsTr("Duration"); parameterKey:"duration" }
                    SelectRow { objectName:"videoFps"; label:qsTr("Frame rate"); parameterKey:"fps"; options:["12 FPS","24 FPS","30 FPS"]; optionValues:[12,24,30] }
                    NumberRow { objectName:"videoOutputCount"; label:qsTr("Output count"); parameterKey:"outputCount" }
                }
                Section { label:qsTr("Advanced options"); description:qsTr("Image conditions, sampling and output"); showValue:true; value:root.expanded ? qsTr("Expanded") : qsTr("Collapsed"); onClicked:root.expanded=!root.expanded }
                LV.VStack {
                    visible:root.expanded; Layout.fillWidth:true; spacing:12
                    LV.VStack {
                        Layout.fillWidth:true; spacing:0
                        Section { label:qsTr("Image conditions"); description:qsTr("First, last or specific frame") }
                        Repeater {
                            model:[{label:qsTr("First frame image"),action:qsTr("Choose"),frame:0},{label:qsTr("Last frame image"),action:qsTr("Choose"),frame:root.timeline.totalFrames-1},{label:qsTr("Additional keyframe"),action:qsTr("Add"),frame:root.timeline.playhead}]
                            delegate:Views.PanelRow {
                                required property var modelData
                                type:LV.ListItem.Action; Layout.fillWidth:true; label:modelData.label; showLeadingIcon:false; showDescription:false
                                primaryAction:({text:modelData.action,tone:LV.AbstractButton.Default})
                                onActionTriggered:root.chooseImage(modelData.frame)
                            }
                        }
                    }
                    LV.VStack {
                        Layout.fillWidth:true; spacing:0
                        Section { label:qsTr("Sampling"); description:qsTr("Reproduce and refine the generation") }
                        NumberRow { objectName:"videoSteps"; label:qsTr("Steps"); parameterKey:"steps" }
                        NumberRow { objectName:"videoCfgScale"; label:qsTr("CFG scale"); parameterKey:"cfgScale" }
                        ToggleRow { label:qsTr("Random seed"); parameterKey:"randomSeed" }
                        NumberRow { objectName:"videoSeed"; label:qsTr("Seed"); parameterKey:"seed"; enabled:!root.values.randomSeed }
                    }
                    LV.VStack {
                        Layout.fillWidth:true; spacing:0
                        Section { label:qsTr("Frame interpolation"); description:qsTr("Applied automatically above 12 FPS") }
                        NumberRow { label:qsTr("Interpolation factor"); parameterKey:"interpolationFactor" }
                    }
                    LV.VStack {
                        Layout.fillWidth:true; spacing:0
                        Section { label:qsTr("Temporal decoding"); description:qsTr("LTX video and image conditioning") }
                        NumberRow { label:qsTr("Decode timestep"); parameterKey:"decodeTimestep" }
                        NumberRow { label:qsTr("Decode noise scale"); parameterKey:"decodeNoiseScale" }
                        NumberRow { label:qsTr("Image condition noise"); parameterKey:"imageConditionNoise" }
                    }
                    LV.VStack {
                        Layout.fillWidth:true; spacing:0
                        Section { label:qsTr("Execution"); description:qsTr("Local model and memory settings") }
                        SelectRow { label:qsTr("Device"); parameterKey:"device"; options:["Auto","CPU","Metal","CUDA","ROCm"]; optionValues:["auto","cpu","mps","cuda","rocm"] }
                        SelectRow { label:qsTr("Precision"); parameterKey:"precision"; options:["Auto","Float 32","Float 16","BFloat 16"]; optionValues:["auto","float32","float16","bfloat16"] }
                        SelectRow { label:qsTr("Offload"); parameterKey:"offload"; options:["Auto","None","Model","Sequential"]; optionValues:["auto","none","model","sequential"] }
                        ToggleRow { label:qsTr("CPU text encoding"); parameterKey:"cpuTextEncoding" }
                        ToggleRow { label:qsTr("VAE tiling"); parameterKey:"vaeTiling" }
                    }
                    LV.VStack {
                        Layout.fillWidth:true; spacing:0
                        Section { label:qsTr("Video output"); description:qsTr("MP4 · H.264") }
                        NumberRow { label:qsTr("Encoding quality · CRF"); parameterKey:"crf" }
                        SelectRow { label:qsTr("Encoding preset"); parameterKey:"encodingPreset"; options:["Medium","Fast","Slow","Very fast","Very slow"]; optionValues:["medium","fast","slow","veryfast","veryslow"] }
                    }
                }
                Views.PanelRow { objectName:"videoSavePreset"; type:LV.ListItem.Action; Layout.fillWidth:true; label:qsTr("Save to preset"); showLeadingIcon:false; showDescription:false; primaryAction:({text:qsTr("Save"),tone:LV.AbstractButton.Default,enabled:!root.hasInvalidInputs}); onActionTriggered:root.savePreset() }
            }
            LV.VStack {
                visible:root.tab==="Shot"; Layout.fillWidth:true; spacing:8
                Section { label:qsTr("Shot %1").arg(root.timeline.shots.findIndex(shot=>shot.id===root.timeline.selectedShot.id)+1); description:qsTr("F%1–%2 · %3 frames").arg(root.timeline.selectedStart).arg(root.timeline.selectedEnd).arg(root.timeline.selectedShot.frames || 0) }
                Views.PanelRow { type:LV.ListItem.InlineEdit; Layout.fillWidth:true; showLeadingIcon:false; showDescription:false; showPrimaryAction:false; label:qsTr("Name"); inputText1:root.timeline.selectedShot.name || ""; onEdited:function(field,value) { if(field==="inputText1") root.timeline.changeShot(root.timeline.selectedShot.id,"name",value) } }
                Views.PanelRow { type:LV.ListItem.Toggle; Layout.fillWidth:true; showLeadingIcon:false; showDescription:false; label:qsTr("Exclude from generation"); checked:!!root.timeline.selectedShot.excluded; onEdited:function(field,value) { if(field==="checked") root.timeline.changeShot(root.timeline.selectedShot.id,"excluded",value) } }
                Views.PanelRow { type:LV.ListItem.Toggle; Layout.fillWidth:true; showLeadingIcon:false; showDescription:false; label:qsTr("Lock"); checked:!!root.timeline.selectedShot.locked; onEdited:function(field,value) { if(field==="checked") root.timeline.changeShot(root.timeline.selectedShot.id,"locked",value) } }
                LV.Label { text:qsTr("Shot prompt"); style:caption; Layout.leftMargin:16 }
                PromptField { objectName:"videoShotPrompt"; Layout.fillWidth:true; Layout.leftMargin:16; Layout.rightMargin:16; text:root.timeline.selectedShot.prompt || ""; placeholderText:qsTr("Use the composition prompt"); onTextChanged:if(text!==(root.timeline.selectedShot.prompt || "")) root.timeline.changeShot(root.timeline.selectedShot.id,"prompt",text) }
                Views.PanelRow { type:LV.ListItem.Action; Layout.fillWidth:true; label:qsTr("Duplicate shot"); showLeadingIcon:false; showDescription:false; primaryAction:({text:qsTr("Duplicate"),tone:LV.AbstractButton.Default}); onActionTriggered:root.timeline.duplicateShot() }
            }
            LV.Label { Layout.fillWidth:true; Layout.leftMargin:16; Layout.rightMargin:16; Layout.bottomMargin:16; visible:text.length>0; text:root.hasInvalidInputs ? qsTr("Correct invalid parameter values before generating.") : root.message; style:caption; wrapMode:Text.Wrap; sizeToContentHeight:true }
        }
    }
}
