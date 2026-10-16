pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0
import ".." as Shared

Column {
    id: root
    objectName: "backgroundColorBody"
    required property var toolState
    readonly property var values: toolState.values
    property bool dropdownActive: false
    readonly property bool modalActive: dropdownActive || stopSheet.visible || modeMenu.visible
    readonly property var defaultPalette: ["#FFFFFF","#C7CBD1","#7D8491","#2F3440","#101116","#FF5C77","#FFAA5A","#FFD66B","#94D182","#60D3B5","#56C9E9","#668CFF","#8066FF","#B689FF","#E88DD9","#7A4E43","#B49373","#697C60","#4C6B89","#3F345F"]
    readonly property var palette: value("backgroundPalette","Default palette")==="Saved swatches" ? value("backgroundSwatches",[]) : defaultPalette
    readonly property var presets: [{name:"Violet sunrise",colors:["#8066FF","#E88DD9","#FFAA5A"]},{name:"Ocean",colors:["#2464EC","#60D3B5"]},{name:"Warm light",colors:["#FF785A","#FFD66B"]},{name:"Monochrome",colors:["#101116","#FFFFFF"]},{name:"Forest",colors:["#284B45","#A8DA91"]},{name:"Rose",colors:["#813F76","#F6ADC4"]}].concat(value("backgroundSavedPresets",[]))
    readonly property var recent: value("backgroundRecent", ["#8066FF","#FFAA5A","#60D3B5","#101116","#FFFFFF"])
    readonly property var stops: value("backgroundGradientStops", [{position:0,color:"#8066FF"},{position:.5,color:"#E88DD9"},{position:1,color:"#FFAA5A"}])
    property int selectedStop: 0
    property bool syncingColor: false
    component Parameter: EditorBackgroundParameter { onModalActiveChanged: root.dropdownActive=modalActive }
    width: parent.width; spacing: 16
    function value(key, fallback) { return values[key] === undefined ? fallback : values[key] }
    function edit(key, next) { if (JSON.stringify(values[key]) !== JSON.stringify(next)) toolState.setValue(key,next) }
    function selectColor(color) { colorModel.editColor(color) }
    function changeStop(index, field, next) {
        const copy = stops.map(function(stop) { return Object.assign({},stop) })
        copy[index][field] = next
        edit("backgroundGradientStops",copy)
        edit("backgroundFill","Gradient")
    }
    function apply() {
        if (!toolState.engine) return
        toolState.notice = toolState.engine.applyBackgroundFill(values) ? "" : toolState.engine.error
        const next = ["#"+colorModel.hex].concat(recent.filter(function(c) { return c !== "#"+colorModel.hex })).slice(0,10)
        edit("backgroundRecent",next)
    }
    function reset() {
        edit("backgroundColor","#8066FF"); edit("backgroundAlpha",100); edit("backgroundOpacity",100)
        edit("backgroundPickerMode",0); edit("backgroundFill","Solid"); edit("backgroundExtend",true)
        edit("backgroundGradientStops",[{position:0,color:"#8066FF"},{position:.5,color:"#E88DD9"},{position:1,color:"#FFAA5A"}])
        edit("backgroundGradientName","Violet sunrise");edit("backgroundGradientType","Linear");edit("backgroundInterpolation","Perceptual");edit("backgroundAngle",90)
        selectedStop=0;toolState.notice=""
    }
    ColorManagerModel {
        id: colorModel
        objectName: "backgroundColorModel"
        mode: Number(root.value("backgroundPickerMode",0))
        onColorEdited: function(color) {
            if(root.syncingColor)return
            root.syncingColor=true
            try {
                root.edit("backgroundColor","#"+hex.toUpperCase())
                root.edit("backgroundAlpha",channelValue(10))
                root.edit("backgroundFill","Solid")
            } finally { root.syncingColor=false }
        }
    }
    function syncColor() {
        if(syncingColor)return
        syncingColor=true
        try {
            const color = String(value("backgroundColor","#8066FF"))
            if ("#"+colorModel.hex.toUpperCase() !== color.toUpperCase()) colorModel.beginColor(color)
            const alpha = Number(value("backgroundAlpha",100))
            if (Math.abs(colorModel.channelValue(10)-alpha)>.01) colorModel.editChannel(10,alpha)
        } finally { syncingColor=false }
    }
    onValuesChanged: syncColor()
    Component.onCompleted: syncColor()
    component ChannelInput: LV.InputField {
        id: entry
        required property int channel
        property bool dirty: false
        readonly property var info: { const revision=colorModel.revision; return colorModel.channel(channel) }
        readonly property string valueText: String(Math.round(Number(info.value))) + (channel>=6&&channel<=9?"":info.unit)
        height: 22; fieldMinHeight: 22; clearButtonVisible: false
        Binding { target:entry.inputItem; property:"font.weight"; value:Font.Normal }
        Binding { target:entry.inputItem; property:"font.styleName"; value:"Regular" }
        onValueTextChanged: if (!focused || !dirty) text=valueText
        Component.onCompleted: text=valueText
        function commit() { if(dirty) { dirty=false; const next=Number(text.replace(/[°%]/g,"")); if(text.trim().length&&isFinite(next)) colorModel.editChannel(channel,next) } text=valueText }
        onTextEdited: dirty=true
        onAccepted: { commit(); root.forceActiveFocus() }
        onFocusedChanged: if(!focused&&dirty) commit()
    }
    component ChannelGroup: Row {
        required property string title
        required property var channels
        width: root.width; height: 22; spacing: 8
        LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width: 44; height: 22; style: body; color: LV.Theme.titleHeaderColor; text: parent.title; verticalAlignment: Text.AlignVCenter }
        Repeater {
            model: parent.channels
            Row {
                required property int modelData
                width: (root.width-44-parent.channels.length*8)/parent.channels.length; height: 22; spacing: 4
                LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width:12; height:22; style:caption; color:LV.Theme.bodyColor; text:colorModel.channel(parent.modelData).label; verticalAlignment:Text.AlignVCenter }
                ChannelInput { objectName:"backgroundChannel-"+parent.modelData; channel:parent.modelData; width:parent.width-16; Accessible.name:colorModel.channel(channel).name }
            }
        }
    }
    EditorBackgroundSection {
        title: qsTr("Color picker")
        accessoryWidth: colorModel.mode===0?61:Math.min(220,root.width-90)
        accessory: Component {
            LV.ComboBox {
                objectName: "background-pickerMode"
                text: colorModel.mode===0?"HSB":colorModel.modeName
                tone:LV.AbstractButton.Primary; arrow:LV.Stepper.UpDown
                Accessible.name:qsTr("Color picker mode")
                onClicked: modeMenu.openFor(this,width-modeMenu.implicitWidth,height)
                Keys.onSpacePressed: modeMenu.openFor(this,width-modeMenu.implicitWidth,height)
                Keys.onReturnPressed: modeMenu.openFor(this,width-modeMenu.implicitWidth,height)
            }
        }
        Item {
            visible:colorModel.mode<=2
            objectName:"backgroundColorPlane"; width:parent.width; height:156
            ColorManagerSurface { x:colorModel.mode===2?(parent.width-60-height)/2:0; width:colorModel.mode===2?height:parent.width-60; height:parent.height; model:colorModel; field:colorModel.mode===1?ColorManagerSurface.BrightnessCube:colorModel.mode===2?ColorManagerSurface.ColorWheel:ColorManagerSurface.HueCube }
            ColorManagerSurface { x:parent.width-52; width:22; height:parent.height; rotation:colorModel.mode===0?180:0; model:colorModel; field:colorModel.mode===0?ColorManagerSurface.HueRail:ColorManagerSurface.BrightnessRail; showMarker:false }
            Item {
                x:parent.width-22; width:22; height:parent.height
                ColorManagerSurface { anchors.centerIn:parent; width:parent.height; height:parent.width; rotation:-90; model:colorModel; field:ColorManagerSurface.ChannelRamp; channel:10; checkerboard:false; showMarker:false }
            }
        }
        Column {
            visible:colorModel.mode>2; width:parent.width; spacing:8
            Repeater {
                model:colorModel.channels
                Row {
                    required property var modelData
                    width:root.width; height:22; spacing:8
                    LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width:12; height:22; style:caption; color:LV.Theme.bodyColor; text:colorModel.channel(parent.modelData).label; verticalAlignment:Text.AlignVCenter }
                    ColorManagerSurface { objectName:"backgroundModeChannel-"+parent.modelData; width:parent.width-104; height:22; model:colorModel; field:ColorManagerSurface.ChannelRamp; channel:parent.modelData }
                    ChannelInput { width:76; channel:parent.modelData }
                }
            }
        }
        Row {
            width:parent.width; height:22; spacing:8
            LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width:Math.min(40,root.width*.1); height:22; style:body; color:LV.Theme.titleHeaderColor; text:qsTr("Hex"); verticalAlignment:Text.AlignVCenter }
            LV.InputField {
                id:hexInput
                objectName:"background-hex"; width:root.width*188/398; height:22; fieldMinHeight:22; clearButtonVisible:false
                property bool dirty:false
                Binding { target:hexInput.inputItem; property:"font.weight"; value:Font.Normal }
                Binding { target:hexInput.inputItem; property:"font.styleName"; value:"Regular" }
                readonly property string valueText: { const revision=colorModel.revision; return "#"+colorModel.hex.toUpperCase() }
                onValueTextChanged: if(!focused||!dirty) text=valueText
                Component.onCompleted:text=valueText
                function commit(){if(dirty){dirty=false;colorModel.editHex(text)}text=valueText}
                onTextEdited:dirty=true
                onAccepted:{commit();root.forceActiveFocus()}
                onFocusedChanged:if(!focused&&dirty)commit()
            }
            LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width:Math.min(40,root.width*.1); height:22; style:body; color:LV.Theme.bodyColor; text:qsTr("Alpha"); verticalAlignment:Text.AlignVCenter }
            ChannelInput { objectName:"background-alpha"; width:parent.width-24-2*Math.min(40,root.width*.1)-root.width*188/398; channel:10 }
        }
        ChannelGroup { title:"RGB"; channels:[3,4,5] }
        ChannelGroup { title:"HSB"; channels:[0,1,2] }
        ChannelGroup { title:"Lab"; channels:[12,13,14] }
        ChannelGroup { title:"CMYK"; channels:[6,7,8,9] }
        Parameter { field:"backgroundOpacity"; title:qsTr("Opacity"); type:"slider"; value:root.value(field,100); onEdited:next=>root.edit(field,next) }
        Parameter { field:"backgroundExtend"; title:qsTr("Extend fill to bleed"); type:"toggle"; value:root.value(field,true); onEdited:next=>root.edit(field,next) }
        LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width:parent.width; height:18; style:caption; color:LV.Theme.bodyColor; text:qsTr("Foreground · %1").arg(root.value("backgroundColor","#8066FF")) }
    }
    EditorBackgroundSection {
        title:qsTr("Swatches")
        Row {
            width:parent.width; height:22; spacing:8
            Parameter { field:"backgroundPalette"; width:root.width*236/398; value:root.value(field,"Default palette"); options:["Default palette","Saved swatches"]; onEdited:next=>root.edit(field,next) }
            LV.LabelButton { objectName:"background-addSwatch"; text:qsTr("Add swatch"); tone:LV.AbstractButton.Borderless; onClicked:{const color="#"+colorModel.hex.toUpperCase();const saved=root.value("backgroundSwatches",[]);if(saved.indexOf(color)<0)root.edit("backgroundSwatches",saved.concat([color]));root.edit("backgroundPalette","Saved swatches")} }
        }
        Flow {
            width:parent.width; spacing:8
            Repeater {
                model:root.palette
                LV.AbstractButton { required property color modelData; width:32; height:28; Accessible.name:qsTr("Swatch %1").arg(modelData); background:Rectangle { radius:4; color:parent.modelData; border.width:String(parent.modelData).toLowerCase()===String(root.value("backgroundColor","#8066FF")).toLowerCase()?2:0; border.color:LV.Theme.primary } onClicked:root.selectColor(modelData) }
            }
        }
        LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width:parent.width; height:18; style:caption; color:LV.Theme.bodyColor; text:qsTr("Recent colors") }
        Flow { width:parent.width; spacing:8; Repeater { model:root.recent; LV.AbstractButton { required property color modelData; width:32; height:22; background:Rectangle { radius:4; color:parent.modelData } Accessible.name:qsTr("Recent color %1").arg(modelData); onClicked:root.selectColor(modelData) } } }
    }
    EditorBackgroundSection {
        title:qsTr("Gradient editor")
        LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width:parent.width; height:18; style:caption; color:LV.Theme.bodyColor; text:root.value("backgroundGradientName","Violet sunrise") }
        EditorBackgroundGradient { objectName:"backgroundGradientPreview"; width:parent.width; height:32; stops:root.stops; angle:root.value("backgroundAngle",90); radial:root.value("backgroundGradientType","Linear")==="Radial" }
        Flow {
            width:parent.width; spacing:8
            Repeater {
                model:root.stops
                Row {
                    id: stopRow
                    required property var modelData
                    required property int index
                    width:(root.width-16)/3; height:22; spacing:4
                    LV.AbstractButton { width:24; height:22; background:Rectangle { radius:4; color:stopRow.modelData.color } onClicked:{root.selectedStop=stopRow.index;stopSheet.open();stopSheet.loadedContent.setColor(stopRow.modelData.color)} }
                    Parameter { field:"gradientStop"+parent.index; type:"number"; unit:"%"; minimum:0; maximum:100; width:parent.width-28; value:Math.round(parent.modelData.position*100); onEdited:next=>root.changeStop(parent.index,"position",next/100) }
                }
            }
        }
        Row {
            width:parent.width; height:22; spacing:8
            Parameter { field:"backgroundGradientType"; width:root.width*150/398; value:root.value(field,"Linear"); options:["Linear","Radial"]; onEdited:next=>{root.edit(field,next);root.edit("backgroundFill","Gradient")} }
            Parameter { field:"backgroundInterpolation"; width:root.width*150/398; value:root.value(field,"Perceptual"); options:["Perceptual","Linear"]; onEdited:next=>root.edit(field,next) }
            Parameter { field:"backgroundAngle"; type:"number"; unit:"°"; minimum:-360; maximum:360; width:parent.width-16-2*root.width*150/398; value:root.value(field,90); onEdited:next=>root.edit(field,next) }
        }
        Row {
            spacing:8
            LV.LabelButton { objectName:"background-addStop"; text:qsTr("Add stop"); tone:LV.AbstractButton.Default; enabled:root.stops.length<32; onClicked:{root.edit("backgroundGradientStops",root.stops.concat([{position:.5,color:"#"+colorModel.hex}]).sort((a,b)=>a.position-b.position));root.edit("backgroundFill","Gradient")} }
            LV.LabelButton { text:qsTr("Remove stop"); tone:LV.AbstractButton.Borderless; enabled:root.stops.length>2; onClicked:root.edit("backgroundGradientStops",root.stops.filter((s,i)=>i!==Math.min(root.selectedStop,root.stops.length-1))) }
            LV.LabelButton { text:qsTr("Reverse"); tone:LV.AbstractButton.Borderless; onClicked:root.edit("backgroundGradientStops",root.stops.map(s=>({position:1-s.position,color:s.color})).reverse()) }
        }
        LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; width:parent.width; height:18; style:caption; color:LV.Theme.bodyColor; text:qsTr("Gradient presets") }
        Grid {
            width:parent.width; columns:3; columnSpacing:8; rowSpacing:8
            Repeater {
                model:root.presets
                LV.AbstractButton { required property var modelData; width:(root.width-16)/3; height:26; Accessible.name:modelData.name; background:EditorBackgroundGradient { stops:parent.modelData.stops || parent.modelData.colors.map((c,i)=>({position:i/(parent.modelData.colors.length-1),color:c})) } onClicked:{root.edit("backgroundGradientStops",modelData.stops || modelData.colors.map((c,i)=>({position:i/(modelData.colors.length-1),color:c})));root.edit("backgroundGradientName",modelData.name);root.edit("backgroundFill","Gradient")} }
            }
        }
    }
    Row {
        spacing:8
        LV.LabelButton { objectName:"background-applyColor"; text:qsTr("Apply color"); tone:LV.AbstractButton.Default; enabled:Boolean(root.toolState.engine)&&root.toolState.engine.documentReady; onClicked:root.apply() }
        LV.LabelButton { text:qsTr("Save preset"); tone:LV.AbstractButton.Borderless; onClicked:root.edit("backgroundSavedPresets",root.value("backgroundSavedPresets",[]).concat([{name:root.value("backgroundGradientName","Gradient"),stops:root.stops}])) }
    }
    LV.Label { font.weight:Font.Normal; font.styleName:"Regular"; visible:root.toolState.notice.length>0; width:parent.width; style:caption; color:LV.Theme.bodyColor; text:root.toolState.notice; wrapMode:Text.WordWrap; sizeToContentHeight:true }
    LV.ContextMenu { id:modeMenu; objectName:"backgroundPickerMenu"; showIconSlot:false; items:colorModel.modeNames.map(name=>({label:name})); onItemTriggered:index=>root.edit("backgroundPickerMode",index) }
    LV.Sheet { id:stopSheet; parent:root.Window.window?root.Window.window.contentItem:null; title:qsTr("Gradient stop"); presentation:LV.Sheet.Mobile; topSafeInset:0; bottomSafeInset:0; contentComponent:Shared.ColorPicker { onAccepted:color=>{root.changeStop(root.selectedStop,"color",String(color));stopSheet.close()} onCanceled:stopSheet.close() } }
}
