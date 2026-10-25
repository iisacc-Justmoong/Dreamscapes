pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV

Item {
    id: root
    objectName: "desktopHome"
    property var recentFiles: []
    property var recentPublished: []
    property var generationHistory: []
    property var jobs: []
    property string generationStatus: ""
    property bool settingsExpanded: false
    property bool loading: false
    property string errorText: ""
    property string selectedAction: "home"
    property string browserSection: ""
    readonly property bool compact: width < LV.Theme.scaleMetric(700)
    readonly property real contentHorizontalInset: compact ? LV.Theme.gap12 : LV.Theme.gap16
    readonly property bool wideWorkspace: viewport.width >= 1040
    readonly property var selectedFile: recentFilesTable.selectedFile
    property string modelName: ""
    property string deviceName: qsTr("Automatic")
    readonly property alias quickGenerateContainer: promptSlot
    readonly property alias storageContainer: storageSlot
    readonly property alias imageWorkspaceContainer: imageWorkspaceSlot
    readonly property alias videoWorkspaceContainer: videoWorkspaceSlot
    readonly property alias storageBrowserContainer: storageBrowserSlot
    signal quickActionRequested(string action)
    signal browseRequested()
    signal publishedRequested()
    signal historyRequested()
    signal fileRequested(var file)
    signal openFileRequested()
    signal imagesRequested(var images)
    signal promptRequested(string prompt)
    signal homeRequested()
    signal queueRequested()
    function showQueue() {
        root.queueRequested()
    }
    function choosePrompt(prompt) { showHome(); root.promptRequested(prompt) }

    function scrollToItem(target) {
        viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height,
            target.mapToItem(content, 0, 0).y))
    }
    function scrollBy(delta) { viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height, viewport.contentY - delta)) }
    function showHome() { viewport.contentY = 0; selectedAction = "home"; root.homeRequested() }
    function activate(action) {
        selectedAction = action
        if (action === "home") showHome()
        else if (action === "files") root.browseRequested()
        else if (action === "assets") { root.homeRequested(); settingsExpanded = true; Qt.callLater(function() { root.scrollToItem(storageSlot) }) }
        else if (action === "history") root.historyRequested()
        else {
            if (action === "image" || action === "video" || action === "audio" || action === "board")
                viewport.contentY = 0
            root.quickActionRequested(action)
        }
    }

    LV.HStack {
        anchors.fill: parent
        spacing: 0
        DesktopHomeSidebar {
            compact: root.compact
            selectedAction: root.selectedAction
            Layout.preferredWidth: listWidth
            Layout.minimumWidth: listWidth
            Layout.maximumWidth: listWidth
            Layout.fillHeight: true
            onActionRequested: function(action) { root.activate(action) }
        }
        Flickable {
            id: viewport
            objectName: "desktopHomeViewport"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.browserSection && root.selectedAction !== "image" && root.selectedAction !== "video"
            contentWidth: width
            contentHeight: content.y + content.implicitHeight + LV.Theme.gap16
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
            LV.VStack { alignment: Qt.AlignLeft;
                id: content
                objectName: "desktopHomeContent"
                x: root.contentHorizontalInset
                y: LV.Theme.gap16
                width: Math.max(0, viewport.width - root.contentHorizontalInset * 2)
                spacing: LV.Theme.gap12
                LV.HStack {
                    Layout.fillWidth: true
                    LV.Label { text: qsTr("Workspace"); style: header }
                    LV.Label { text: qsTr("Create and continue"); style: caption; color: LV.Theme.descriptionColor; visible: content.width > 500 }
                    Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
                    LV.LabelButton { tone: LV.AbstractButton.Default; objectName: "homeOpenFile"; text: qsTr("Open file"); onClicked: root.openFileRequested() }
                    LV.LabelButton { tone: LV.AbstractButton.Default; objectName: "homeNewCanvas"; text: qsTr("New canvas"); onClicked: root.activate("canvas") }
                }
                Item {
                    id: promptSlot
                    objectName: "desktopQuickGenerateSlot"
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                }
                LV.Label {
                    objectName: "desktopHomeError"
                    visible: text.length > 0
                    text: root.errorText
                    style: caption
                    color: LV.Theme.accentRed
                    wrapMode: Text.Wrap
                    sizeToContentHeight: true
                    Layout.fillWidth: true
                }
                GridLayout {
                    id: workArea
                    objectName: "homeFileWorkspace"
                    Layout.fillWidth: true
                    columns: root.wideWorkspace ? 2 : 1
                    columnSpacing: LV.Theme.gap12
                    rowSpacing: LV.Theme.gap12
                    readonly property real panelHeight: root.wideWorkspace
                        ? Math.max(360, viewport.height - promptSlot.height - 284) : 360
                    Item {
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        Layout.preferredWidth: 700; Layout.preferredHeight: workArea.panelHeight
                        HomeRecentFiles {
                            id: recentFilesTable
                            anchors.fill: parent; anchors.margins: LV.Theme.gap12
                            files: root.recentFiles
                            loading: root.loading
                            onViewAllRequested: root.browseRequested()
                            onOpenFileRequested: root.openFileRequested()
                            onFileRequested: function(file) { root.fileRequested(file) }
                        }
                    }
                    Item {
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        Layout.preferredWidth: 515; Layout.preferredHeight: workArea.panelHeight
                        HomeSelectedFile {
                            anchors.fill: parent; anchors.margins: LV.Theme.gap12
                            file: recentFilesTable.selectedFile
                            onFileRequested: function(file) { root.fileRequested(file) }
                        }
                    }
                }
                GridLayout {
                    objectName: "homeSessionWorkspace"
                    Layout.fillWidth: true
                    columns: root.wideWorkspace ? 2 : 1
                    columnSpacing: LV.Theme.gap12
                    rowSpacing: LV.Theme.gap12
                    HomeQueueBar {
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        Layout.preferredWidth: 526; Layout.preferredHeight: 160
                        jobs: root.jobs
                        statusText: root.generationStatus
                        modelName: root.modelName
                        deviceName: root.deviceName
                        onViewQueueRequested: root.showQueue()
                    }
                    Item {
                        Layout.fillWidth: true; Layout.minimumWidth: 0
                        Layout.preferredWidth: 689; Layout.preferredHeight: 160
                        HomeRecentGenerations {
                            anchors.fill: parent; anchors.margins: LV.Theme.gap12
                            files: root.generationHistory
                            loading: root.loading
                            onViewAllRequested: root.historyRequested()
                            onFileRequested: function(file) { root.fileRequested(file) }
                            onImagesRequested: function(images) { root.imagesRequested(images) }
                        }
                    }
                }
                LV.HStack {
                    objectName: "homeSessionStatus"
                    Layout.preferredHeight: LV.Theme.controlHeightSm
                    Layout.fillWidth: true
                    LV.Label { text: root.generationStatus || qsTr("Session · Idle"); style: caption; color: LV.Theme.descriptionColor; elide: Text.ElideRight; Layout.maximumWidth: content.width / 2 }
                    Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
                    LV.Label { text: qsTr("Storage · Society drive"); style: caption; color: LV.Theme.descriptionColor }
                }
                LV.LabelButton {
                    objectName: "desktopGenerationSettings"
                    visible: root.settingsExpanded
                    text: qsTr("Hide generation settings")
                    tone: LV.AbstractButton.Borderless
                    onClicked: root.settingsExpanded = !root.settingsExpanded
                }
                Item {
                    id: storageSlot
                    visible: root.settingsExpanded
                    objectName: "desktopStorageSlot"
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                }
            }
        }
        Item {
            id: storageBrowserSlot
            objectName: "desktopStorageBrowserSlot"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.browserSection.length > 0
        }
        Item {
            id: videoWorkspaceSlot
            objectName: "desktopVideoWorkspaceSlot"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.selectedAction === "video"
        }
        Item {
            id: imageWorkspaceSlot
            objectName: "desktopImageWorkspaceSlot"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.selectedAction === "image"
        }
    }
}
