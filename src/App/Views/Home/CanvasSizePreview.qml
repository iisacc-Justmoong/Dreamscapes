pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

Rectangle {
    id: root
    property real pixelWidth: 1
    property real pixelHeight: 1
    property bool selected: false
    property bool showRatio: false
    property string ratioLabel: ""
    color: LV.Theme.window
    radius: LV.Theme.radiusMd
    Rectangle {
        anchors.centerIn: parent
        readonly property real ratio: root.pixelWidth > 0 && root.pixelHeight > 0
            ? root.pixelWidth / root.pixelHeight : 1
        width: Math.min(parent.width * 0.72, parent.height * 0.74 * ratio)
        height: width / ratio
        radius: Math.min(LV.Theme.radiusMd, height / 3)
        color: root.selected ? LV.Theme.accent : "#e8e8e8"
        LV.Label {
            anchors.centerIn: parent
            visible: root.showRatio
            text: root.ratioLabel
            style: caption
            color: "white"
        }
    }
}
