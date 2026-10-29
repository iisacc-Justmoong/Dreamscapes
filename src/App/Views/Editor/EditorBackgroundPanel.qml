pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import ".." as Frames
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import ".." as Shared

Column {
    id: root
    objectName: "editorBackgroundPanel"
    required property var toolState
    NativeBackgroundModel {
        id: nativeBackground
        toolState: root.toolState.nativeModel
    }
    readonly property var backend: toolState.engine
    readonly property var info: backend ? backend.maskPanelInfo : ({canEdit:false,masks:[],selected:{},targets:[],reason:qsTr("Open a canvas first.")})
    readonly property var selected: info.selected || {}
    readonly property bool hasMask: Boolean(selected.id)
    readonly property var values: toolState.values
    readonly property string currentView: values.backgroundView || "Color"
    readonly property bool modalActive: options.visible || colorSheet.visible || renameSheet.visible || (viewLoader.item ? viewLoader.item.modalActive : false)
    readonly property var views: ["Color","Remove","Mask","Replace"]
    spacing: 12
    width: 398
    // Preserve focus and menu anchors across property edits and canvas previews.
    NativeSnapshotListModel {
        id: masksModel
        objectName: "backgroundNativeMasks"
        keyRole: "maskId"
        fieldMapping: ({maskId: "id", maskName: "name", maskVisible: "visible", maskSelected: "selected"})
        entries: root.info.masks || []
    }
    function value(field, fallback) { return values[field] === undefined ? fallback : values[field] }
    function edit(field, next) { if(value(field) !== next) toolState.setValue(field,next) }
    function action(name) { nativeBackground.maskAction(name) }
    function propertyEdit(field, next) { nativeBackground.maskProperty(field, next) }
    function collectionAction(name) { nativeBackground.maskCollectionAction(name) }
    function resetMask() { nativeBackground.resetMask() }
    function openOptions(control, kind, entries, maskId) {
        options.kind=kind;options.entries=entries;options.maskId=maskId || ""
        options.openFor(control,Math.min(0,root.width-control.x-options.implicitWidth),control.height)
    }
    component Heading: LV.Label {
        style: body; font.weight: Font.Normal; font.styleName: "Regular"; color: LV.Theme.bodyColor; height: 18
        width: parent.width; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
    }
    component Caption: LV.Label {
        style: caption; color: LV.Theme.bodyColor; width: parent.width
        wrapMode: Text.WordWrap; sizeToContentHeight: true
    }
    component SectionTitle: Column {
        required property string title
        width: parent.width; spacing: 8
        Rectangle { width: parent.width; height: 1; color: LV.Theme.surfacePanel }
        Heading { text: parent.title; color: LV.Theme.titleHeaderColor; font.weight: Font.Medium; font.styleName: "Medium" }
    }
    component NumberInput: LV.InputField {
        id: input
        required property real amount
        required property real maximum
        property string unit: "%"
        property real minimum: 0
        property string valueText: String(Math.round(amount*10)/10) + (unit==="px"?" px":unit)
        property bool edited: false
        signal committed(real number)
        height: 22; fieldMinHeight: 22; clearButtonVisible: false
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        function commit() {
            if(edited) {
                edited=false
                const number=NativeData.tools.numberInput(text,minimum,maximum,unit)
                if(number!==null) committed(number)
            }
            text=valueText
        }
        onValueTextChanged: if(!focused || !edited) text=valueText
        onTextEdited: edited=true
        onAccepted: { commit(); root.forceActiveFocus() }
        onFocusedChanged: if(!focused && edited) commit()
        Component.onCompleted: text=valueText
    }
    component SliderRow: Row {
        id: control
        required property string title
        required property string field
        required property real amount
        property real minimum: 0
        property real maximum: 100
        property string unit: "%"
        property bool maskProperty: false
        property real sliderMaximum: maximum
        width: parent.width; height: 30; spacing: 8
        function commit(number) { if(maskProperty) root.propertyEdit(field,number); else root.edit(field,number) }
        Heading { id: parameterLabel; width: Math.min(80,root.width*.25); height: 30; text: control.title }
        LV.Slider {
            objectName: "maskSlider-"+control.field
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width-parameterLabel.width-number.width-16
            size: LV.Slider.Mini; showMinMax: false; showLabels: false; showSymbol: false
            from: control.minimum; to: control.sliderMaximum; stepSize: control.field==="feather"?.1:1
            value: control.amount
            Accessible.name: control.title
            onMoved: control.commit(value)
        }
        NumberInput {
            id: number
            objectName: "maskNumber-"+control.field
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(100,root.width*.307)
            amount: control.amount; maximum: control.maximum; minimum: control.minimum; unit: control.unit
            Accessible.name: control.title
            onCommitted: function(value){ control.commit(value) }
        }
    }
    component MaskTooltip: LV.Tooltip {
        automatic: false
        closePolicy: Controls.Popup.NoAutoClose
        anchorPoint: Qt.point(target?target.width/2:0,0)
        preferredPlacement: LV.Tooltip.Above
    }
    component ToggleRow: Item {
        id: control
        required property string title
        required property string field
        required property string iconName
        property bool initial: true
        width: 64; height: 22
        Image { width: 18; height: 18; anchors.verticalCenter: parent.verticalCenter; source: LV.Theme.iconPath(control.iconName); sourceSize: Qt.size(36,36) }
        LV.ToggleSwitch {
            id: toggle
            objectName: "maskToggle-"+control.field
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            trackWidth: 38; trackHeight: 22
            checked: root.value(control.field,control.initial)
            Accessible.name: control.title
            onToggled: root.edit(control.field,checked)
        }
        HoverHandler { id: toggleHover }
        MaskTooltip { target: control; visible: toggleHover.hovered || toggle.visualFocus; text: control.title }
    }
    component MaskIcon: LV.IconButton {
        id: control
        required property string label
        property string helpText: label
        width: 30; height: 30; iconSize: 18
        horizontalPadding: Math.max(0,(width-iconSize)/2); verticalPadding: 6; spacing: 0
        text: ""; tone: LV.AbstractButton.Borderless; activeFocusOnTab: true
        Accessible.name: label; Accessible.description: helpText
        MaskTooltip { objectName: control.objectName+"-tooltip"; target: control; visible: control.hovered || control.visualFocus; text: control.helpText }
    }
    component ActionButton: MaskIcon {
        required property string actionId
        tone: LV.AbstractButton.Borderless
        enabled: root.info.canEdit
        objectName: "maskAction-"+actionId
        onClicked: root.action(actionId)
    }
    Item {
        width: parent.width; height: root.currentView === "Mask" ? 22 : 30
        LV.IconSegmentedControl {
            objectName: "backgroundNavigation"
            forceBorderlessTone: false
            Repeater {
                model: root.views
                LV.IconButton {
                    id: selectionButton1
                    required property string modelData
                    objectName: "backgroundTab-"+modelData
                    width: 22; height: 22; iconSize: 18
                    iconSource: Qt.resolvedUrl("Assets/Background/"+modelData.toLowerCase()+".svg")
                    tone: LV.AbstractButton.Borderless
                    Accessible.name: modelData; Accessible.checkable: true; Accessible.checked: root.currentView===modelData
                    onClicked: { root.toolState.notice=""; root.edit("backgroundView",modelData) }

                    EditorButtonState { button: selectionButton1; selected: Boolean(root.currentView===modelData) }
                    Accessible.onToggleAction: selectionButton1.click()
                }
            }
        }
        LV.IconButton {
            objectName: "backgroundReset"
            anchors.right: parent.right; width: 22; height: 22; iconSize: 18; iconName: "reset"
            tone: LV.AbstractButton.Borderless; Accessible.name: qsTr("Reset %1 settings").arg(root.currentView)
            onClicked: { if(root.currentView==="Mask") root.resetMask(); else if(viewLoader.item) viewLoader.item.reset() }
        }
    }
    Row {
        width: parent.width; height: root.currentView==="Mask"?22:20; spacing: 8
        Image { width: 18; height: 18; source: Qt.resolvedUrl("Assets/Background/"+root.currentView.toLowerCase()+".svg"); sourceSize: Qt.size(36,36) }
        Heading { width: parent.width-26; text: root.currentView; height:parent.height; font.pixelSize:root.currentView==="Mask"?13:15; font.weight:root.currentView==="Mask"?Font.Normal:Font.Medium; font.styleName:root.currentView==="Mask"?"Regular":"Medium" }
    }
    Column {
        objectName: "backgroundMaskBody"
        visible: root.currentView==="Mask"
        width: parent.width; spacing: 16
        Column {
            width: parent.width; spacing: 8
            Row {
                width: parent.width; height: 22; spacing: 8
                Image { width: 18; height: 18; anchors.verticalCenter: parent.verticalCenter; source: Qt.resolvedUrl("Assets/Background/mask.svg"); sourceSize: Qt.size(36,36) }
                LV.ComboBox {
                    id: target
                    objectName: "maskTarget"
                    width: parent.width-26; height: 22; arrow: LV.Stepper.UpDown; tone: LV.AbstractButton.Primary
                    text: root.value("maskTarget","Selected canvas layer")==="Selected canvas layer"?qsTr("Selected canvas layer"):NativeData.presentation.find(root.info.targets || [], root.info.targetId, "id").name || qsTr("Selected canvas layer")
                    enabled: Boolean(root.backend) && (root.info.targets || []).length>0
                    activeFocusOnTab: true; Accessible.name: qsTr("Mask target: ")+text
                    onClicked: root.openOptions(target,"target",NativeData.presentation.choices(root.info.targets || [], "name", "id", "id"))
                    Keys.onReturnPressed: clicked(); Keys.onSpacePressed: clicked()
                }
            }
            Row {
                width: parent.width; spacing: 8
                Repeater {
                    model: ["Brush","Eraser","Lasso","Polygon","Rectangle","Ellipse","Gradient","Color range"]
                    MaskIcon {
                        id: selectionButton2
                        required property string modelData
                        objectName: "maskTool-"+modelData
                        width: (parent.width-56)/8
                        label: modelData
                        iconName: ({Brush:"brush",Eraser:"eraser",Lasso:"panel-select-lasso",Polygon:"panel-select-polygon",Rectangle:"panel-select-rectangle",Ellipse:"ellipse-marquee",Gradient:"panel-fill-gradient","Color range":"select-by-color"})[modelData]
                        tone: LV.AbstractButton.Borderless
                        enabled: root.info.canEdit && (modelData!=="Eraser" || root.hasMask)
                        Accessible.checkable: true; Accessible.checked: root.value("maskTool","Brush")===modelData
                        onClicked: root.edit("maskTool",modelData)

                        EditorButtonState { button: selectionButton2; selected: Boolean(root.value("maskTool","Brush")===modelData) }
                        Accessible.onToggleAction: selectionButton2.click()
                    }
                }
            }
            Item {
                width: parent.width; height: width<258?68:30
                Row {
                    spacing: 8
                    Repeater {
                        model: ["Add","Subtract","Intersect","Replace"]
                        MaskIcon {
                            id: selectionButton3
                            required property string modelData
                            objectName: "maskMode-"+modelData
                            label: modelData; helpText: qsTr("Selection mode: %1").arg(modelData)
                            iconName: ({Add:"add-selection",Subtract:"subtract-selection",Intersect:"intersect-selection",Replace:"selection-border"})[modelData]
                            tone: LV.AbstractButton.Borderless
                            enabled: root.info.canEdit; Accessible.checkable: true; Accessible.checked: root.value("maskMode","Add")===modelData
                            onClicked: root.edit("maskMode",modelData)

                            EditorButtonState { button: selectionButton3; selected: Boolean(root.value("maskMode","Add")===modelData) }
                            Accessible.onToggleAction: selectionButton3.click()
                        }
                    }
                }
                Row {
                    anchors.right: parent.right; y: parent.width<258?38:0; spacing: 8
                    Item {
                        id: subjectHint
                        width: 30; height: 30
                        HoverHandler { id: subjectHover }
                        ActionButton { label: qsTr("Select subject"); helpText: enabled?label:label+" · "+(root.info.semanticReason || ""); iconName: "subject-select"; actionId: "subject"; enabled: root.info.canSelectSubject || false }
                        MaskTooltip { target: subjectHint; visible: !root.info.canSelectSubject && subjectHover.hovered; text: root.info.semanticReason || "" }
                    }
                    Item {
                        id: skyHint
                        width: 30; height: 30
                        HoverHandler { id: skyHover }
                        ActionButton { label: qsTr("Select sky"); helpText: enabled?label:label+" · "+(root.info.semanticReason || ""); iconName: "select-sky"; actionId: "sky"; enabled: root.info.canSelectSky || false }
                        MaskTooltip { target: skyHint; visible: !root.info.canSelectSky && skyHover.hovered; text: root.info.semanticReason || "" }
                    }
                    ActionButton { label: qsTr("Select all"); iconName: "select-all-toolbar"; actionId: "selectAll" }
                }
            }
            Caption { visible: !root.info.canEdit; text: root.info.reason || "" }
            Caption { visible: Number(root.info.polygonPoints)>0; text: qsTr("%1 points").arg(root.info.polygonPoints); Accessible.description: qsTr("Close path to finish the polygon") }
            SliderRow { visible: root.value("maskTool")==="Color range"; title: qsTr("Fuzziness"); field: "maskFuzziness"; amount: root.value(field,50) }
        }
        Column {
            width: parent.width; spacing: 8; enabled: root.info.canEdit
            SectionTitle { title: qsTr("Brush") }
            SliderRow { title: qsTr("Size"); field: "maskSize"; amount: root.value(field,40); minimum: 1; maximum: 4096; sliderMaximum: 100; unit: "px" }
            SliderRow { title: qsTr("Hardness"); field: "maskHardness"; amount: root.value(field,60) }
            SliderRow { title: qsTr("Flow"); field: "maskFlow"; amount: root.value(field,100) }
            SliderRow { title: qsTr("Opacity"); field: "maskOpacity"; amount: root.value(field,100) }
            Item {
                width: parent.width; height: 30
                ToggleRow { anchors.verticalCenter: parent.verticalCenter; title: qsTr("Use pen pressure"); iconName: "brush-pressure"; field: "maskPressure" }
                Row {
                    anchors.right: parent.right; spacing: 8
                    ActionButton { label: qsTr("Invert selection"); iconName: "invert-selection"; actionId: "invertSelection"; tone: LV.AbstractButton.Default; enabled: root.hasMask }
                    ActionButton { label: qsTr("Clear selection"); iconName: "deselect"; actionId: "clear" }
                    ActionButton { label: qsTr("Close path"); iconName: "close-path"; actionId: "closePath"; enabled: Number(root.info.polygonPoints)>=3 }
                }
            }
        }
        Column {
            width: parent.width; spacing: 8
            Column {
                width: parent.width; spacing: 8
                Rectangle { width: parent.width; height: 1; color: LV.Theme.surfacePanel }
                Row {
                    width: parent.width; height: 18; spacing: 8
                    Heading { width: parent.width-50; text: qsTr("Masks"); color: LV.Theme.titleHeaderColor; font.weight: Font.Medium; font.styleName: "Medium" }
                    Caption { objectName: "maskCount"; width: 42; horizontalAlignment: Text.AlignRight; text: String((root.info.masks || []).length); Accessible.name: qsTr("%1 masks").arg((root.info.masks || []).length) }
                }
            }
            Caption { visible: !root.hasMask; text: qsTr("No masks"); Accessible.description: qsTr("Paint or make a selection to create a mask.") }
            Repeater {
                model: masksModel
                Row {
                    id: maskRow
                    required property string maskId
                    required property string maskName
                    required property bool maskVisible
                    required property bool maskSelected
                    width: root.width; height: 30; spacing: 8
                    LV.LabelButton {
                        id: selectionButton4
                        objectName: "maskRow-"+maskRow.maskId
                        width: parent.width-76; height: 30; text: maskRow.maskName
                        tone: LV.AbstractButton.Borderless
                        enabled: root.info.canEdit; Accessible.checkable: true; Accessible.checked: maskRow.maskSelected
                        onClicked: root.backend.selectBackgroundMask(maskRow.maskId)

                        EditorButtonState { button: selectionButton4; selected: Boolean(maskRow.maskSelected) }
                        Accessible.onToggleAction: selectionButton4.click()
                    }
                    MaskIcon {
                        objectName: "maskVisible-"+maskRow.maskId
                        label: qsTr("Mask visibility: %1").arg(maskRow.maskName)
                        helpText: (maskRow.maskVisible?qsTr("Hide %1"):qsTr("Show %1")).arg(maskRow.maskName)
                        iconName: maskRow.maskVisible?"visibility":"hide"; enabled: root.info.canEdit
                        Accessible.checkable: true; Accessible.checked: maskRow.maskVisible
                        function toggleVisibility() {const id=maskRow.maskId;const next=!maskRow.maskVisible;root.backend.selectBackgroundMask(id);root.propertyEdit("visible",next)}
                        onClicked: toggleVisibility()
                        onDoubleClicked: toggleVisibility()
                    }
                    MaskIcon {
                        id: more
                        objectName: "maskMore-"+maskRow.maskId
                        label: qsTr("Mask actions: %1").arg(maskRow.maskName); iconName: "generalmoreHorizontal"; enabled: root.info.canEdit
                        onClicked: root.openOptions(more,"more",[{id:"rename",label:qsTr("Rename")},{id:"duplicate",label:qsTr("Duplicate")},{id:"invert",label:qsTr("Invert mask")},{id:"bypass",label:qsTr("Bypass all masks")},{id:"delete",label:qsTr("Delete")}],maskRow.maskId)
                    }
                }
            }
            Item {
                width: parent.width; height: 30
                Row {
                    spacing: 8
                    MaskIcon { objectName: "maskNew"; label: qsTr("New mask"); iconName: "new-layer"; tone: LV.AbstractButton.Default; enabled: root.info.canEdit; onClicked: root.collectionAction("new") }
                    MaskIcon { objectName: "maskDuplicate"; label: qsTr("Duplicate mask"); iconName: "duplicate-layer"; tone: LV.AbstractButton.Default; enabled: root.hasMask && root.info.canEdit; onClicked: root.collectionAction("duplicate") }
                    MaskIcon { objectName: "maskDelete"; label: qsTr("Delete mask"); iconName: "delete-layer"; enabled: root.hasMask && root.info.canEdit; onClicked: root.collectionAction("delete") }
                }
                ToggleRow { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; title: qsTr("New mask for each completed selection"); iconName: "new-layer"; field: "maskNewEach" }
            }
            Caption { visible: root.hasMask && root.selected.empty; text: qsTr("Empty mask"); Accessible.description: qsTr("Paint or select areas to reveal them before Apply.") }
        }
        Column {
            width: parent.width; spacing: 8
            SectionTitle { title: qsTr("Properties") }
            Row {
                width: parent.width; spacing: 8; height: 22; enabled: root.hasMask && root.info.canEdit
                Heading { width: parent.width-combine.width-8; text: qsTr("Combine") }
                LV.ComboBox {
                    id: combine
                    objectName: "maskCombine"
                    width: parent.width*230/398; height: 22; arrow: LV.Stepper.UpDown; tone: LV.AbstractButton.Primary
                    text: root.selected.combine==="Add"?qsTr("Add / union"):root.selected.combine || qsTr("Add / union")
                    activeFocusOnTab: true; Accessible.name: qsTr("Combine with: ")+text
                    onClicked: root.openOptions(combine,"combine",[{id:"Add",label:qsTr("Add / union")},{id:"Subtract",label:qsTr("Subtract")},{id:"Intersect",label:qsTr("Intersect")},{id:"Replace",label:qsTr("Replace")}])
                    Keys.onReturnPressed: clicked(); Keys.onSpacePressed: clicked()
                }
            }
            SliderRow { enabled: root.hasMask && root.info.canEdit; title: qsTr("Feather"); field: "feather"; amount: root.selected.feather===undefined?2:root.selected.feather; maximum: 128; sliderMaximum: 10; unit: "px"; maskProperty: true }
            SliderRow { enabled: root.hasMask && root.info.canEdit; title: qsTr("Density"); field: "density"; amount: root.selected.density===undefined?100:root.selected.density; maskProperty: true }
            Row {
                width: parent.width; height: 30; spacing: 8
                ToggleRow { anchors.verticalCenter: parent.verticalCenter; title: qsTr("Show overlay on canvas"); iconName: "quick-mask"; field: "maskOverlay" }
                Item { width: parent.width-202; height: 30 }
                LV.AbstractButton {
                    id: overlayColor
                    objectName: "maskOverlayColor"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24; height: 22; tone: LV.AbstractButton.Borderless; Accessible.name: qsTr("Mask overlay color")
                    background: Rectangle { radius: 4; color: root.value("maskOverlayColor","#FF5C77") }
                    onClicked: { colorSheet.open(); colorSheet.loadedContent.setColor(root.value("maskOverlayColor","#FF5C77")) }
                    MaskTooltip { target: overlayColor; visible: overlayColor.hovered || overlayColor.visualFocus; text: qsTr("Mask overlay color") }
                }
                NumberInput { objectName: "maskNumber-maskOverlayOpacity"; anchors.verticalCenter: parent.verticalCenter; width: 90; amount: root.value("maskOverlayOpacity",50); maximum: 100; Accessible.name: qsTr("Overlay opacity"); onCommitted: function(number){root.edit("maskOverlayOpacity",number)} }
            }
        }
        Flow {
            width: parent.width; spacing: 8
            ActionButton { label: qsTr("Apply mask"); iconSource: Qt.resolvedUrl("Assets/Background/mask-apply.svg"); actionId: "apply"; tone: LV.AbstractButton.Default; enabled: root.hasMask && root.info.canEdit }
            ActionButton { label: qsTr("Invert mask"); iconName: "mask-invert"; actionId: "invert"; enabled: root.hasMask && root.info.canEdit }
            ActionButton { label: qsTr("Reset mask"); iconName: "reset"; actionId: "reset"; enabled: root.hasMask && root.info.canEdit }
        }
        Caption { visible: root.info.applied || false; text: qsTr("Applied"); Accessible.description: qsTr("Original pixels are preserved") }
        Caption { visible: root.toolState.notice.length>0 || (root.backend && root.backend.error.length>0); text: root.toolState.notice || (root.backend ? root.backend.error : "") }
    }
    Loader {
        id: viewLoader
        visible: root.currentView!=="Mask"
        active: visible
        width: parent.width; height: item?item.implicitHeight:0
        sourceComponent: root.currentView==="Color" ? colorView : root.currentView==="Remove" ? removeView : replaceView
    }
    Component { id: colorView; EditorBackgroundColorPanel { toolState: root.toolState } }
    Component { id: removeView; EditorBackgroundRemovePanel { toolState: root.toolState } }
    Component { id: replaceView; EditorBackgroundReplacePanel { toolState: root.toolState } }
    LV.ContextMenu {
        id: options
        objectName: "maskOptions"
        parent: root.Window.window ? root.Window.window.contentItem : null
        property string kind: "target"
        property string maskId: ""
        property var entries: []
        showIconSlot: false
        items: NativeData.presentation.labels(entries)
        onItemTriggered: function(index) {
            if(!entries[index] || !root.backend) return
            const id=entries[index].id
            if(kind==="target") root.edit("maskTarget",id)
            else if(kind==="combine") root.propertyEdit("combine",id)
            else {
                const selectedId=maskId
                options.close()
                // Complete the menu's close before changing its anchor row or
                // opening a sheet; collection edits replace the row delegates.
                Qt.callLater(function() {
                    if(!root.backend || !root.backend.selectBackgroundMask(selectedId)) return
                    if(id==="rename") {renameSheet.open();renameSheet.loadedContent.entryText=root.backend.maskPanelInfo.selected.name}
                    else if(id==="duplicate" || id==="delete") root.collectionAction(id)
                    else root.action(id)
                })
            }
        }
    }
    Frames.PanelSheet {
        id: colorSheet
        objectName: "maskColorSheet"
        parent: root.toolState.parent
        presentation: LV.Sheet.Mobile; title: qsTr("Overlay color"); contentPadding: 16; topSafeInset: 0; bottomSafeInset: 0
        contentComponent: Shared.ColorPicker {
            onAccepted: function(color){root.edit("maskOverlayColor",String(color).toUpperCase());colorSheet.close()}
            onCanceled: colorSheet.close()
        }
    }
    Frames.PanelSheet {
        id: renameSheet
        objectName: "maskRenameSheet"
        parent: root.toolState.parent
        presentation: LV.Sheet.Mobile; title: qsTr("Rename mask"); topSafeInset: 0; bottomSafeInset: 0
        contentComponent: Column {
            property alias entryText: renameInput.text
            width: parent.width; spacing: 8
            LV.InputField { id: renameInput; objectName: "maskRenameInput"; width: parent.width; clearButtonVisible: false; Accessible.name: qsTr("Mask name"); onAccepted: function(value) {root.propertyEdit("name",value);if(!root.backend.error.length)renameSheet.close()} }
            LV.LabelButton { text: qsTr("Save"); tone: LV.AbstractButton.Primary; onClicked: {root.propertyEdit("name",renameInput.text);if(!root.backend.error.length)renameSheet.close()} }
        }
    }
}
