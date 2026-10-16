import QtQuick
import LVRS 1.0 as LV

Column {
    id: root
    required property string title
    property Component accessory
    property real accessoryWidth: 0
    width: parent.width
    spacing: 8
    Rectangle { width: parent.width; height: 1; color: LV.Theme.surfacePanel }
    Item {
        width: parent.width; height: root.accessory ? 20 : 18
        LV.Label { width: parent.width-root.accessoryWidth; height: parent.height; style: body; font.weight: Font.Medium; font.styleName:"Medium"; color: LV.Theme.titleHeaderColor; text: root.title; verticalAlignment: Text.AlignVCenter }
        Loader { anchors.right: parent.right; width: root.accessoryWidth; height: parent.height; sourceComponent: root.accessory }
    }
}
