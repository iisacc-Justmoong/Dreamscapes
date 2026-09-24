pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import LVRS 1.0 as LV

LV.AbstractButton {
    id: control
    property string label: ""
    property string iconName: ""
    property bool selected: false

    implicitWidth: LV.Theme.scaleMetric(177)
    implicitHeight: LV.Theme.scaleMetric(104)
    cornerRadius: LV.Theme.scaleMetric(18)
    horizontalPadding: 0
    verticalPadding: 0
    spacing: 0
    Accessible.name: label

    background: Rectangle {
        radius: control.resolvedCornerRadius
        color: control.down ? LV.Theme.panelBackground06 : LV.Theme.panelBackground04
        border.width: control.selected ? LV.Theme.scaleRealMetric(1.5) : LV.Theme.scaleRealMetric(1)
        border.color: control.selected ? LV.Theme.accent : LV.Theme.panelBackground10
        antialiasing: true
    }

    contentItem: Item {
        Rectangle {
            x: LV.Theme.gap16
            y: LV.Theme.gap16
            width: LV.Theme.scaleMetric(44)
            height: width
            radius: LV.Theme.scaleMetric(14)
            color: control.selected ? LV.Theme.accent : LV.Theme.panelBackground08

            Image {
                id: sourceIcon
                anchors.centerIn: parent
                width: LV.Theme.scaleMetric(24)
                height: width
                visible: !control.iconName.startsWith("dreamscapes")
                source: visible ? LV.Theme.iconPath(control.iconName) : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                layer.enabled: visible && GraphicsInfo.api !== GraphicsInfo.Software
                layer.effect: MultiEffect {
                    contrast: -1
                    brightness: 0.5
                    colorization: 1
                    colorizationColor: LV.Theme.titleHeaderColor
                }
            }

            Canvas {
                visible: control.iconName === "dreamscapesImage" || control.iconName === "dreamscapesVideo"
                anchors.centerIn: parent
                width: LV.Theme.scaleMetric(24)
                height: width
                onPaint: {
                    const context = getContext("2d")
                    context.reset()
                    context.strokeStyle = LV.Theme.titleHeaderColor
                    context.fillStyle = LV.Theme.titleHeaderColor
                    context.lineWidth = 1.7
                    context.lineCap = "round"
                    context.lineJoin = "round"
                    context.beginPath()
                    context.moveTo(6, 4)
                    context.lineTo(18, 4)
                    context.quadraticCurveTo(21, 4, 21, 7)
                    context.lineTo(21, 17)
                    context.quadraticCurveTo(21, 20, 18, 20)
                    context.lineTo(6, 20)
                    context.quadraticCurveTo(3, 20, 3, 17)
                    context.lineTo(3, 7)
                    context.quadraticCurveTo(3, 4, 6, 4)
                    context.closePath()
                    context.stroke()
                    if (control.iconName === "dreamscapesVideo") {
                        context.beginPath()
                        context.moveTo(10, 8)
                        context.lineTo(16, 12)
                        context.lineTo(10, 16)
                        context.closePath()
                        context.fill()
                    } else {
                        context.beginPath()
                        context.arc(8, 9, 1.5, 0, Math.PI * 2)
                        context.fill()
                        context.beginPath()
                        context.moveTo(5, 17)
                        context.lineTo(10, 12)
                        context.lineTo(13, 15)
                        context.lineTo(16, 12)
                        context.lineTo(20, 17)
                        context.stroke()
                    }
                }
            }

            Grid {
                visible: control.iconName === "dreamscapesBoard"
                anchors.centerIn: parent
                columns: 2
                spacing: LV.Theme.scaleMetric(4)
                Repeater {
                    model: 4
                    Rectangle {
                        width: LV.Theme.scaleMetric(7)
                        height: width
                        radius: LV.Theme.scaleMetric(2)
                        color: "transparent"
                        border.width: LV.Theme.scaleRealMetric(1.5)
                        border.color: LV.Theme.titleHeaderColor
                    }
                }
            }
        }

        LV.Label {
            x: LV.Theme.gap16
            y: LV.Theme.scaleMetric(70)
            width: parent.width - LV.Theme.gap16 * 2
            height: LV.Theme.textHeader2LineHeight
            text: control.label
            style: header2
            elide: Text.ElideRight
        }
    }
}
