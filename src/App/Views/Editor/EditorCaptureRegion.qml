pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import LVRS 1.0 as LV

Window {
    id: root
    objectName: "photoCaptureRegionPicker"
    property var target: ({x: 0, y: 0, width: 800, height: 600})
    property point startPoint: Qt.point(0, 0)
    property rect selection: Qt.rect(0, 0, 0, 0)
    signal selected(rect rect)
    title: qsTr("Select a capture region")
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    modality: Qt.ApplicationModal
    color: "transparent"
    x: target.x || 0; y: target.y || 0
    width: target.width || 800; height: target.height || 600
    onVisibleChanged: if (visible) selection = Qt.rect(0, 0, 0, 0)
    Rectangle { anchors.fill: parent; color: LV.Theme.window; opacity: 0.4 }
    Rectangle {
        x: root.selection.x; y: root.selection.y
        width: root.selection.width; height: root.selection.height
        color: "transparent"; border.width: 2; border.color: LV.Theme.accentWhite
    }
    LV.Label { anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: 24; style: body; color: LV.Theme.accentWhite; text: qsTr("Drag to capture a region · Esc to cancel") }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.CrossCursor
        onPressed: function(mouse) { root.startPoint = Qt.point(mouse.x, mouse.y); root.selection = Qt.rect(mouse.x, mouse.y, 0, 0) }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const endX = Math.max(0, Math.min(root.width, mouse.x)), endY = Math.max(0, Math.min(root.height, mouse.y))
            root.selection = Qt.rect(Math.min(root.startPoint.x, endX), Math.min(root.startPoint.y, endY), Math.abs(endX - root.startPoint.x), Math.abs(endY - root.startPoint.y))
        }
        onReleased: {
            const area = root.selection
            root.close()
            if (area.width >= 2 && area.height >= 2) root.selected(Qt.rect(root.x + area.x, root.y + area.y, area.width, area.height))
        }
    }
    Item { anchors.fill: parent; focus: true; Keys.onEscapePressed: root.close() }
}

