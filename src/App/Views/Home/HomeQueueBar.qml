pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

Item {
    id: root
    objectName: "homeQueueBar"
    property var jobs: []
    property string statusText: ""
    property string modelName: ""
    property string deviceName: qsTr("Automatic")
    readonly property int runningCount: NativeData.facts.stateCount(jobs, ["running", "connecting-host", "downloading"])
    readonly property int waitingCount: NativeData.facts.stateCount(jobs, ["queued"])
    readonly property int failedCount: NativeData.facts.stateCount(jobs, ["failed"])
    signal viewQueueRequested()
    implicitHeight: 160
    LV.VStack { alignment: Qt.AlignLeft;
        anchors.fill: parent
        anchors.margins: LV.Theme.gap12
        spacing: LV.Theme.gap8
        LV.HStack {
            Layout.fillWidth: true
            LV.Label { text: qsTr("Generation queue"); style: header2; Layout.fillWidth: true }
            LV.LabelButton { text: qsTr("View queue"); tone: LV.AbstractButton.Borderless; onClicked: root.viewQueueRequested() }
        }
        LV.HStack {
            Layout.fillWidth: true; spacing: LV.Theme.gap8
            Image { source: Qt.resolvedUrl("Assets/queue-ready.svg"); Layout.preferredWidth: 6; Layout.preferredHeight: 6 }
            LV.VStack { alignment: Qt.AlignLeft;
                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: LV.Theme.gap2
                LV.Label { text: root.statusText || (root.runningCount ? qsTr("Generating…") : root.waitingCount ? qsTr("Queued") : qsTr("Ready to generate")); style: body; Layout.fillWidth: true; elide: Text.ElideRight }
                LV.Label { objectName: "homeQueueRunning"; text: qsTr("%1 running · %2 waiting · %3 failed").arg(root.runningCount).arg(root.waitingCount).arg(root.failedCount); style: caption; color: LV.Theme.descriptionColor }
            }
        }
        LV.HStack {
            Layout.fillWidth: true; spacing: LV.Theme.gap16
            Repeater {
                model: [{label: qsTr("Device"), value: root.deviceName}, {label: qsTr("Model"), value: root.modelName || qsTr("Unavailable")}, {label: qsTr("Execution"), value: root.runningCount ? qsTr("Running") : qsTr("Idle")}]
                delegate: LV.VStack { alignment: Qt.AlignLeft;
                    required property var modelData
                    Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: LV.Theme.gap4
                    LV.Label { text: modelData.label; style: caption; color: LV.Theme.descriptionColor }
                    LV.Label { text: modelData.value; style: body; Layout.fillWidth: true; elide: Text.ElideRight }
                }
            }
        }
        LV.Label { text: qsTr("Generation progress and failures appear here."); style: caption; color: LV.Theme.descriptionColor; Layout.fillWidth: true; elide: Text.ElideRight }
    }
}
