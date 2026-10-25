pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import LVRS 1.0 as LV

LV.Modal {
    id: root
    frameCornerRadius: 0
    objectName: "failureDetailsModal"
    property var report: ({})
    title: qsTr("Failure explanation")
    showIcon: false
    minWidth: 0
    maxWidth: 860
    primaryText: qsTr("Close")
    onPrimaryClicked: root.open = false
    contentComponent: Component {
        Flickable {
            objectName: "failureDetailsScroll"
            implicitHeight: Math.max(120, Math.min(540, root.height - 200))
            contentWidth: width
            contentHeight: explanation.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
            LV.Label {
                id: explanation
                objectName: "failureExplanationText"
                width: parent.width - 16
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                style: body
                text: (root.report.state === "pending" ? qsTr("Agent analysis is running. Captured evidence is available below.\n\n") : "")
                    + (root.report.details || root.report.rawError || "")
                    + (root.report.modelUnavailableReason ? "\n\n" + root.report.modelUnavailableReason : "")
                    + (root.report.agentError ? qsTr("\n\nAgent status: ") + root.report.agentError : "")
                    + (root.report.persistenceError ? qsTr("\n\nReport could not be saved: ") + root.report.persistenceError : "")
                    + (root.report.reportPath ? qsTr("\n\nReport file: ") + root.report.reportPath : "")
            }
        }
    }
}
