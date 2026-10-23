pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import "EditorViewOptionDefinitions.js" as Definitions

Item {
    id: root
    required property var canvas
    property string family: "snap"
    readonly property string title: family === "snap" ? "Snap" : family === "grid" ? "Grid" : "Guides"
    readonly property var values: canvas.viewOptions[family]
    readonly property string layoutType: values.type || ""
    readonly property string layoutUnits: values.units || ""
    readonly property string layoutOrientation: values.orientation || ""
    readonly property var sections: Definitions.sections(family, {type: layoutType, units: layoutUnits, orientation: layoutOrientation})
    readonly property bool modalActive: true
    signal closeRequested()
    implicitWidth: 384
    implicitHeight: content.height + 83
    Rectangle { anchors.fill: parent; color: LV.Theme.surfaceSolid; radius: LV.Theme.radiusControl; border.width: 1; border.color: LV.Theme.surfacePanel }
    Row {
        id: header
        x: 8; y: 8; width: parent.width - 16; height: 24; spacing: 8
        Image { anchors.verticalCenter: parent.verticalCenter; width: 16; height: 16; source: "qrc:/editor/view-options/" + root.family + ".svg"; sourceSize.width: 16; sourceSize.height: 16 }
        LV.Label { width: parent.width - 56; height: 24; verticalAlignment: Text.AlignVCenter; style: body; text: root.title }
        LV.IconButton { objectName: "viewOptionsClose"; width: 24; height: 24; iconSize: 16; iconSource: "qrc:/editor/view-options/close.svg"; tone: LV.AbstractButton.Default; Accessible.name: qsTr("Close view options"); onClicked: root.closeRequested() }
    }
    Flickable {
        objectName: "viewOptionsScroll"
        x: 0; y: 40; width: parent.width; height: Math.max(0, parent.height - 80)
        contentWidth: width; contentHeight: content.height
        flickableDirection: Flickable.VerticalFlick; boundsBehavior: Flickable.StopAtBounds; clip: true
        Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
        Column {
            id: content
            width: parent.width; spacing: 0
            Repeater {
                model: root.sections
                Column {
                    id: group
                    required property var modelData
                    required property int index
                    width: content.width; spacing: 0
                    Rectangle { visible: group.index > 0; width: parent.width; height: visible ? 1 : 0; color: LV.Theme.surfacePanel }
                    Column {
                        x: 8; width: parent.width - 16; topPadding: 8; bottomPadding: 8; spacing: 4
                        LV.Label { visible: Boolean(group.modelData.label); height: visible ? 13 : 0; style: body; text: group.modelData.label; width: parent.width }
                        Repeater { model: group.modelData.rows; EditorViewOptionRow { required property var modelData; canvas: root.canvas; family: root.family; definition: modelData } }
                    }
                }
            }
        }
    }
    Rectangle { x: 0; y: parent.height - 40; width: parent.width; height: 1; color: LV.Theme.surfacePanel }
    Item {
        x: 8; y: parent.height - 32; width: parent.width - 16; height: 24
        LV.LabelButton { objectName: "viewOptionsReset"; width: 64; height: 24; text: qsTr("Reset"); tone: LV.AbstractButton.Default; onClicked: root.canvas.viewOptions.reset(root.family) }
        LV.IconButton { objectName: "viewOptionsClearGuides"; x: 72; width: 24; height: 24; visible: root.family === "guides"; enabled: root.canvas.viewOptions.guideCount > 0; iconName: "delete"; iconSize: 16; tone: LV.AbstractButton.Default; Accessible.name: qsTr("Clear all guides"); onClicked: root.canvas.viewOptions.clearGuides() }
        LV.LabelButton { objectName: "viewOptionsCreateGuides"; visible: root.family === "guides"; anchors.right: done.left; anchors.rightMargin: 8; width: 116; height: 24; text: root.values.type === "Single" ? qsTr("Add guide") : qsTr("Create guides"); tone: LV.AbstractButton.Default; onClicked: if (root.canvas.createViewGuides()) root.closeRequested() }
        LV.LabelButton { id: done; objectName: "viewOptionsDone"; anchors.right: parent.right; width: 60; height: 24; text: qsTr("Done"); tone: LV.AbstractButton.Default; onClicked: root.closeRequested() }
    }
}
