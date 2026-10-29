pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import ".." as Frames
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "editorViewBar"
    required property var canvas
    property string openFamily: "snap"
    readonly property bool modalActive: optionsPopup.visible || compositeMenu.visible || channelMenu.visible || presetMenu.visible
    readonly property alias optionsPanel: panelLoader.item
    signal publishRequested()
    implicitHeight: 40
    clip: true
    function showOptions(family, anchor) { openFamily = family; optionsPopup.anchor = anchor; optionsPopup.open() }
    function toggle(section, key) { canvas.viewOptions.edit(section, key, !canvas.viewOptions[section][key]) }
    component ViewToggle: Item {
        id: control
        required property string label
        required property string icon
        required property real faceWidth
        required property string section
        property string key: "enabled"
        property bool detailed: false
        width: faceWidth + (detailed ? 23 : 0); height: 24
        LV.LabelButton {
            id: face
            objectName: "editorViewToggle-" + control.section + "-" + control.key
            text: control.label; width: control.faceWidth; height: 24; leftPadding: 28
            tone: LV.AbstractButton.Default
            EditorButtonState { button: face; selected: Boolean(root.canvas.viewOptions[control.section][control.key]) }
            onClicked: root.toggle(control.section, control.key)
            Image { x: 6; y: 4; width: 16; height: 16; sourceSize.width: 16; sourceSize.height: 16; source: "qrc:/editor/view-options/" + control.icon + ".svg" }
        }
        Rectangle { visible: control.detailed; x: control.faceWidth; y: 4; width: 1; height: 16; color: LV.Theme.surfacePanel }
        LV.Stepper {
            objectName: "editorViewOptions-" + control.section
            visible: control.detailed; x: control.faceWidth + 1
            width: 22; height: 24; arrow: LV.Stepper.Down; tone: LV.AbstractButton.Borderless
            Accessible.name: control.label + " options"
            onClicked: root.showOptions(control.section, control)
        }
    }
    Flickable {
        objectName: "editorViewControls"
        x: 8; y: 8; height: 24; width: Math.max(0, root.width - right.width - 24)
        contentWidth: controls.width; contentHeight: 24
        boundsBehavior: Flickable.StopAtBounds; flickableDirection: Flickable.HorizontalFlick; clip: true
        Row {
            id: controls
            height: 24; spacing: 8
            Image { anchors.verticalCenter: parent.verticalCenter; width: 16; height: 16; source: "qrc:/editor/view-options/show.svg"; sourceSize.width: 16; sourceSize.height: 16 }
            LV.Label { anchors.verticalCenter: parent.verticalCenter; text: qsTr("View"); style: body }
            LV.ComboBox { id: composite; objectName: "editorViewComposite"; width: 118; height: 20; anchors.verticalCenter: parent.verticalCenter; tone: LV.ComboBox.Borderless; arrow: LV.Stepper.Down; text: root.canvas.viewOptions.display.composite; onClicked: compositeMenu.openFor(this,0,height) }
            LV.Label { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Channel"); style: caption }
            LV.ComboBox { id: channel; objectName: "editorViewChannel"; width: 80; height: 20; anchors.verticalCenter: parent.verticalCenter; tone: LV.ComboBox.Borderless; arrow: LV.Stepper.Down; text: root.canvas.viewOptions.display.channel; onClicked: channelMenu.openFor(this,0,height) }
            Rectangle { width: 1; height: 16; anchors.verticalCenter: parent.verticalCenter; color: LV.Theme.surfacePanel }
            ViewToggle { label: qsTr("Rulers") + " · " + root.canvas.viewOptions.rulerUnit; icon: "rulers"; faceWidth: 104; section: "display"; key: "rulers" }
            ViewToggle { label: qsTr("Grid"); icon: "grid"; faceWidth: 64; section: "grid"; detailed: true }
            ViewToggle { label: qsTr("Guides"); icon: "guides"; faceWidth: 80; section: "guides"; detailed: true }
            ViewToggle { label: qsTr("Snap"); icon: "snap"; faceWidth: 70; section: "snap"; detailed: true }
            Rectangle { width: 1; height: 16; anchors.verticalCenter: parent.verticalCenter; color: LV.Theme.surfacePanel }
            ViewToggle { label: qsTr("Outlines"); icon: "outlines"; faceWidth: 90; section: "display"; key: "outlines" }
            ViewToggle { label: qsTr("Transparency"); icon: "transparency"; faceWidth: 124; section: "display"; key: "transparency" }
            ViewToggle { label: qsTr("Pixel preview"); icon: "pixel-preview"; faceWidth: 124; section: "display"; key: "pixel-preview" }
        }
    }
    Row {
        id: right
        anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
        spacing: 8
        LV.LabelButton { id: constrain; objectName: "editorViewConstrain"; width: 82; height: 22; text: qsTr("Constrain"); tone: LV.AbstractButton.Default; EditorButtonState { button: constrain; selected: Boolean(root.canvas.viewOptions.display.constrain) } onClicked: root.toggle("display","constrain") }
        LV.ComboBox { id: preset; objectName: "editorViewPreset"; width: 97; height: 20; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Preset"); tone: LV.ComboBox.Borderless; arrow: LV.Stepper.Down; onClicked: presetMenu.openFor(this,0,height) }
        LV.LabelButton {
            id: publish
            objectName: "editorPublish"
            width: 66; height: 22
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Publish")
            tone: LV.AbstractButton.Primary
            Accessible.name: text
            onClicked: root.publishRequested()
        }
    }
    LV.ContextMenu {
        id: compositeMenu; objectName: "editorViewCompositeMenu"; selectedIndex: root.canvas.viewOptions.display.composite === "Outlines" ? 1 : 0; showIconSlot: false
        items: [{label:"Composite"},{label:"Outlines"}]
        onItemTriggered: function(index) { root.canvas.viewOptions.edit("display", "composite", index === 0 ? "Composite" : "Outlines") }
    }
    LV.ContextMenu {
        id: channelMenu; objectName: "editorViewChannelMenu"; selectedIndex: NativeData.presentation.indexOf(["RGB","Red","Green","Blue","Alpha"], root.canvas.viewOptions.display.channel); showIconSlot: false
        items: ["RGB","Red","Green","Blue","Alpha"]
        onItemTriggered: function(index) { root.canvas.viewOptions.edit("display","channel", ["RGB","Red","Green","Blue","Alpha"][index]) }
    }
    LV.ContextMenu {
        id: presetMenu; objectName: "editorViewPresetMenu"; selectedIndex: NativeData.presentation.indexOf(["Smart","Pixel","Vector","Layout"], root.canvas.viewOptions.snap.preset); showIconSlot: false
        items: ["Smart","Pixel","Vector","Layout"]
        onItemTriggered: function(index) { root.canvas.viewOptions.preset(["Smart","Pixel","Vector","Layout"][index]); root.canvas.viewOptions.edit("snap","enabled",true) }
    }
    LV.Popover {
        id: optionsPopup
        objectName: "editorViewOptionsPopup"
        property Item anchor: null
        width: Math.min(384, Math.max(0, parent ? parent.width - 16 : 384))
        height: Math.min(panelLoader.item ? panelLoader.item.implicitHeight : 600, parent ? Math.max(80,parent.height - 16) : 800)
        padding: 0; modal: true; dim: false; motionEnabled: false
        x: Math.max(8,Math.min(parent ? parent.width-width-8 : 8,anchor && parent ? anchor.mapToItem(parent,0,0).x : 8))
        y: Math.max(8,Math.min(parent ? parent.height-height-8 : 8,anchor && parent ? anchor.mapToItem(parent,0,anchor.height+4).y : 8))
        background: Item {}
        contentItem: Loader {
            id: panelLoader
            active: optionsPopup.visible
            sourceComponent: EditorViewOptionsPanel { canvas: root.canvas; family: root.openFamily; onCloseRequested: optionsPopup.close() }
        }
        onClosed: root.canvas.forceActiveFocus()
    }
}
