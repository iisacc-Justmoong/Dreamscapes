pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import ".." as Frames
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV

Item {
    id: root
    readonly property Item panelFrame: Frames.PanelFrame { parent: root; panelId: "generation-queue" }
    property var jobs: []
    property var inference: ({})
    property url previewSource: ""
    property string selectedJob: ""
    readonly property alias activeJob: projection.reviewActiveJob
    NativeGenerationProjection { id: projection; jobs: root.jobs }
    signal jobRequested(string id)
    signal cancelRequested(string id)
    signal failureRequested(string id)
    function count(states) { root.jobs; return projection.countStates(states) }
    function stateName(state) { return projection.queueStateLabel(state) }
    LV.VStack {
        anchors.fill: parent
        spacing: LV.Theme.gap8
        LV.HStack {
            Layout.fillWidth: true
            LV.Label { text: qsTr("GENERATION"); style: caption; Layout.fillWidth: true }
            LV.LabelButton {
                objectName: "reviewAllJobsButton"
                text: qsTr("All jobs")
                tone: LV.AbstractButton.Default
                onClicked: root.jobRequested("")
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: LV.Theme.gap8
            rowSpacing: LV.Theme.gap4
            LV.Label { text: qsTr("Running  %1").arg(root.count(["running","connecting-host","downloading"])); style: caption }
            LV.Label { text: qsTr("Queued  %1").arg(root.count(["queued"])); style: caption }
            LV.Label { text: qsTr("Complete  %1").arg(root.count(["completed"])); style: caption }
            LV.Label { text: qsTr("Failed  %1").arg(root.count(["failed"])); style: caption }
            LV.Label { text: qsTr("Canceled  %1").arg(root.count(["cancelled","interrupted"])); style: caption }
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 142 : 0
            visible: !!root.activeJob.id && root.previewSource.toString().length > 0
            Image {
                objectName: "reviewQueueLivePreview"
                anchors.fill: parent
                anchors.bottomMargin: LV.Theme.controlHeightSm
                source: parent.visible ? root.previewSource : ""
                sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                retainWhileLoading: true
            }
            LV.Label { anchors.bottom: parent.bottom; text: qsTr("Live preview"); style: caption }
        }
        ListView {
            id: queue
            objectName: "reviewQueueList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.jobs
            clip: true
            reuseItems: true
            spacing: LV.Theme.gap4
            boundsBehavior: Flickable.StopAtBounds
            Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
            delegate: Rectangle {
                id: row
                required property var modelData
                width: queue.width
                height: content.implicitHeight + LV.Theme.gap8 * 2
                radius: LV.Theme.gap4
                color: LV.Theme.surfaceSolid
                border.width: root.selectedJob === modelData.id ? 1 : 0
                border.color: LV.Theme.accent
                readonly property bool active: !!root.activeJob.id && root.activeJob.id === modelData.id
                readonly property bool pending: NativeData.presentation.indexOf(["queued","running","connecting-host","downloading"], modelData.state) >= 0
                readonly property bool measurable: active && root.inference.total > 0 && root.inference.step >= 0
                LV.VStack {
                    id: content
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: LV.Theme.gap8
                    spacing: LV.Theme.gap4
                    LV.HStack {
                        Layout.fillWidth: true
                        LV.Label { text: qsTr("Job %1").arg((row.modelData.id || "").slice(0,8)); style: caption; Layout.fillWidth: true; elide: Text.ElideRight }
                        LV.Label { text: root.stateName(row.modelData.state); style: caption; color: row.modelData.state === "failed" ? LV.Theme.accentRed : LV.Theme.textSecondary }
                    }
                    LV.Label {
                        Layout.fillWidth: true
                        text: (row.modelData.mediaType || qsTr("Unavailable")) + " · " + (row.modelData.modelName || qsTr("Model unavailable"))
                        style: caption
                        elide: Text.ElideMiddle
                    }
                    LV.ProgressBar {
                        Layout.fillWidth: true
                        visible: row.measurable
                        size: regular
                        maximumValue: Math.max(1, root.inference.total || 1)
                        currentValue: Math.max(0, root.inference.step || 0)
                        motionEnabled: false
                    }
                    LV.Label {
                        Layout.fillWidth: true
                        text: (row.active ? (root.inference.state || qsTr("Preparing"))
                            + (row.measurable ? qsTr(" · %1 / %2").arg(root.inference.step).arg(root.inference.total) : "")
                            : row.modelData.state === "completed" ? qsTr("%1 outputs").arg((row.modelData.mediaType === "Video" ? row.modelData.videos || [] : row.modelData.images || []).length)
                            : row.modelData.error || root.stateName(row.modelData.state)).replace(/[\r\n\u2028\u2029]+/g, " ")
                        style: caption
                        elide: Text.ElideRight
                    }
                    LV.HStack {
                        visible: row.pending || row.modelData.state === "failed"
                        LV.LabelButton {
                            objectName: "reviewCancelJob_" + row.modelData.id
                            visible: row.pending
                            text: qsTr("Cancel job")
                            tone: LV.AbstractButton.Default
                            onClicked: root.cancelRequested(row.modelData.id)
                        }
                        LV.LabelButton {
                            objectName: "reviewFailure_" + row.modelData.id
                            visible: row.modelData.state === "failed"
                            text: qsTr("Failure details")
                            tone: LV.AbstractButton.Default
                            onClicked: root.failureRequested(row.modelData.id)
                        }
                    }
                }
                TapHandler { onTapped: root.jobRequested(row.modelData.id) }
            }
            LV.Label { anchors.centerIn: parent; visible: queue.count === 0; text: qsTr("No generation jobs"); style: caption }
        }
    }
}
