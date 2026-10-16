pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

Column {
    id: root
    objectName: "backgroundRemoveBody"
    required property var toolState
    readonly property var values: toolState.values
    readonly property var engine: toolState.engine
    readonly property var preview: value("backgroundRemovalPreview",({}))
    property bool dropdownActive: false
    readonly property bool modalActive: dropdownActive
    component Parameter: EditorBackgroundParameter { onModalActiveChanged: root.dropdownActive=modalActive }
    width: parent.width; spacing: 16
    function value(key, fallback) { return values[key] === undefined ? fallback : values[key] }
    function edit(key, next) { toolState.setValue(key,next) }
    function run() {
        if (!engine) return
        const result=engine.previewBackgroundRemoval(values)
        edit("backgroundRemovalPreview",result)
        toolState.notice=result.ready?"":result.error || ""
    }
    function refine(key,next) { const ready=preview.ready; edit(key,next); if(ready)run() }
    function resetEdges() {
        const defaults={removeThreshold:50,removeRadius:3,removeSmooth:20,removeFeather:.8,removeShift:0,removeHair:true,removeDecontaminate:true}
        Object.keys(defaults).forEach(key=>edit(key,defaults[key]))
        if(preview.ready)run()
    }
    function reset() {
        edit("backgroundRemovalPreview",({}));resetEdges()
        const defaults={removeInput:"Selected canvas layer",removeModel:"Automatic",removeKeep:"All foreground subjects",removeQuality:"Balanced",removeView:"Split",removeOverlay:false,removeResult:"New layer with mask",removePreserve:true}
        Object.keys(defaults).forEach(key=>edit(key,defaults[key]));toolState.notice=""
    }
    function apply(asMask) {
        if (!engine || !preview.ready) return
        if (engine.applyBackgroundRemoval(values)) {
            edit("backgroundRemovalPreview",({}))
            if(asMask)edit("backgroundView","Mask")
        } else toolState.notice=engine.error
    }
    EditorBackgroundSection {
        title:qsTr("ML background removal")
        Parameter { field:"removeInput"; title:qsTr("Input"); value:root.value(field,"Selected canvas layer"); options:["Selected canvas layer"]; onEdited:next=>root.edit(field,next) }
        Parameter { field:"removeModel"; title:qsTr("Model"); value:root.value(field,"Automatic"); options:["Automatic"]; onEdited:next=>root.edit(field,next) }
        Parameter { field:"removeKeep"; title:qsTr("Keep"); value:root.value(field,"All foreground subjects"); options:["All foreground subjects","Selected subject"]; onEdited:next=>root.refine(field,next) }
        Parameter { field:"removeQuality"; title:qsTr("Quality"); type:"choices"; value:root.value(field,"Balanced"); options:["Fast","Balanced","Precise"]; onEdited:next=>root.edit(field,next) }
        Row {
            spacing:8
            LV.LabelButton { objectName:"background-remove"; text:qsTr("Remove background"); tone:LV.AbstractButton.Default; enabled:Boolean(root.engine)&&root.engine.documentReady; onClicked:root.run() }
            LV.LabelButton { text:qsTr("Cancel"); tone:LV.AbstractButton.Borderless; onClicked:{root.edit("backgroundRemovalPreview",({}));root.toolState.notice=""} }
        }
        LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width:parent.width; style:caption; color:LV.Theme.bodyColor; text:root.toolState.notice || (root.preview.ready?qsTr("Removal preview ready"):qsTr("Ready to analyze the selected layer")); wrapMode:Text.WordWrap; sizeToContentHeight:true }
    }
    EditorBackgroundSection {
        title:qsTr("Preview")
        Rectangle {
            objectName:"backgroundRemovalPreview"; width:parent.width; height:root.preview.ready?156:108; radius:4; color:LV.Theme.surfaceRaised
            Column {
                visible:!root.preview.ready; x:12; y:12; width:parent.width-24; spacing:8
                Image { width:24; height:24; source:Qt.resolvedUrl("Assets/Background/remove.svg"); sourceSize:Qt.size(48,48) }
                LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width:parent.width; height:18; style:body; color:LV.Theme.titleHeaderColor; text:qsTr("No removal preview yet") }
                LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width:parent.width; style:caption; color:LV.Theme.bodyColor; text:qsTr("Run background removal to review the cutout and its transparent background."); wrapMode:Text.WordWrap; sizeToContentHeight:true }
            }
            Item {
                visible:Boolean(root.preview.ready); anchors.fill:parent; clip:true
                Image { anchors.fill:parent; source:root.value("removeView","Split")==="Before"?root.preview.before || "":root.preview.after || ""; fillMode:Image.PreserveAspectFit }
                Item { visible:root.value("removeView","Split")==="Split"; width:parent.width/2; height:parent.height; clip:true; Image { width:parent.width*2; height:parent.height; source:root.preview.before || ""; fillMode:Image.PreserveAspectFit } }
                Image { visible:root.value("removeOverlay",false); anchors.fill:parent; source:root.preview.mask || ""; opacity:.5; fillMode:Image.PreserveAspectFit }
            }
        }
        Parameter { field:"removeView"; title:qsTr("View"); type:"choices"; value:root.value(field,"Split"); options:["Before","After","Split"]; onEdited:next=>root.edit(field,next) }
        Parameter { field:"removeOverlay"; title:qsTr("Show mask overlay"); type:"toggle"; value:root.value(field,false); onEdited:next=>root.edit(field,next) }
    }
    EditorBackgroundSection {
        title:qsTr("Refine edges")
        Parameter { field:"removeThreshold"; title:qsTr("Detection threshold"); type:"slider"; value:root.value(field,50); onEdited:next=>root.refine(field,next) }
        Parameter { field:"removeRadius"; title:qsTr("Edge radius"); type:"slider"; unit:"px"; maximum:10; step:.1; value:root.value(field,3); onEdited:next=>root.refine(field,next) }
        Parameter { field:"removeSmooth"; title:qsTr("Smooth edges"); type:"slider"; value:root.value(field,20); onEdited:next=>root.refine(field,next) }
        Parameter { field:"removeFeather"; title:qsTr("Feather"); type:"slider"; unit:"px"; maximum:10; step:.1; value:root.value(field,.8); onEdited:next=>root.refine(field,next) }
        Parameter { field:"removeShift"; title:qsTr("Shift edge"); type:"slider"; minimum:-100; value:root.value(field,0); onEdited:next=>root.refine(field,next) }
        Parameter { field:"removeHair"; title:qsTr("Recover hair and fine detail"); type:"toggle"; value:root.value(field,true); onEdited:next=>root.refine(field,next) }
        Parameter { field:"removeDecontaminate"; title:qsTr("Decontaminate edge colors"); type:"toggle"; value:root.value(field,true); onEdited:next=>root.refine(field,next) }
        Row {
            spacing:8
            LV.LabelButton { text:qsTr("Refine with brush"); tone:LV.AbstractButton.Default; onClicked:{if(root.preview.ready)root.apply(true);else root.edit("backgroundView","Mask");root.edit("maskTool","Brush")} }
            LV.LabelButton { text:qsTr("Edit as mask"); tone:LV.AbstractButton.Borderless; onClicked:{if(root.preview.ready)root.apply(true);else root.edit("backgroundView","Mask")} }
        }
    }
    EditorBackgroundSection {
        title:qsTr("Output")
        Parameter { field:"removeResult"; title:qsTr("Result"); value:root.value(field,"New layer with mask"); options:["New layer with mask","Current layer mask"]; onEdited:next=>root.edit(field,next) }
        Parameter { field:"removePreserve"; title:qsTr("Preserve the original layer"); type:"toggle"; value:root.value(field,true); onEdited:next=>root.edit(field,next) }
        Row {
            spacing:8
            LV.LabelButton { objectName:"background-applyRemoval"; text:qsTr("Apply removal"); tone:LV.AbstractButton.Default; enabled:Boolean(root.preview.ready); onClicked:root.apply(false) }
            LV.LabelButton { text:qsTr("Reset edges"); tone:LV.AbstractButton.Borderless; onClicked:root.resetEdges() }
        }
        LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; visible:!root.preview.ready; width:parent.width; height:18; style:caption; color:LV.Theme.bodyColor; text:qsTr("Apply becomes available after a removal preview is ready.") }
    }
}
