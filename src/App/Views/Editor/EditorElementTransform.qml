pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "editorElementTransform"
    required property var canvas
    readonly property var state: canvas ? canvas.elementTransform : ({})
    readonly property bool transforming: Boolean(state.dragging)
    readonly property var dimensionAnchor: state.points && state.points.length === 4 ? state.points[2] : Qt.point(0, 0)
    visible: Boolean(state.visible)
    // Artwork uses canvas coordinates; native input performs the corresponding
    // hit tests so handles remain the same size through zoom and page offsets.
    onStateChanged: drawing.requestPaint()
    onWidthChanged: drawing.requestPaint()
    onHeightChanged: drawing.requestPaint()
    Canvas {
        id: drawing
        anchors.fill: parent
        renderTarget: Canvas.Image
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            function outline(points, active) {
                if (!points || points.length !== 4) return
                ctx.beginPath()
                ctx.moveTo(points[0].x, points[0].y)
                for (let i = 1; i < 4; ++i) ctx.lineTo(points[i].x, points[i].y)
                ctx.closePath()
                ctx.strokeStyle = "#FFFFFF"; ctx.lineWidth = active ? 3 : 2; ctx.stroke()
                ctx.strokeStyle = active ? "#4096F3" : "#94BFF2"; ctx.lineWidth = 1; ctx.stroke()
                if (!active) return
                ctx.strokeStyle = "#4096F3"; ctx.globalAlpha = 0.65
                for (let t = 1; t <= 2; ++t) {
                    const f = t / 3
                    ctx.beginPath()
                    ctx.moveTo(points[0].x + (points[1].x - points[0].x) * f, points[0].y + (points[1].y - points[0].y) * f)
                    ctx.lineTo(points[3].x + (points[2].x - points[3].x) * f, points[3].y + (points[2].y - points[3].y) * f)
                    ctx.moveTo(points[0].x + (points[3].x - points[0].x) * f, points[0].y + (points[3].y - points[0].y) * f)
                    ctx.lineTo(points[1].x + (points[2].x - points[1].x) * f, points[1].y + (points[2].y - points[1].y) * f)
                    ctx.stroke()
                }
                ctx.globalAlpha = 1
            }
            const selections = root.state.selections || []
            for (let i = 0; i < selections.length; ++i) outline(selections[i].points, false)
            outline(root.state.points, true)
            if (root.state.editable && !root.state.creating) {
                ctx.strokeStyle = "#4096F3"; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(root.state.rotateStem.x, root.state.rotateStem.y)
                ctx.lineTo(root.state.rotate.x, root.state.rotate.y); ctx.stroke()
                ctx.beginPath(); ctx.arc(root.state.center.x, root.state.center.y, 3, 0, Math.PI * 2)
                ctx.stroke()
            }
        }
    }
    Repeater {
        model: root.state.editable ? root.state.handles || [] : []
        Rectangle {
            required property var modelData
            objectName: "elementTransformHandle-" + modelData.id
            x: modelData.point.x - width / 2; y: modelData.point.y - height / 2
            width: 9; height: 9
            color: "#FFFFFF"; border.width: 1; border.color: "#4096F3"
        }
    }
    Rectangle {
        objectName: "elementTransformHandle-rotate"
        visible: Boolean(root.state.editable)
        x: root.state.rotate ? root.state.rotate.x - width / 2 : 0
        y: root.state.rotate ? root.state.rotate.y - height / 2 : 0
        width: 9; height: 9; radius: width / 2
        color: "#FFFFFF"; border.width: 1; border.color: "#4096F3"
    }
    LV.Tooltip {
        objectName: "elementTransformDimensions"
        target: root
        automatic: false
        visible: root.transforming && root.visible
        anchorPoint: Qt.point(root.dimensionAnchor.x + 12, root.dimensionAnchor.y + 12)
        preferredPlacement: LV.Tooltip.Below
        contentPadding: 6
        cornerRadius: 4
        tailLength: 0
        motionEnabled: false
        delay: 0; timeout: -1
        text: Number(root.state.width || 0).toFixed(2).replace(/\.00$/, "") + " × " + Number(root.state.height || 0).toFixed(2).replace(/\.00$/, "") + " px"
    }
}
