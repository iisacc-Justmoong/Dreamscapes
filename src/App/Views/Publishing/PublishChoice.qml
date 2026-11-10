pragma ComponentBehavior: Bound
import Dreamscapes.Storage 1.0
import QtQuick
import LVRS 1.0 as LV
import Dreamscapes.Style 1.0 as Style

Column {
    id: root
    property string label
    property string value
    property var options: []
    signal chosen(string value)
    spacing: Style.Tokens.metric("Theme Metrics/Space/6")
    LV.Label { width: parent.width; visible: text.length > 0; text: root.label; style: caption }
    // The shared stepper is used inside the existing keyboard-accessible button.
    LV.AbstractButton {
        id: face
        objectName: root.objectName + "Button"
        width: parent.width; height: 20
        tone: LV.AbstractButton.Default
        horizontalPadding: 0; verticalPadding: 0; cornerRadius: LV.Theme.radiusControl
        backgroundColor: Style.Tokens.color("Theme Colors/Panel/surfaceInput")
        Accessible.name: root.label + ": " + root.value
        onClicked: choices.openFor(face, 0, height + Style.Tokens.metric("Theme Metrics/Space/4"))
        contentItem: Item {
            LV.Label { x: 8; anchors.verticalCenter: parent.verticalCenter; width: Math.max(0,parent.width-28); text: root.value || qsTr("Select…"); style: body; elide: Text.ElideRight }
            LV.Stepper {
                objectName: root.objectName + "Stepper"
                anchors.right: parent.right; anchors.rightMargin: 1; anchors.verticalCenter: parent.verticalCenter
                width: 18; height: 18; arrow: LV.Stepper.UpDown; tone: LV.AbstractButton.Primary
                enabled: face.enabled
                onClicked: choices.openFor(face, 0, face.height + Style.Tokens.metric("Theme Metrics/Space/4"))
            }
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
