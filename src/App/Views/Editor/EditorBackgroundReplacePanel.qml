pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import LVRS 1.0 as LV

Column {
    id: root
    objectName:"backgroundReplaceBody"
    required property var toolState
    readonly property var values:toolState.values
    readonly property var engine:toolState.engine
    property bool dropdownActive: false
    readonly property bool modalActive:dropdownActive || sourceDialog.visible || recentMenu.visible
    readonly property var recent:value("replaceRecentFiles",[])
    component Parameter: EditorBackgroundParameter { onModalActiveChanged: root.dropdownActive=modalActive }
    width:parent.width; spacing:16
    function value(field,fallback){return values[field]===undefined?fallback:values[field]}
    function edit(field,next){toolState.setValue(field,next)}
    function choose(source,kind){edit(kind==="Image"?"replaceImage":"replaceFile",String(source));edit("replaceSource",kind);edit("replaceRecentFiles",[String(source)].concat(recent.filter(url=>url!==String(source))).slice(0,10))}
    function pick(kind){sourceDialog.kind=kind;sourceDialog.open()}
    function generate(){if(!engine||!engine.documentReady){toolState.notice=qsTr("Open a canvas first.");return}const next=Object.assign({},values,{"field-12":value("replacePrompt","Soft studio background, warm daylight"),"field-13":"Generate"});toolState.actionRequested("background","field-15",next)}
    function reset(){const defaults={replaceSource:"Generative",replacePrompt:"Soft studio background, warm daylight",replaceModel:"Current generation model",replaceVariations:4,replaceStrength:60,replaceSeed:-1,replaceFit:"Fill",replaceScale:100,replaceAngle:0,replaceX:0,replaceY:0,replaceBlend:"Normal",replaceOpacity:100,replaceMatch:84,replacePreserve:true,replaceLighting:true,replaceResult:"New editable layer"};Object.keys(defaults).forEach(key=>edit(key,defaults[key]));toolState.notice=""}
    function apply(){const source=value("replaceSource","Generative");if(source==="Generative"){generate();return}if(engine)toolState.notice=engine.applyBackgroundReplacement(source==="Image"?value("replaceImage",""):value("replaceFile",""),values)?"":engine.error}
    component IconAction: LV.IconButton {
        required property string actionName
        width:22; height:22; iconSize:18; tone:LV.AbstractButton.Borderless
        Accessible.name:actionName
    }
    component Well: Rectangle {
        required property string label
        required property string kind
        width:root.width; height:44; radius:4; color:LV.Theme.surfaceRaised
        LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; x:12; width:parent.width-24; height:parent.height; style:body; color:LV.Theme.bodyColor; text:parent.label; verticalAlignment:Text.AlignVCenter }
        DropArea { anchors.fill:parent; onDropped:drop=>{if(drop.urls.length){root.choose(drop.urls[0],parent.kind);drop.acceptProposedAction()}} }
    }
    EditorBackgroundSection {
        title:qsTr("Source")
        Parameter { field:"replaceTarget"; title:qsTr("Target"); value:root.value(field,"Background / current mask"); options:["Background / current mask"]; onEdited:next=>root.edit(field,next) }
        Row {
            spacing:8
            Repeater {
                model:[{name:"Generative",icon:"panel-generate-text-image"},{name:"Image",icon:"panel-photo-source"},{name:"File",icon:"panel-file-open"}]
                IconAction { id: selectionButton1; required property var modelData; objectName:"background-source-"+modelData.name; actionName:modelData.name; iconName:modelData.icon; tone: LV.AbstractButton.Borderless; onClicked:root.edit("replaceSource",modelData.name)
                    Accessible.checkable: selectionButton1.checkable
                    Accessible.checked: selectionButton1.checked
                    EditorButtonState { button: selectionButton1; selected: Boolean(root.value("replaceSource","Generative")===modelData.name) }
                    Accessible.onToggleAction: selectionButton1.click()
                }
            }
        }
    }
    EditorBackgroundSection {
        title:qsTr("Generate")
        LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width:parent.width; height:18; style:body; color:LV.Theme.titleHeaderColor; text:qsTr("Prompt") }
        Parameter { field:"replacePrompt"; type:"text"; value:root.value(field,"Soft studio background, warm daylight"); onEdited:next=>root.edit(field,next) }
        Parameter { field:"replaceModel"; title:qsTr("Model"); value:root.value(field,"Current generation model"); options:["Current generation model"].concat(root.toolState.generationRuntime?root.toolState.generationRuntime.models.map(model=>({label:model.name || model.id,value:model.id})):[]); onEdited:next=>root.edit(field,next) }
        Parameter { field:"replaceVariations"; title:qsTr("Variations"); type:"number"; unit:""; minimum:1; maximum:16; value:root.value(field,4); onEdited:next=>root.edit(field,next) }
        Parameter { field:"replaceStrength"; title:qsTr("Strength"); type:"slider"; value:root.value(field,60); onEdited:next=>root.edit(field,next) }
        Row {
            spacing:8
            IconAction { objectName:"background-generate"; actionName:qsTr("Generate replacement"); iconName:"panel-generate-text-image"; tone:LV.AbstractButton.Default; enabled:Boolean(root.engine)&&root.engine.documentReady; onClicked:root.generate() }
            IconAction { actionName:qsTr("Random seed"); iconName:"generalrefresh"; onClicked:root.edit("replaceSeed",Math.floor(Math.random()*2147483647)) }
        }
    }
    EditorBackgroundSection {
        title:qsTr("Image")
        Well { label:qsTr("Drop image"); kind:"Image" }
        Row {
            spacing:8
            IconAction { actionName:qsTr("Choose image"); iconName:"panel-photo-source"; onClicked:root.pick("Image") }
            IconAction { actionName:qsTr("Paste image"); iconName:"generalpaste"; onClicked:{if(root.engine&&root.engine.captureBackgroundClipboard())root.choose("clipboard:image","Image");else if(root.engine)root.toolState.notice=root.engine.error} }
        }
        Parameter { field:"replaceImage"; title:qsTr("Selected image"); type:"text"; readOnly:true; value:root.value(field,qsTr("No image selected")) }
    }
    EditorBackgroundSection {
        title:qsTr("File")
        Well { label:qsTr("Drop image or canvas"); kind:"File" }
        Row {
            spacing:8
            IconAction { actionName:qsTr("Browse files"); iconName:"panel-file-open"; onClicked:root.pick("File") }
            IconAction { actionName:qsTr("Recent files"); iconName:"recent-documents"; enabled:root.recent.length>0; onClicked:recentMenu.openFor(this,0,height) }
        }
        Parameter { field:"replaceFile"; title:qsTr("Selected file"); type:"text"; readOnly:true; value:root.value(field,qsTr("No file selected")) }
    }
    EditorBackgroundSection {
        title:qsTr("Transform & blend")
        LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width:parent.width; height:18; style:body; color:LV.Theme.bodyColor; text:qsTr("Image fit") }
        Row {
            spacing:8
            Repeater { model:[{name:"Fill",icon:"crop"},{name:"Fit",icon:"fit-canvas"},{name:"Stretch",icon:"scale"}]; IconAction { id: selectionButton2; required property var modelData; objectName:"background-fit-"+modelData.name; actionName:modelData.name; iconName:modelData.icon; tone: LV.AbstractButton.Borderless; onClicked:root.edit("replaceFit",modelData.name)
                Accessible.checkable: selectionButton2.checkable
                Accessible.checked: selectionButton2.checked
                EditorButtonState { button: selectionButton2; selected: Boolean(root.value("replaceFit","Fill")===modelData.name) }
                Accessible.onToggleAction: selectionButton2.click()
            } }
        }
        Row {
            width:parent.width; height:22; spacing:8
            Parameter { field:"replaceScale"; title:qsTr("Scale"); type:"number"; unit:"%"; minimum:1; maximum:10000; width:(root.width-8)*198/390; controlWidth:width-48; value:root.value(field,100); onEdited:next=>root.edit(field,next) }
            Parameter { field:"replaceAngle"; title:qsTr("Angle"); type:"number"; unit:"°"; minimum:-360; maximum:360; width:(root.width-8)*192/390; controlWidth:width-48; value:root.value(field,0); onEdited:next=>root.edit(field,next) }
        }
        Row {
            width:parent.width; height:22; spacing:8
            Parameter { field:"replaceX"; title:"X"; type:"number"; unit:"px"; minimum:-65536; maximum:65536; width:(root.width-8)*191/390; controlWidth:width-24; value:root.value(field,0); onEdited:next=>root.edit(field,next) }
            Parameter { field:"replaceY"; title:"Y"; type:"number"; unit:"px"; minimum:-65536; maximum:65536; width:(root.width-8)*199/390; controlWidth:width-24; value:root.value(field,0); onEdited:next=>root.edit(field,next) }
        }
        Parameter { field:"replaceBlend"; title:qsTr("Blend mode"); value:root.value(field,"Normal"); options:["Normal","Multiply","Screen","Overlay","Soft Light","Darken","Lighten"]; onEdited:next=>root.edit(field,next) }
        Parameter { field:"replaceOpacity"; title:qsTr("Opacity"); type:"slider"; value:root.value(field,100); onEdited:next=>root.edit(field,next) }
        Parameter { field:"replaceMatch"; title:qsTr("Subject match"); type:"slider"; value:root.value(field,84); onEdited:next=>root.edit(field,next) }
        Parameter { field:"replacePreserve"; title:qsTr("Preserve subject"); type:"toggle"; value:root.value(field,true); onEdited:next=>root.edit(field,next) }
        Parameter { field:"replaceLighting"; title:qsTr("Match lighting"); type:"toggle"; value:root.value(field,true); onEdited:next=>root.edit(field,next) }
    }
    EditorBackgroundSection {
        title:qsTr("Output")
        Parameter { field:"replaceResult"; title:qsTr("Result"); value:root.value(field,"New editable layer"); options:["New editable layer","Replace background layer"]; onEdited:next=>root.edit(field,next) }
        Row {
            spacing:8
            IconAction { objectName:"background-applyReplacement"; actionName:qsTr("Apply replacement"); iconName:"generalcheckmark"; tone:LV.AbstractButton.Default; enabled:Boolean(root.engine)&&root.engine.documentReady&&(root.value("replaceSource","Generative")==="Generative"||String(root.value(root.value("replaceSource","Generative")==="Image"?"replaceImage":"replaceFile","")).length>0); onClicked:root.apply() }
            IconAction { actionName:qsTr("Reset"); iconName:"reset"; onClicked:root.reset() }
        }
    }
    LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; visible:root.toolState.notice.length>0; width:parent.width; style:caption; color:LV.Theme.bodyColor; text:root.toolState.notice; wrapMode:Text.WordWrap; sizeToContentHeight:true }
    FileDialog { id:sourceDialog; property string kind:"Image"; title:kind==="Image"?qsTr("Choose image"):qsTr("Choose image or canvas"); nameFilters:kind==="Image"?[qsTr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff *.heic)")]:[qsTr("Images and canvas (*.iisc *.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff *.heic)")]; onAccepted:root.choose(selectedFile,kind) }
    LV.ContextMenu { id:recentMenu; showIconSlot:false; items:root.recent.map(url=>({label:url.split("/").pop()})); onItemTriggered:index=>root.choose(root.recent[index],"File") }
}
