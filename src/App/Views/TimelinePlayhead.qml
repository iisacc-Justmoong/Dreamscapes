pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Style 1.0 as Style

// Shared ruler geometry; its default color comes from the LVUI catalog.
Canvas {
    id: marker
    property color fillColor: Style.Tokens.color("Theme Colors/Accent/Blue/Default")
    implicitWidth: 22
    implicitHeight: 26
    renderTarget: Canvas.Image
    onFillColorChanged: requestPaint()
    onAvailableChanged: if (available) requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.clearRect(0, 0, width, height)
        ctx.fillStyle = marker.fillColor
        ctx.beginPath()
        ctx.moveTo(0, 0)
        ctx.lineTo(width, 0)
        ctx.lineTo(width, height * 17 / 26)
        ctx.lineTo(width / 2, height)
        ctx.lineTo(0, height * 17 / 26)
        ctx.closePath()
        ctx.fill()
    }
}
