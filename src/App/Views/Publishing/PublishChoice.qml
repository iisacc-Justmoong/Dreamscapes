pragma ComponentBehavior: Bound
import Dreamscapes.Storage 1.0
import QtQuick
import LVRS 1.0 as LV

Column {
    id: root
    property string label
    property string value
    property var options: []
    signal chosen(string value)
    spacing: LV.Theme.gap6
    LV.Label { width: parent.width; visible: text.length > 0; text: root.label; style: caption }
    // LV.ComboBox's indicator source is read-only. This LVRS button composition
    // retains the Figma screen's original 18px green stepper asset.
    LV.AbstractButton {
        id: face
        objectName: root.objectName + "Button"
        width: parent.width; height: 20
        tone: LV.AbstractButton.Default
        horizontalPadding: 0; verticalPadding: 0; cornerRadius: LV.Theme.radiusControl
        backgroundColor: LV.Theme.surfaceInput
        Accessible.name: root.label + ": " + root.value
        onClicked: choices.openFor(face, 0, height + LV.Theme.gap4)
        contentItem: Item {
            LV.Label { x: 8; anchors.verticalCenter: parent.verticalCenter; width: Math.max(0,parent.width-28); text: root.value || qsTr("Select…"); style: body; elide: Text.ElideRight }
            Image { anchors.right: parent.right; anchors.rightMargin: 1; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18; source: "Assets/stepper.svg"; sourceSize.width: 18; sourceSize.height: 18 }
        }
    }
    LV.ContextMenu {
        id: choices
        objectName: root.objectName + "Menu"
        showIconSlot: false
        items: NativeData.presentation.labels(root.options, "")
        selectedIndex: NativeData.presentation.indexOf(root.options, root.value)
        onItemTriggered: function(index) { root.chosen(String(root.options[index])) }
    }
}
