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
    property bool settingsExpanded: false
    property bool loading: false
    property string errorText: ""
    property string selectedAction: "home"
    readonly property bool compact: width < LV.Theme.scaleMetric(700)
    readonly property real contentHorizontalInset: compact ? LV.Theme.gap12 : LV.Theme.gap24
    readonly property alias quickGenerateContainer: promptSlot
    readonly property alias storageContainer: storageSlot
    readonly property alias imageWorkspaceContainer: imageWorkspaceSlot
    readonly property alias videoWorkspaceContainer: videoWorkspaceSlot
    signal quickActionRequested(string action)
    signal browseRequested()
    signal publishedRequested()
    signal historyRequested()
    signal fileRequested(var file)
    signal openFileRequested()
    signal imagesRequested(var images)
    signal promptRequested(string prompt)
    function choosePrompt(prompt) { showHome(); root.promptRequested(prompt) }

    function scrollToItem(target) {
        viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height,
            target.mapToItem(content, 0, 0).y))
    }
    function scrollBy(delta) { viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height, viewport.contentY - delta)) }
    function showHome() { viewport.contentY = 0; selectedAction = "home" }
    function activate(action) {
        selectedAction = action
        if (action === "home") showHome()
        else if (action === "files") scrollToItem(recentRow)
        else if (action === "assets") { settingsExpanded = true; Qt.callLater(function() { root.scrollToItem(storageSlot) }) }
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
            visible: root.selectedAction !== "image" && root.selectedAction !== "video"
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
                spacing: LV.Theme.gap24
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
                HomeContentRow {
                    id: recentRow
                    objectName: "desktopRecentFiles"
                    Layout.fillWidth: true
                    title: qsTr("Continue creating")
                    subtitle: qsTr("Pick up a draft or open a recent project.")
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
                    title: qsTr("Recent generations")
                    subtitle: qsTr("Every variation stays here. Choose what to take forward.")
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
                HomeContentRow {
                    id: stylesRow
                    objectName: "desktopStyles"
                    Layout.fillWidth: true
                    title: qsTr("Explore a direction")
                    subtitle: qsTr("A new visual language for your next project.")
                    actionText: qsTr("Explore styles")
                    itemObjectNamePrefix: "desktopStyle"
                    files: [
                        {name: qsTr("Quiet landscapes"), description: qsTr("Soft tones · Natural light"), previewSource: Qt.resolvedUrl("Assets/Directions/landscape.svg"), prompt: "A quiet landscape, soft tones and natural light."},
                        {name: qsTr("Objects in orbit"), description: qsTr("Studio · Sculptural"), previewSource: Qt.resolvedUrl("Assets/Directions/studio.svg"), prompt: "A sculptural object in orbit, soft studio lighting and a simple backdrop."},
                        {name: qsTr("Modern editorial"), description: qsTr("Graphic · Bold shapes"), previewSource: Qt.resolvedUrl("Assets/Directions/poster.svg"), prompt: "A modern editorial composition, bold graphic shapes and a warm palette."},
                        {name: qsTr("Organic abstraction"), description: qsTr("Texture · Muted color"), previewSource: Qt.resolvedUrl("Assets/Directions/fluid.svg"), prompt: "Organic abstraction, tactile textures and muted colors."},
                        {name: qsTr("Architectural calm"), description: qsTr("Warm light · Minimal"), previewSource: Qt.resolvedUrl("Assets/Directions/fluid.svg"), prompt: "Minimal architecture, warm natural light and a calm atmosphere."}
                    ]
                    onScrollRequested: function(delta) { root.scrollBy(delta) }
                    onViewAllRequested: { root.scrollToItem(stylesRow); stylesRow.focusFirstCard() }
                    onFileRequested: function(file) { root.choosePrompt(file.prompt) }
                }
                HomePromptStarters {
                    Layout.fillWidth: true
                    onPromptRequested: function(prompt) { root.choosePrompt(prompt) }
                }
                HomeContentRow {
                    objectName: "desktopPublishedList"
                    Layout.fillWidth: true
                    title: qsTr("From your workspace")
                    subtitle: qsTr("Published ideas and collections worth revisiting.")
                    actionText: qsTr("All published")
                    actionObjectName: "desktopPublishedViewAll"
                    maximumItems: 4
                    files: root.recentPublished
                    loading: root.loading
                    emptyText: qsTr("No published work yet")
                    onScrollRequested: function(delta) { root.scrollBy(delta) }
                    onViewAllRequested: root.publishedRequested()
                    onFileRequested: function(file) { root.fileRequested(file) }
                }
                LV.LabelButton {
                    objectName: "desktopGenerationSettings"
                    text: root.settingsExpanded ? qsTr("Hide generation settings") : qsTr("Generation settings")
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
