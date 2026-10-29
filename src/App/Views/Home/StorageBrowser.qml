pragma ComponentBehavior: Bound
import QtQuick
import ".." as Frames
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

Item {
    id: root
    readonly property Item panelFrame: Frames.PanelFrame { parent: root; panelId: "files" }
    objectName: "storageBrowser"
    property var container: null
    property var download: null
    property var imageOpenRequest: null
    property var imageImporter: null
    readonly property bool importing: !!imageImporter && imageImporter.active
    readonly property var downloadState: download ? download.state : ({})
    readonly property bool downloading: !!download && download.active
    readonly property bool downloadVisible: !!container && downloadState.containerId === container.containerId
        && String(downloadState.key || "").startsWith(section + "/")
        && (downloading || downloadState.state === "failed" || downloadState.state === "cancelled")
    property alias section: nativeBrowser.section
    property alias directory: nativeBrowser.directory
    property alias query: nativeBrowser.query
    property alias sortOrder: nativeBrowser.sortOrder
    property alias selectedKey: nativeBrowser.selectedKey
    property alias notice: nativeBrowser.notice
    property string errorText: ""
    readonly property bool available: container && container.available
    readonly property string sectionTitle: section === "generation-history" ? qsTr("Generation History")
        : section === "published" ? qsTr("Published") : qsTr("Files")
    readonly property alias sectionFiles: nativeBrowser.sectionFiles
    readonly property alias entries: nativeBrowser.entries
    readonly property alias selectedEntry: nativeBrowser.selectedEntry
    NativeStorageBrowser {
        id: nativeBrowser
        objectName: "storageNativeBrowser"
        container: root.container
        active: root.visible
        onNavigationResetRequested: { search.text = ""; selection.reset(); grid.positionViewAtBeginning() }
        onPositionResetRequested: grid.positionViewAtBeginning()
        onFileRequested: file => root.fileRequested(file)
    }
    signal backRequested()
    signal sectionRequested(string section)
    signal fileRequested(var file)
    signal imagesRequested(var images)
    signal openFileRequested()

    function updateSnapshot() { nativeBrowser.updateSnapshot() }
    function resetNavigation() { nativeBrowser.resetNavigation() }
    function focusSearch() { search.inputItem.forceActiveFocus() }
    function up() { nativeBrowser.up() }
    function activate(file) { nativeBrowser.activate(file) }
    function downloadStatus() { return nativeBrowser.downloadStatus(downloadState) }
    CanvasPreview { id: previews }
    Timer {
        id: searchDelay
        interval: 120
        onTriggered: { root.query = search.text; grid.positionViewAtBeginning() }
    }
    Shortcut {
        sequences: [StandardKey.Back]
        enabled: root.visible
        onActivated: root.directory ? root.up() : root.backRequested()
    }

    LV.VStack {
        anchors.fill: parent
        anchors.margins: LV.Theme.gap12
        spacing: LV.Theme.gap10
        LV.HStack {
            Layout.fillWidth: true
            LV.LabelButton {
                objectName: "storageBrowserBack"
                text: qsTr("Home")
                tone: LV.AbstractButton.Default
                onClicked: root.backRequested()
            }
            LV.Label {
                text: root.sectionTitle
                style: header
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }
            LV.IconButton {
                objectName: "storageBrowserRefresh"
                iconName: "generalrefresh"
                tone: LV.AbstractButton.Default
                Accessible.name: qsTr("Refresh files")
                onClicked: { root.notice = ""; if (root.container) root.container.refresh() }
            }
        }
        LV.HStack {
            Layout.fillWidth: true
            Repeater {
                model: [{key: "files", title: qsTr("Files")}, {key: "generation-history", title: qsTr("History")},
                    {key: "published", title: qsTr("Published")}]
                LV.LabelButton {
                    required property var modelData
                    objectName: "storageBrowserSection-" + modelData.key
                    text: modelData.title
                    tone: LV.AbstractButton.Default
                    checked: root.section === modelData.key
                    onClicked: root.sectionRequested(modelData.key)
                }
            }
            Item { Layout.fillWidth: true }
        }
        LV.HStack {
            Layout.fillWidth: true
            LV.InputField {
                id: search
                objectName: "storageBrowserSearch"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                placeholderText: qsTr("Search files")
                Accessible.name: qsTr("Search this collection, including folders")
                onTextChanged: searchDelay.restart()
            }
            LV.LabelButton {
                id: sort
                objectName: "storageBrowserSort"
                text: root.sortOrder === "name" ? qsTr("Name") : root.sortOrder === "oldest" ? qsTr("Oldest") : qsTr("Newest")
                tone: LV.AbstractButton.Default
                onClicked: sortMenu.openFor(sort, 0, height)
            }
        }
        LV.HStack {
            Layout.fillWidth: true
            LV.LabelButton {
                objectName: "storageBrowserUp"
                text: "↑"
                Accessible.name: qsTr("Parent folder")
                tone: LV.AbstractButton.Default
                enabled: root.directory.length > 0
                onClicked: root.up()
            }
            LV.Label {
                objectName: "storageBrowserLocation"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.sectionTitle + (root.directory ? " / " + root.directory : "")
                elide: Text.ElideMiddle
                style: caption
            }
            LV.Label {
                objectName: "storageBrowserCount"
                text: qsTr("%1 files").arg(root.sectionFiles.length)
                style: caption
            }
        }
        ImageSelectionBar {
            id: selection
            objectName: "storageBrowserSelection"
            entries: root.sectionFiles
            onOpenRequested: function(images) { root.imagesRequested(images) }
        }
        LV.HStack {
            objectName: "storageBrowserDownload"
            Layout.fillWidth: true
            visible: root.downloadVisible || root.importing
            LV.Label {
                objectName: "storageBrowserDownloadStatus"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: root.importing ? qsTr("Opening originals…") : (root.imageOpenRequest && root.imageOpenRequest.active
                    ? qsTr("Image %1/%2 · ").arg(root.imageOpenRequest.completed + 1).arg(root.imageOpenRequest.total) : "")
                    + (root.downloadState.name || "") + " · " + root.downloadStatus()
                elide: Text.ElideMiddle
                style: caption
                Accessible.role: Accessible.StaticText
            }
            LV.LabelButton {
                objectName: "storageBrowserDownloadRetry"
                text: qsTr("Retry")
                tone: LV.AbstractButton.Default
                visible: !root.downloading && !root.importing
                enabled: !!root.downloadState.key && root.available && root.downloadState.containerId === root.container.containerId
                onClicked: {
                    if (root.imageOpenRequest && root.imageOpenRequest.canRetry) {
                        root.imageOpenRequest.retry()
                        return
                    }
                    const file = NativeData.facts.fileForKey(root.sectionFiles, root.downloadState.key, "key")
                    if (file.key) root.fileRequested(file)
                }
            }
            LV.LabelButton {
                objectName: "storageBrowserDownloadCancel"
                text: qsTr("Cancel")
                tone: LV.AbstractButton.Default
                visible: root.downloading || root.importing
                onClicked: {
                    if (root.importing) root.imageImporter.cancel()
                    else if (root.download) root.download.cancel()
                }
            }
        }
        LV.ProgressBar {
            objectName: "storageBrowserDownloadProgress"
            Layout.fillWidth: true
            size: regular
            motionEnabled: false
            maximumValue: Math.max(1, root.downloadState.total || 1)
            currentValue: root.downloadState.completed || 0
            visible: root.downloading && root.downloadState.total > 0
        }
        LV.Label {
            objectName: "storageBrowserError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.notice || root.errorText || (root.container ? root.container.errorString : "")
            color: LV.Theme.accentRed
            style: caption
            wrapMode: Text.Wrap
            sizeToContentHeight: true
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            GridView {
                id: grid
                objectName: "storageBrowserGrid"
                anchors.fill: parent
                model: root.entries
                cellWidth: width / Math.max(1, Math.floor(width / 180))
                cellHeight: 208
                cacheBuffer: cellHeight
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                activeFocusOnTab: true
                keyNavigationEnabled: true
                Keys.onReturnPressed: if (currentIndex >= 0) root.activate(root.entries[currentIndex])
                Keys.onEnterPressed: if (currentIndex >= 0) root.activate(root.entries[currentIndex])
                Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
                delegate: LV.Card {
                    required property int index
                    required property var modelData
                    objectName: "storageBrowserCard" + index
                    width: grid.cellWidth - LV.Theme.gap8
                    height: grid.cellHeight - LV.Theme.gap8
                    type: modelData.isDirectory ? LV.Card.Folder : LV.Card.File
                    detail: LV.Card.Brief
                    size: LV.Card.Medium
                    filename: modelData.name || ""
                    description: modelData.isDirectory ? qsTr("Folder")
                        : modelData.available === false ? qsTr("Not downloaded") : modelData.relativePath || ""
                    metadata: root.downloading && root.downloadState.key === modelData.key ? root.downloadStatus()
                        : modelData.available === false ? qsTr("Not downloaded") : modelData.metadata1 || ""
                    iconName: modelData.isDirectory ? "nodesfolder" : modelData.iconName || "fileTypestext"
                    previewSource: root.visible && !modelData.isDirectory ? previews.sourceForFile(modelData) : ""
                    selectable: false
                    selected: root.selectedKey === modelData.key || selection.isSelected(modelData)
                    showMenu: false
                    onClicked: {
                        grid.currentIndex = index
                        root.selectedKey = modelData.key
                        if (selection.selectionMode) selection.toggle(modelData)
                    }
                    onDoubleClicked: root.activate(modelData)
                    onActionTriggered: root.activate(modelData)
                }
            }
            LV.Label {
                objectName: "storageBrowserEmpty"
                anchors.centerIn: parent
                width: Math.max(0, parent.width - LV.Theme.gap24)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                sizeToContentHeight: true
                visible: grid.count === 0
                text: !root.available ? qsTr("No mirrored drive is available. Select a drive in storage settings.")
                    : root.query ? qsTr("No matching files") : qsTr("This folder is empty")
            }
        }
        LV.HStack {
            Layout.fillWidth: true
            LV.Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                style: caption
                text: root.selectedEntry.name || ""
                elide: Text.ElideMiddle
            }
            LV.LabelButton {
                objectName: "storageBrowserOpenFile"
                text: qsTr("Open file")
                tone: LV.AbstractButton.Default
                onClicked: root.openFileRequested()
            }
            LV.LabelButton {
                objectName: "storageBrowserOpen"
                text: root.selectedEntry.available === false ? qsTr("Download and open") : qsTr("Open")
                tone: LV.AbstractButton.Default
                enabled: !!root.selectedEntry.key && !(root.downloading && root.downloadState.key === root.selectedEntry.key)
                onClicked: root.activate(root.selectedEntry)
            }
        }
    }
    LV.ContextMenu {
        id: sortMenu
        items: [qsTr("Newest"), qsTr("Oldest"), qsTr("Name")]
        onItemTriggered: function(index) { root.sortOrder = ["newest", "oldest", "name"][index] }
    }
}
