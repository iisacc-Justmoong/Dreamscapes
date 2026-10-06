pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.VStack {
    id: root
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
    readonly property var visibleFiles: Array.prototype.slice.call(files || [], 0, maximumItems)
    readonly property alias count: cards.count
    readonly property real cardWidth: width >= 900 ? (width - 4 * 16) / 5 : 180
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
    spacing: 16
    LV.HStack {
        Layout.fillWidth: true
        spacing: 8
        LV.VStack {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 8
            LV.Label {
                text: root.title
                style: header
                font.pixelSize: 18
                font.weight: Font.DemiBold
                font.styleName: "SemiBold"
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                elide: Text.ElideRight
            }
            LV.Label {
                text: root.subtitle
                style: description
                font.pixelSize: 13
                font.weight: Font.Medium
                font.styleName: "Medium"
                color: LV.Theme.descriptionColor
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                sizeToContentHeight: true
            }
        }
        ImageSelectionBar {
            id: selectionBar
            objectName: root.objectName + "Selection"
            entries: root.imageSelectionEnabled ? root.visibleFiles : []
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
        Layout.preferredHeight: 224
        visible: count > 0
        model: root.visibleFiles
        orientation: ListView.Horizontal
        flickableDirection: Flickable.HorizontalFlick
        spacing: 16
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
            width: root.cardWidth
            height: 224
            filename: modelData.name || ""
            description: modelData.description || ""
            metadata: modelData.dateText || modelData.description || ""
            previewSource: root.visible && width > 0 ? modelData.previewSource || "" : ""
            selectable: false
            selected: selectionBar.isSelected(modelData)
            showMenu: false
            opacity: hovered || activeFocus ? 1 : 0.8
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
