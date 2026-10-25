pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

// Foreground decoration: it neither takes input nor participates in a layout.
// An explicit target lets layout-owned panels keep their original geometry.
Rectangle {
    id: frame
    property Item target: parent
    property string panelId: target ? target.objectName : ""
    property bool topEdge: true
    property bool rightEdge: true
    property bool bottomEdge: true
    property bool leftEdge: true
    readonly property int strokeWidth: 1
    readonly property bool completeBorder: topEdge && rightEdge && bottomEdge && leftEdge
    readonly property point origin: {
        let item = target
        let left = 0
        let top = 0
        while (item && item !== parent) {
            left += item.x
            top += item.y
            item = item.parent
        }
        return Qt.point(left, top)
    }
    objectName: "panelFrame_" + panelId
    x: origin.x
    y: origin.y
    width: target ? target.width : 0
    height: target ? target.height : 0
    visible: target && target.visible && width > 0 && height > 0
    z: 1000
    color: "transparent"
    radius: 0
    border.width: completeBorder ? strokeWidth : 0
    border.color: LV.Theme.accentGrayMuted
    antialiasing: false
    enabled: false
    Accessible.ignored: true
    Rectangle {
        objectName: "panelEdge_" + frame.panelId + "-top"
        visible: !frame.completeBorder && frame.topEdge
        width: frame.width; height: frame.strokeWidth
        color: frame.border.color
    }
    Rectangle {
        objectName: "panelEdge_" + frame.panelId + "-right"
        visible: !frame.completeBorder && frame.rightEdge
        x: frame.width - width; width: frame.strokeWidth; height: frame.height
        color: frame.border.color
    }
    Rectangle {
        objectName: "panelEdge_" + frame.panelId + "-bottom"
        visible: !frame.completeBorder && frame.bottomEdge
        y: frame.height - height; width: frame.width; height: frame.strokeWidth
        color: frame.border.color
    }
    Rectangle {
        objectName: "panelEdge_" + frame.panelId + "-left"
        visible: !frame.completeBorder && frame.leftEdge
        width: frame.strokeWidth; height: frame.height
        color: frame.border.color
    }
}
