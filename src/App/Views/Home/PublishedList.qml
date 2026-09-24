pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

ListView {
    id: root
    property var files: []
    property int maximumItems: 4
    readonly property var visibleFiles: Array.prototype.slice.call(files || [], 0, maximumItems)
    signal fileRequested(var file)

    implicitHeight: LV.Theme.scaleMetric(280)
    model: visibleFiles
    spacing: LV.Theme.gap8
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
            color: row.down ? LV.Theme.panelBackground06 : LV.Theme.panelBackground04
        }

        contentItem: Item {
            Rectangle {
                id: preview
                x: LV.Theme.gap8
                y: LV.Theme.gap8
                width: LV.Theme.scaleMetric(48)
                height: width
                radius: LV.Theme.scaleMetric(10)
                clip: true
                color: LV.Theme.panelBackground08

                Image {
                    anchors.fill: parent
                    source: row.modelData.previewSource || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: source.toString().length > 0 && status === Image.Ready
                }

                Image {
                    anchors.centerIn: parent
                    width: LV.Theme.scaleMetric(24)
                    height: width
                    source: LV.Theme.iconPath(row.modelData.iconName || "fileTypesimage")
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
                elide: Text.ElideRight
            }

            LV.Label {
                x: LV.Theme.scaleMetric(68)
                y: LV.Theme.scaleMetric(34)
                width: parent.width - x - LV.Theme.scaleMetric(44)
                height: LV.Theme.scaleMetric(18)
                text: [row.modelData.description || "", row.modelData.dateText || ""].filter(function(value) {
                    return String(value).length > 0
                }).join(" · ")
                style: caption
                elide: Text.ElideRight
            }

            Image {
                x: parent.width - LV.Theme.scaleMetric(32)
                y: LV.Theme.scaleMetric(20)
                width: LV.Theme.scaleMetric(24)
                height: width
                source: LV.Theme.iconPath("generalchevronRight")
                fillMode: Image.PreserveAspectFit
            }
        }
    }
}
