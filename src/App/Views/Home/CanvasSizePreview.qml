pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV
import Dreamscapes.Style 1.0 as Style

Rectangle {
    id: root
    property real pixelWidth: 1
    property real pixelHeight: 1
    property bool selected: false
    property bool showRatio: false
    property string ratioLabel: ""
    color: Style.Tokens.color("Theme Colors/Panel/window")
    radius: Style.Tokens.metric("Theme Metrics/Radius/8")
    Rectangle {
        anchors.centerIn: parent
        readonly property real ratio: root.pixelWidth > 0 && root.pixelHeight > 0
            ? root.pixelWidth / root.pixelHeight : 1
        width: Math.min(parent.width * 0.72, parent.height * 0.74 * ratio)
        height: width / ratio
        radius: Math.min(Style.Tokens.metric("Theme Metrics/Radius/8"), height / 3)
        color: root.selected ? LV.Theme.accent : Style.Tokens.color("Theme Colors/Text/titleHeaderColor")
        LV.Label {
            anchors.centerIn: parent
            visible: root.showRatio
            text: root.ratioLabel
            style: caption
            color: Style.Tokens.color("Theme Colors/Text/textTokenBase")
        }
    }
}
