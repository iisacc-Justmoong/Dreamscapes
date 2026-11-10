pragma ComponentBehavior: Bound
import Dreamscapes.Storage 1.0
import QtQuick
import LVRS 1.0 as LV
import Dreamscapes.Style 1.0 as Style

ListView {
    id: root
    property var files: []
    property int maximumItems: 4
    property bool desktopStyle: false
    readonly property var visibleFiles: NativeData.presentation.range(files || [], 0, maximumItems)
    signal fileRequested(var file)

    implicitHeight: LV.Theme.scaleMetric(280)
    model: visibleFiles
    spacing: Style.Tokens.metric("Theme Metrics/Space/8")
    clip: true
    interactive: false
    boundsBehavior: Flickable.StopAtBounds

    delegate: LV.AbstractButton {
        id: row
        required property int index
        required property var modelData
        objectName: "recentPublishedItem" + index
        width: root.width
        height: LV.Theme.scaleMetric(64)
        horizontalPadding: 0
        verticalPadding: 0
        cornerRadius: LV.Theme.scaleMetric(14)
        Accessible.name: modelData.name || ""
        Accessible.description: modelData.description || ""
        onClicked: root.fileRequested(modelData)

        background: Rectangle {
            radius: row.resolvedCornerRadius
            color: row.down || row.hovered ? Style.Tokens.color("Theme Colors/Panel/surfaceAlt")
                : root.desktopStyle ? Style.Tokens.color("Theme Colors/Panel/windowAlt") : Style.Tokens.color("Theme Colors/Panel/subSurface")
        }

        contentItem: Item {
            Rectangle {
                id: preview
                x: Style.Tokens.metric("Theme Metrics/Space/8")
                y: Style.Tokens.metric("Theme Metrics/Space/8")
                width: LV.Theme.scaleMetric(48)
                height: width
                radius: LV.Theme.scaleMetric(10)
                clip: true
                color: Style.Tokens.color("Theme Colors/Panel/surfacePanel")
                gradient: root.desktopStyle ? publicationGradient : null
                Gradient {
                    id: publicationGradient
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: row.modelData.iconName === "fileTypesimage" ? Style.Tokens.color("Theme Colors/Accent/Purple/Default") : Style.Tokens.color("Theme Colors/Accent/Orange/Default") }
                    GradientStop { position: 1; color: Style.Tokens.color("Theme Colors/Panel/canvas") }
                }

                Image {
                    anchors.fill: parent
                    source: row.modelData.previewSource || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: !root.desktopStyle && source.toString().length > 0 && status === Image.Ready
                }

                Image {
                    anchors.centerIn: parent
                    width: LV.Theme.scaleMetric(24)
                    height: width
                    source: root.desktopStyle ? LV.Theme.iconPath("spark")
                        : LV.Theme.iconPath(row.modelData.iconName || "fileTypesimage")
                    fillMode: Image.PreserveAspectFit
                }
            }

            LV.Label {
                x: LV.Theme.scaleMetric(68)
                y: LV.Theme.scaleMetric(11)
                width: parent.width - x - LV.Theme.scaleMetric(44)
                height: LV.Theme.scaleMetric(20)
                text: row.modelData.name || ""
                style: body
                font.pixelSize: Style.Tokens.metric("Theme Typography/Body/textBody")
                elide: Text.ElideRight
            }

            LV.Label {
                x: LV.Theme.scaleMetric(68)
                y: LV.Theme.scaleMetric(34)
                width: parent.width - x - LV.Theme.scaleMetric(44)
                height: LV.Theme.scaleMetric(18)
                text: NativeData.presentation.join([row.modelData.description || "", row.modelData.dateText || ""], " · ")
                style: caption
                elide: Text.ElideRight
            }

            Image {
                x: parent.width - LV.Theme.scaleMetric(32)
                y: LV.Theme.scaleMetric(20)
                width: LV.Theme.scaleMetric(24)
                height: width
                source: root.desktopStyle ? LV.Theme.iconPath("generalopen") : LV.Theme.iconPath("generalchevronRight")
                fillMode: Image.PreserveAspectFit
            }
        }
    }
}
