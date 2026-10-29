pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

LV.VStack { alignment: Qt.AlignLeft;
    id: root
    objectName: "desktopRecentFiles"
    property var files: []
    property bool loading: false
    property string selectedKey: ""
    property string typeFilter: "All types"
    readonly property var visibleFiles: NativeData.facts.filesOfKind(files, typeFilter)
    readonly property var selectedFile: NativeData.facts.fileForKey(files, selectedKey)
    readonly property int count: list.count
    signal fileRequested(var file)
    signal viewAllRequested()
    signal openFileRequested()
    spacing: LV.Theme.gap8
    Layout.minimumWidth: 0
    CanvasPreview { id: previews }
    function selectFile(file) { selectedKey = NativeData.facts.key(file) }
    function reconcileSelection() {
        selectedKey = NativeData.facts.reconciledFileKey(files, selectedKey)
    }
    onFilesChanged: reconcileSelection()
    Component.onCompleted: reconcileSelection()
    LV.HStack {
        Layout.fillWidth: true
        spacing: LV.Theme.gap8
        LV.Label { text: qsTr("Recent files"); style: header2 }
        LV.Label { text: qsTr("%1 files").arg(root.files.length); style: caption; color: LV.Theme.descriptionColor }
        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
        LV.LabelMenuButton { tone: LV.AbstractButton.Default; id: filter; objectName: "homeFileTypeFilter"; text: root.typeFilter; visible: root.width > 420; onClicked: typeMenu.openFor(this, 0, height) }
        LV.LabelButton { objectName: "desktopRecentFilesViewAll"; text: qsTr("View all"); tone: LV.AbstractButton.Borderless; onClicked: root.viewAllRequested() }
    }
    LV.HStack {
        Layout.fillWidth: true
        Layout.preferredHeight: 22
        spacing: LV.Theme.gap8
        LV.Label { text: qsTr("FILE"); style: caption; color: LV.Theme.descriptionColor; Layout.fillWidth: true }
        LV.Label { text: qsTr("SPECIFICATIONS"); style: caption; color: LV.Theme.descriptionColor; visible: root.width >= 550; Layout.preferredWidth: 112 }
        LV.Label { text: qsTr("UPDATED"); style: caption; color: LV.Theme.descriptionColor; visible: root.width >= 440; Layout.preferredWidth: 82 }
        LV.Label { text: qsTr("STATE"); style: caption; color: LV.Theme.descriptionColor; visible: root.width >= 550; Layout.preferredWidth: 70 }
    }
    ListView {
        id: list
        objectName: "desktopRecentFileCards"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: root.visibleFiles
        activeFocusOnTab: true
        keyNavigationEnabled: false
        boundsBehavior: Flickable.StopAtBounds
        Keys.onPressed: function(event) {
            let next = currentIndex
            if (event.key === Qt.Key_Down) next++
            else if (event.key === Qt.Key_Up) next--
            else if (event.key === Qt.Key_Home) next = 0
            else if (event.key === Qt.Key_End) next = count - 1
            else return
            if (count) { currentIndex = Math.max(0, Math.min(count - 1, next)); root.selectFile(root.visibleFiles[currentIndex]); positionViewAtIndex(currentIndex, ListView.Contain) }
            event.accepted = true
        }
        Keys.onReturnPressed: if (root.selectedKey) root.fileRequested(root.selectedFile)
        Keys.onEnterPressed: if (root.selectedKey) root.fileRequested(root.selectedFile)
        Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
        delegate: Rectangle {
            id: entry
            required property var modelData
            required property int index
            objectName: "desktopRecentFile" + index
            width: list.width
            height: 48
            Accessible.role: Accessible.ListItem
            Accessible.name: modelData.name || ""
            Accessible.selected: root.selectedKey === NativeData.facts.key(modelData)
            Accessible.onPressAction: root.selectFile(modelData)
            readonly property url previewSource: previews.sourceForFile(modelData)
            readonly property int previewStatus: image.status
            color: root.selectedKey === NativeData.facts.key(modelData) ? LV.Theme.accentMuted : "transparent"
            radius: 0
            LV.HStack {
                anchors.fill: parent
                anchors.leftMargin: LV.Theme.gap8
                anchors.rightMargin: LV.Theme.gap8
                spacing: LV.Theme.gap8
                Image { id: image; source: entry.previewSource; sourceSize: Qt.size(72,72); asynchronous: true; fillMode: Image.PreserveAspectCrop; clip: true; Layout.preferredWidth: 36; Layout.preferredHeight: 36 }
                LV.VStack { alignment: Qt.AlignLeft;
                    Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: LV.Theme.gap4
                    LV.Label { text: entry.modelData.name || ""; style: body; elide: Text.ElideRight; Layout.fillWidth: true }
                    LV.Label { text: entry.modelData.metadata1 || entry.modelData.description || NativeData.facts.kind(entry.modelData); style: caption; color: LV.Theme.descriptionColor; elide: Text.ElideRight; Layout.fillWidth: true }
                }
                LV.Label { text: NativeData.facts.dimensions(entry.modelData); style: caption; color: LV.Theme.descriptionColor; visible: root.width >= 550; Layout.preferredWidth: 112; elide: Text.ElideRight }
                LV.Label { text: NativeData.facts.date(entry.modelData); style: caption; color: LV.Theme.descriptionColor; visible: root.width >= 440; Layout.preferredWidth: 82; elide: Text.ElideRight }
                LV.Label { text: NativeData.facts.state(entry.modelData); style: caption; color: LV.Theme.descriptionColor; visible: root.width >= 550; Layout.preferredWidth: 70; elide: Text.ElideRight }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: { list.currentIndex = entry.index; root.selectFile(entry.modelData); list.forceActiveFocus() }
                onDoubleClicked: root.fileRequested(entry.modelData)
            }
        }
    }
    LV.Label { objectName: "desktopRecentFilesEmpty"; visible: !root.visibleFiles.length; text: root.loading ? qsTr("Reading Society…") : qsTr("No recent files"); style: body }
    LV.Label { text: root.selectedKey ? qsTr("1 selected · %1").arg(root.selectedFile.name || "") : qsTr("No file selected"); style: caption; color: LV.Theme.descriptionColor; Layout.fillWidth: true; elide: Text.ElideRight }
    LV.ContextMenu { id: typeMenu; showIconSlot: false; items: [qsTr("All types"), "Canvas", "Image", "Video", "Document"]; onItemTriggered: function(index, entry) { root.typeFilter = String(entry) } }
}
