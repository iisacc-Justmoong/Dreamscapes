pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

LV.VStack {
    id: root
    Layout.minimumWidth: 0
    CanvasPreview { id: canvasPreviews }
    property string title: ""
    property string subtitle: ""
    property string actionText: qsTr("All files")
    property string actionObjectName: objectName + "ViewAll"
    property string cardsObjectName: objectName + "Cards"
    property string itemObjectNamePrefix: objectName + "Card"
    property string emptyObjectName: objectName + "Empty"
    property string emptyText: qsTr("No files yet")
    property var files: []
    property int maximumItems: 20
    property bool loading: false
    property bool openFileAction: false
    property bool imageSelectionEnabled: false
    readonly property var visibleFiles: NativeData.presentation.range(files || [], 0, maximumItems)
    readonly property alias count: cards.count
    readonly property real cardWidth: metrics.implicitWidth
    readonly property real cardHeight: metrics.implicitHeight
    readonly property int visibleColumns: Math.max(1, Math.floor((width + LV.Theme.gap16) / (cardWidth + LV.Theme.gap16)))
    LV.Card { id: metrics; type: LV.Card.File; size: LV.Card.Medium; visible: false }
    signal viewAllRequested()
    signal fileRequested(var file)
    signal openFileRequested()
    signal imagesRequested(var images)
    signal scrollRequested(real delta)
    function activateFile(file) {
        if (imageSelectionEnabled && selectionBar.selectionMode) selectionBar.toggle(file)
        else root.fileRequested(file)
    }
    function focusFirstCard() {
        cards.currentIndex = 0
        cards.positionViewAtBeginning()
        const card = cards.itemAtIndex(0)
        if (card) card.forceActiveFocus()
    }
    spacing: LV.Theme.gap12
    LV.HStack {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        spacing: LV.Theme.gap8
        LV.Label {
            text: root.title
            style: header
            elide: Text.ElideRight
            Layout.minimumWidth: 0
        }
        LV.Label {
            objectName: root.objectName + "Count"
            visible: root.width >= 500
            text: root.imageSelectionEnabled ? qsTr("%1 results").arg(root.visibleFiles.length) : qsTr("%1 files").arg(root.visibleFiles.length)
            style: caption
            color: LV.Theme.descriptionColor
        }
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }
        ImageSelectionBar {
            id: selectionBar
            objectName: root.objectName + "Selection"
            entries: root.imageSelectionEnabled ? root.visibleFiles : []
            visible: (root.width >= 550 && selectionBar.images.length > 1) || selectionBar.selectionMode
            onOpenRequested: function(images) { root.imagesRequested(images) }
        }
        LV.LabelButton {
            objectName: root.objectName + "OpenFile"
            visible: root.openFileAction
            text: qsTr("Open file")
            tone: LV.AbstractButton.Borderless
            onClicked: root.openFileRequested()
        }
        LV.LabelButton {
            objectName: root.actionObjectName
            text: root.actionText
            tone: LV.AbstractButton.Borderless
            onClicked: root.viewAllRequested()
        }
    }
    ListView {
        id: cards
        objectName: root.cardsObjectName
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredHeight: root.cardHeight
        visible: count > 0
        model: root.visibleFiles
        orientation: ListView.Horizontal
        flickableDirection: Flickable.HorizontalFlick
        spacing: root.visibleColumns > 1 && count >= root.visibleColumns
            ? Math.max(LV.Theme.gap16, (width - root.visibleColumns * root.cardWidth) / (root.visibleColumns - 1))
            : LV.Theme.gap16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        keyNavigationEnabled: true
        Keys.onReturnPressed: if (currentIndex >= 0) root.activateFile(root.visibleFiles[currentIndex])
        Keys.onEnterPressed: if (currentIndex >= 0) root.activateFile(root.visibleFiles[currentIndex])
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                currentIndex = event.key === Qt.Key_Home ? 0 : count - 1
                positionViewAtIndex(currentIndex, ListView.Contain)
                event.accepted = true
            }
        }
        // Leave vertical scrolling to the Home viewport; horizontal wheels move this row.
        WheelHandler {
            orientation: Qt.Horizontal
            target: null
            onWheel: function(event) {
                const delta = event.pixelDelta.x || event.angleDelta.x / 120 * 60
                if (delta && cards.contentWidth > cards.width) {
                    cards.contentX = Math.max(cards.originX, Math.min(cards.originX + cards.contentWidth - cards.width, cards.contentX - delta))
                    event.accepted = true
                } else event.accepted = false
            }
        }
        WheelHandler {
            orientation: Qt.Vertical
            target: null
            onWheel: function(event) {
                const delta = event.pixelDelta.y || event.angleDelta.y / 120 * 60
                if (delta) { root.scrollRequested(delta); event.accepted = true }
                else event.accepted = false
            }
        }
        Controls.ScrollBar.horizontal: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
        delegate: LV.Card {
            id: card
            required property int index
            required property var modelData
            objectName: root.itemObjectNamePrefix + index
            type: LV.Card.File
            size: LV.Card.Medium
            detail: LV.Card.Brief
            filename: modelData.name || ""
            description: modelData.description || ""
            metadata: modelData.dateText || modelData.description || ""
            previewSource: root.visible && width > 0 ? canvasPreviews.sourceForFile(modelData) : ""
            selectable: false
            selected: selectionBar.isSelected(modelData)
            showMenu: false
            onClicked: root.activateFile(modelData)
            onActiveFocusChanged: if (activeFocus) {
                cards.currentIndex = index
                cards.positionViewAtIndex(index, ListView.Contain)
            }
        }
    }
    LV.ListItem {
        objectName: root.emptyObjectName
        Layout.fillWidth: true
        visible: root.visibleFiles.length === 0
        type: LV.ListItem.Detail
        label: root.loading ? qsTr("Reading Society…") : root.emptyText
        detail: qsTr("Society content will appear here.")
        showLeadingIcon: false
        showBookmark: false
        showDate: false
        showFolders: false
        showTags: false
    }
}
