pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0
import "StorageBrowserEntries.js" as Entries

Item {
    id: root
    objectName: "storageBrowser"
    property var container: null
    property string section: "files"
    property string directory: ""
    property string query: ""
    property string sortOrder: "newest"
    property string selectedKey: ""
    property string notice: ""
    property string errorText: ""
    property bool initialized: false
    property var mirroredAliases: []
    property var mirrorRevision: -1
    readonly property bool available: container && container.available
    readonly property string storageIdentity: container ? container.containerId + "|" + container.rootPath : ""
    readonly property string sectionTitle: section === "generation-history" ? qsTr("Generation History")
        : section === "published" ? qsTr("Published") : qsTr("Files")
    readonly property var sectionFiles: available ? Entries.sectionFiles(mirroredAliases, section) : []
    readonly property var entries: Entries.listing(sectionFiles, section, directory, query, sortOrder)
    readonly property var selectedEntry: entries.find(file => file.key === selectedKey) || ({})
    signal backRequested()
    signal sectionRequested(string section)
    signal fileRequested(var file)
    signal imagesRequested(var images)
    signal openFileRequested()

    function updateSnapshot() {
        if (!initialized || !visible || !container || mirrorRevision === container.revision) return
        mirroredAliases = container.aliases
        mirrorRevision = container.revision
    }

    function resetNavigation() {
        directory = ""
        query = ""
        search.text = ""
        selectedKey = ""
        notice = ""
        selection.reset()
        grid.positionViewAtBeginning()
    }
    function focusSearch() { search.inputItem.forceActiveFocus() }
    function up() {
        if (!directory) return
        directory = directory.indexOf("/") < 0 ? "" : directory.slice(0, directory.lastIndexOf("/"))
        selectedKey = ""
        grid.positionViewAtBeginning()
    }
    function activate(file) {
        if (!file) return
        notice = ""
        if (file.isDirectory) {
            directory = file.relativePath
            selectedKey = ""
            grid.positionViewAtBeginning()
        } else if (file.available === false) notice = qsTr("This file has not been downloaded to the mirrored drive yet.")
        else if (file.key) root.fileRequested(file)
    }
    onSectionChanged: if (initialized) resetNavigation()
    onStorageIdentityChanged: if (initialized) { mirrorRevision = -1; resetNavigation(); updateSnapshot() }
    onVisibleChanged: updateSnapshot()
    Component.onCompleted: { initialized = true; resetNavigation(); updateSnapshot() }
    onEntriesChanged: if (!entries.some(file => file.key === selectedKey)) selectedKey = ""
    Connections {
        target: root.container
        function onStateChanged() { root.updateSnapshot() }
    }
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
            entries: root.sectionFiles.filter(file => file.available !== false)
            onOpenRequested: function(images) { root.imagesRequested(images) }
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
                    metadata: modelData.available === false ? qsTr("Not downloaded") : modelData.metadata1 || ""
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
                text: qsTr("Open")
                tone: LV.AbstractButton.Default
                enabled: !!root.selectedEntry.key && root.selectedEntry.available !== false
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
