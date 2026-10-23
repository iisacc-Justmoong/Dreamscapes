pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

Rectangle {
    id: root
    objectName: "homeQueueBar"
    property var jobs: []
    property string statusText: ""
    readonly property int runningCount: jobs.filter(j => ["running", "connecting-host", "downloading"].indexOf(j.state) >= 0).length
    readonly property int waitingCount: jobs.filter(j => j.state === "queued").length
    signal viewQueueRequested()
    color: LV.Theme.panelBackground03
    radius: LV.Theme.radiusLg
    implicitHeight: row.implicitHeight + LV.Theme.gap12 * 2
    LV.HStack {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: LV.Theme.gap12
        spacing: root.width < 420 ? LV.Theme.gap8 : LV.Theme.gap16
        Image { width: LV.Theme.gap6; height: width; source: Qt.resolvedUrl("Assets/queue-ready.svg"); sourceSize: Qt.size(width, height) }
        LV.Label { text: qsTr("Queue"); style: body; visible: root.width >= 420 }
        LV.Label {
            objectName: "homeQueueRunning"
            text: root.width < 420 ? qsTr("%1 running · %2 waiting").arg(root.runningCount).arg(root.waitingCount) : qsTr("%1 running").arg(root.runningCount)
            style: root.width < 420 ? caption : body
        }
        LV.Label { objectName: "homeQueueWaiting"; text: qsTr("%1 waiting").arg(root.waitingCount); style: body; color: LV.Theme.descriptionColor; visible: root.width >= 420 }
        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
        LV.Label {
            visible: root.width >= 600
            text: root.statusText || (root.runningCount ? qsTr("Generating…") : root.waitingCount ? qsTr("Queued") : qsTr("Ready to generate"))
            style: caption
            color: LV.Theme.descriptionColor
            elide: Text.ElideRight
            Layout.maximumWidth: root.width / 3
        }
        LV.LabelButton { text: qsTr("View queue"); tone: LV.AbstractButton.Borderless; onClicked: root.viewQueueRequested() }
    }
}
