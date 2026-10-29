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
    secondaryText: qsTr("Copy report")
    secondaryEnabled: reportText.length > 0
    readonly property string reportText: (root.report.state === "pending" ? qsTr("Agent analysis is running. Captured evidence is available below.\n\n") : "")
        + (root.report.details || root.report.rawError || "")
        + (root.report.modelUnavailableReason ? "\n\n" + root.report.modelUnavailableReason : "")
        + (root.report.agentError ? qsTr("\n\nAgent status: ") + root.report.agentError : "")
        + (root.report.persistenceError ? qsTr("\n\nReport could not be saved: ") + root.report.persistenceError : "")
        + (root.report.reportPath ? qsTr("\n\nReport file: ") + root.report.reportPath : "")
    onPrimaryClicked: root.open = false
    onSecondaryClicked: {
        if (root.contentItem)
            root.contentItem.copyReport()
    }
    contentComponent: Component {
        Flickable {
            objectName: "failureDetailsScroll"
            function copyReport() {
                const start = explanation.selectionStart
                const end = explanation.selectionEnd
                const cursor = explanation.cursorPosition
                explanation.selectAll()
                explanation.copy()
                if (start !== end)
                    explanation.select(start, end)
                else {
                    explanation.deselect()
                    explanation.cursorPosition = cursor
                }
            }
            implicitHeight: Math.max(120, Math.min(540, root.height - 200))
            contentWidth: width
            contentHeight: explanation.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
            TextEdit {
                id: explanation
                objectName: "failureExplanationText"
                width: parent.width - 16
                readOnly: true
                selectByMouse: true
                persistentSelection: true
                cursorVisible: false
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.WrapAnywhere
                color: LV.Theme.bodyColor
                selectionColor: LV.Theme.accent
                selectedTextColor: LV.Theme.textPrimary
                font.family: LV.Theme.fontBody
                font.pixelSize: LV.Theme.textBody
                font.weight: LV.Theme.textBodyWeight
                font.styleName: LV.Theme.textBodyStyleName
                font.letterSpacing: LV.Theme.textBodyLetterSpacing
                font.hintingPreference: Font.PreferNoHinting
                text: root.reportText
            }
        }
    }
}
