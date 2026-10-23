pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

Item {
    id: root
    required property var canvas
    readonly property string unit: canvas.viewOptions.rulerUnit
    visible: canvas.viewOptions.display.rulers
    readonly property var horizontal: { if (!canvas || !canvas.documentReady) return []; const changed = canvas.zoom + canvas.panX + canvas.canvasOriginX; return canvas.viewRulerTicks(true, width) }
    readonly property var vertical: { if (!canvas || !canvas.documentReady) return []; const changed = canvas.zoom + canvas.panY + canvas.canvasOriginY; return canvas.viewRulerTicks(false, height) }
    Rectangle {
        x: 22; y: 0; width: Math.max(0,root.width-22); height: 22; color: LV.Theme.surfaceSolid; clip: true
        Repeater {
            model: root.horizontal
            Item {
                id: horizontalTick
                required property var modelData
                x: modelData.position - 22; width: 64; height: 22
                Rectangle { width: 1; height: 6; anchors.bottom: parent.bottom; color: LV.Theme.captionColor }
                LV.Label { x: 3; y: 2; style: caption; text: horizontalTick.modelData.label }
                Repeater { model: 3; Rectangle { required property int index; x: horizontalTick && horizontalTick.modelData ? (index+1)*horizontalTick.modelData.spacing/4 : 0; width: 1; height: 3; anchors.bottom: horizontalTick ? horizontalTick.bottom : undefined; color: LV.Theme.captionColor } }
            }
        }
    }
    Rectangle {
        x: 0; y: 22; width: 22; height: Math.max(0,root.height-22); color: LV.Theme.surfaceSolid; clip: true
        Repeater {
            model: root.vertical
            Item {
                id: verticalTick
                required property var modelData
                y: modelData.position - 22; width: 22; height: 64
                Rectangle { height: 1; width: 6; anchors.right: parent.right; color: LV.Theme.captionColor }
                LV.Label { x: 3; y: 6; rotation: -90; transformOrigin: Item.TopLeft; style: caption; text: verticalTick.modelData.label }
                Repeater { model: 3; Rectangle { required property int index; y: verticalTick && verticalTick.modelData ? (index+1)*verticalTick.modelData.spacing/4 : 0; height: 1; width: 3; anchors.right: verticalTick ? verticalTick.right : undefined; color: LV.Theme.captionColor } }
            }
        }
    }
    Rectangle {
        width: 22; height: 22; color: LV.Theme.surfacePanel
        LV.Label {
            objectName: "editorCanvasRulerUnit"
            anchors.centerIn: parent
            style: caption
            text: root.unit
        }
    }
}
