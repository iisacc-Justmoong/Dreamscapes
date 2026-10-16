import QtQuick

Canvas {
    id: root
    property var stops: [{position: 0, color: "#8066FF"}, {position: .5, color: "#E88DD9"}, {position: 1, color: "#FFAA5A"}]
    property real angle: 90
    property bool radial: false
    onStopsChanged: requestPaint()
    onAngleChanged: requestPaint()
    onRadialChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        const radians=(angle-90)*Math.PI/180
        const dx=Math.cos(radians),dy=Math.sin(radians)
        const span=Math.abs(dx)*width+Math.abs(dy)*height
        const gradient = radial ? ctx.createRadialGradient(width/2,height/2,0,width/2,height/2,Math.max(width,height)/2) : ctx.createLinearGradient(width/2-dx*span/2,height/2-dy*span/2,width/2+dx*span/2,height/2+dy*span/2)
        stops.forEach(function(stop) { gradient.addColorStop(Math.max(0, Math.min(1, stop.position)), stop.color) })
        ctx.clearRect(0, 0, width, height)
        ctx.fillStyle = gradient
        ctx.beginPath()
        ctx.roundedRect(0,0,width,height,4,4)
        ctx.fill()
    }
}
