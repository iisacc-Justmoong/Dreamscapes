pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

LV.VStack { alignment: Qt.AlignLeft;
    id: root
    objectName: "generationHistory"
    property var files: []
    property bool loading: false
    property string focusedKey: ""
    onFilesChanged: {
        const retainedKey = focusedKey, retainedY = grid.contentY
        Qt.callLater(function() {
            const index = NativeData.facts.fileIndex(root.files, retainedKey)
            if (index >= 0) grid.currentIndex = index
            grid.contentY = Math.max(0, Math.min(retainedY, grid.contentHeight - grid.height))
        })
    }
    signal fileRequested(var file)
    signal imagesRequested(var images)
    signal viewAllRequested()
    // Stock 22px header controls + two 48px result rows + the footer fit in
    // the 160px section without cutting off the second row's metadata.
    spacing: 0
    Layout.minimumWidth: 0
    CanvasPreview { id: previews }
    LV.HStack {
        Layout.fillWidth: true
        LV.Label { text: qsTr("Recent generations"); style: header2; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
        ImageSelectionBar { id: selection; objectName: "generationHistorySelection"; entries: root.files; onOpenRequested: function(images) { root.imagesRequested(images) } }
        LV.LabelButton { objectName: "viewAllGenerationHistory"; text: qsTr("View all"); tone: LV.AbstractButton.Borderless; onClicked: root.viewAllRequested() }
    }
    GridView {
        id: grid
        objectName: "generationHistoryCards"
        Layout.fillWidth: true
        Layout.fillHeight: true
        model: root.files
        cellWidth: width / (width >= 500 ? 2 : 1)
        cellHeight: 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        keyNavigationEnabled: false
        visible: count > 0
        Keys.onPressed: function(event) {
            let next = currentIndex
            const columns = width >= 500 ? 2 : 1
            if (event.key === Qt.Key_Down) next += columns
            else if (event.key === Qt.Key_Up) next -= columns
            else if (event.key === Qt.Key_Right) next++
            else if (event.key === Qt.Key_Left) next--
            else if (event.key === Qt.Key_Home) next = 0
            else if (event.key === Qt.Key_End) next = count - 1
            else return
            if (count) { currentIndex = Math.max(0, Math.min(count - 1, next)); root.focusedKey = NativeData.facts.key(root.files[currentIndex]); positionViewAtIndex(currentIndex, GridView.Contain) }
            event.accepted = true
        }
        function activate(file) { if (selection.selectionMode) selection.toggle(file); else root.fileRequested(file) }
        Keys.onReturnPressed: if (currentIndex >= 0) activate(root.files[currentIndex])
        Keys.onEnterPressed: if (currentIndex >= 0) activate(root.files[currentIndex])
        Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
        delegate: Rectangle {
            id: result
            required property var modelData
            required property int index
            objectName: "generationHistoryCard" + index
            width: grid.cellWidth - LV.Theme.gap12
            height: grid.cellHeight
            Accessible.role: Accessible.Button
            Accessible.name: modelData.name || ""
            Accessible.onPressAction: grid.activate(modelData)
            readonly property url previewSource: previews.sourceForFile(modelData)
            readonly property int previewStatus: thumbnail.status
            color: selection.isSelected(modelData) ? LV.Theme.accentMuted : "transparent"
            LV.HStack {
                anchors.fill: parent
                spacing: LV.Theme.gap8
                Image { id: thumbnail; source: result.previewSource; sourceSize: Qt.size(120,80); asynchronous: true; fillMode: Image.PreserveAspectCrop; clip: true; Layout.preferredWidth: 60; Layout.preferredHeight: 40 }
                LV.VStack { alignment: Qt.AlignLeft;
                    Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: LV.Theme.gap2
                    LV.Label { text: result.modelData.name || ""; style: body; Layout.fillWidth: true; elide: Text.ElideRight }
                    LV.Label { text: NativeData.facts.model(result.modelData) + " · " + NativeData.facts.kind(result.modelData); style: caption; color: LV.Theme.descriptionColor; Layout.fillWidth: true; elide: Text.ElideRight }
                    LV.Label { text: NativeData.facts.resultSummary(result.modelData); style: caption; color: LV.Theme.descriptionColor; Layout.fillWidth: true; elide: Text.ElideRight }
                }
            }
            MouseArea { anchors.fill: parent; onClicked: { grid.currentIndex = result.index; root.focusedKey = NativeData.facts.key(result.modelData); grid.forceActiveFocus(); grid.activate(result.modelData) } }
        }
    }
    LV.Label { objectName: "emptyGenerationHistory"; visible: !root.files.length; text: root.loading ? qsTr("Reading Society…") : qsTr("No generated images yet"); style: body }
    LV.Label { text: qsTr("%1 recent results").arg(root.files.length); style: caption; color: LV.Theme.descriptionColor }
}
