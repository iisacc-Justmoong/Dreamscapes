pragma ComponentBehavior: Bound
import QtQuick
import ".." as Frames
import QtQuick.Layouts
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import Dreamscapes.Style 1.0 as Style

Item {
    id: root
    readonly property Item panelFrame: Frames.PanelFrame { parent: root; panelId: "result-inspector" }
    required property var review
    signal previewRequested()
    signal saveRequested()
    signal playbackFailed(string message)
    readonly property var focused: review.focusedEntry
    LV.VStack {
        anchors.fill: parent
        spacing: Style.Tokens.metric("Theme Metrics/Space/8")
        LV.HStack {
            Layout.fillWidth: true
            LV.Label { text: qsTr("INSPECTOR"); style: caption; Layout.fillWidth: true }
            LV.Label { text: root.review.selectedCount > 1 ? qsTr("%1 selected").arg(root.review.selectedCount) : qsTr("Focused result"); style: caption }
        }
        ReviewMediaView {
            objectName: "reviewInspectorPreview"
            Layout.fillWidth: true
            Layout.preferredHeight: 178
            source: root.focused.imageSource || ""
            mediaSource: root.focused.mediaSource || ""
            video: root.focused.mediaType === "Video"
            pixelWidth: root.focused.width || 0
            pixelHeight: root.focused.height || 0
            onPlaybackFailed: function(message) { root.playbackFailed(message) }
        }
        LV.HStack {
            Layout.fillWidth: true
            LV.Label { text: qsTr("Preview"); style: caption; color: Style.Tokens.color("Theme Colors/Text/bodyColor"); Layout.fillWidth: true }
            LV.LabelButton { text: qsTr("Expand"); enabled: !!(root.focused.imageSource || root.focused.mediaSource || root.focused.documentSource); tone: LV.AbstractButton.Default; onClicked: root.previewRequested() }
            LV.LabelButton { text: qsTr("Save current"); enabled: !!(root.focused.documentSource || root.focused.mediaSource || root.focused.imageSource); tone: LV.AbstractButton.Default; onClicked: root.saveRequested() }
        }
        Flickable {
            id: viewport
            objectName: "reviewInspectorScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: fields.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
            LV.VStack {
                id: fields
                width: Math.max(0, viewport.width - Style.Tokens.metric("Theme Metrics/Space/8"))
                spacing: Style.Tokens.metric("Theme Metrics/Space/8")
                Repeater {
                    model: root.review.inspectorRows
                    delegate: LV.VStack {
                        id: field
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Style.Tokens.metric("Theme Metrics/Space/2")
                        LV.Label { Layout.fillWidth: true; text: field.modelData.label; style: caption; color: Style.Tokens.color("Theme Colors/Text/bodyColor") }
                        Controls.TextArea {
                            objectName: "reviewInspector" + field.modelData.label.replace(/\s/g, "")
                            Layout.fillWidth: true
                            text: field.modelData.mixed ? field.modelData.value + "\n" + field.modelData.values.join("\n") : field.modelData.value
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.WrapAnywhere
                            textFormat: TextEdit.PlainText
                            color: Style.Tokens.color("Theme Colors/Text/titleHeaderColor")
                            font: inspectorLabel.font
                            padding: 0
                            background: null
                        }
                    }
                }
                LV.Label { id: inspectorLabel; visible: false; style: caption }
                LV.Label {
                    Layout.fillWidth: true
                    visible: !!root.focused.id
                    text: qsTr("Job · %1").arg(root.focused.id || "")
                    style: caption
                    wrapMode: Text.WrapAnywhere
                    color: Style.Tokens.color("Theme Colors/Text/bodyColor")
                }
                LV.Label {
                    Layout.fillWidth: true
                    text: qsTr("Original · %1").arg(root.focused.documentSource || root.focused.mediaSource || qsTr("Unavailable"))
                    style: caption
                    wrapMode: Text.WrapAnywhere
                    color: Style.Tokens.color("Theme Colors/Text/bodyColor")
                }
            }
        }
    }
}
