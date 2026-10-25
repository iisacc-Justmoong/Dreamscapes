pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

LV.VStack { alignment: Qt.AlignLeft;
    id: root
    objectName: "homeSelectedFile"
    property var file: ({})
    readonly property bool hasFile: NativeData.facts.key(file).length > 0
    readonly property url previewSource: hasFile ? previews.sourceForFile(file) : ""
    readonly property var facts: Object.assign({}, file, details.metadata)
    HomeFileDetails { id: details; file: root.file }
    signal fileRequested(var file)
    spacing: LV.Theme.gap8
    Layout.minimumWidth: 0
    CanvasPreview { id: previews }
    LV.HStack {
        Layout.fillWidth: true
        LV.Label { text: qsTr("Selected file"); style: header2; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
        LV.LabelButton { tone: LV.AbstractButton.Default; objectName: "homeOpenSelectedFile"; text: root.file.available === false ? qsTr("Download and open") : qsTr("Open in editor"); enabled: root.hasFile; onClicked: root.fileRequested(root.file) }
    }
    Image { objectName: "homeSelectedFilePreview"; source: root.previewSource; asynchronous: true; sourceSize: Qt.size(1024,1024); fillMode: Image.PreserveAspectFit; Layout.fillWidth: true; Layout.fillHeight: true }
    LV.Label { text: root.hasFile ? root.file.name || "" : qsTr("Select a recent file"); style: header2; Layout.fillWidth: true; elide: Text.ElideRight }
    LV.Label { text: root.file.relativePath || root.file.metadata2 || root.file.folderPath || qsTr("No file selected"); style: caption; color: LV.Theme.descriptionColor; Layout.fillWidth: true; elide: Text.ElideMiddle }
    GridLayout {
        Layout.fillWidth: true
        columns: root.width >= 400 ? 3 : 1
        columnSpacing: LV.Theme.gap16
        rowSpacing: LV.Theme.gap8
        Repeater {
            model: [{label: qsTr("Canvas"), value: NativeData.facts.dimensions(root.facts)}, {label: qsTr("Structure"), value: root.hasFile ? NativeData.facts.structure(root.facts) : qsTr("Unavailable")}, {label: qsTr("Color"), value: NativeData.facts.value(root.facts,"colorSpace") || qsTr("Unavailable")}]
            delegate: LV.VStack { alignment: Qt.AlignLeft;
                required property var modelData
                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: LV.Theme.gap4
                LV.Label { text: modelData.label; style: caption; color: LV.Theme.descriptionColor }
                LV.Label { text: modelData.value; style: body; Layout.fillWidth: true; elide: Text.ElideRight }
            }
        }
    }
    LV.Label { text: root.hasFile ? NativeData.facts.state(root.file) + " · " + NativeData.facts.date(root.file) + (root.file.metadata1 ? " · " + root.file.metadata1 : "") : ""; style: caption; color: LV.Theme.descriptionColor; Layout.fillWidth: true; elide: Text.ElideRight }
}
