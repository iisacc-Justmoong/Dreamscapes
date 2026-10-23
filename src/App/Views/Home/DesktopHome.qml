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
    readonly property real contentHorizontalInset: compact ? LV.Theme.gap12 : LV.Theme.gap24
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
    function showQueue() {
        settingsExpanded = true
        Qt.callLater(function() { root.scrollToItem(storageSlot) })
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
            contentHeight: content.y + content.implicitHeight + 32
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
            LV.VStack {
                id: content
                objectName: "desktopHomeContent"
                x: root.contentHorizontalInset
                y: 20
                width: Math.max(0, viewport.width - root.contentHorizontalInset * 2)
                spacing: LV.Theme.gap20
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
                HomeQueueBar {
                    Layout.fillWidth: true
                    jobs: root.jobs
                    statusText: root.generationStatus
                    onViewQueueRequested: root.showQueue()
                }
                HomeContentRow {
                    id: recentRow
                    objectName: "desktopRecentFiles"
                    Layout.fillWidth: true
                    title: qsTr("Recent files")
                    actionText: qsTr("All files")
                    files: root.recentFiles
                    openFileAction: true
                    loading: root.loading
                    emptyText: qsTr("No recent files")
                    cardsObjectName: "desktopRecentFileCards"
                    itemObjectNamePrefix: "desktopRecentFile"
                    onScrollRequested: function(delta) { root.scrollBy(delta) }
                    onViewAllRequested: root.browseRequested()
                    onOpenFileRequested: root.openFileRequested()
                    onFileRequested: function(file) { root.fileRequested(file) }
                }
                HomeContentRow {
                    objectName: "generationHistory"
                    Layout.fillWidth: true
                    title: qsTr("Recent Generations")
                    actionText: qsTr("Generation history")
                    actionObjectName: "viewAllGenerationHistory"
                    cardsObjectName: "generationHistoryCards"
                    itemObjectNamePrefix: "generationHistoryCard"
                    emptyObjectName: "emptyGenerationHistory"
                    emptyText: qsTr("No generated images yet")
                    files: root.generationHistory
                    imageSelectionEnabled: true
                    loading: root.loading
                    onScrollRequested: function(delta) { root.scrollBy(delta) }
                    onViewAllRequested: root.historyRequested()
                    onFileRequested: function(file) { root.fileRequested(file) }
                    onImagesRequested: function(images) { root.imagesRequested(images) }
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
