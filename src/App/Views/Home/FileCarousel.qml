pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.VStack {
    id: root
    property string title: ""
    property var files: []
    property int maximumItems: 20
    property string itemObjectNamePrefix: "fileCard"
    property string cardsObjectName: "fileCards"
    property bool loading: false
    property string emptyText: qsTr("No files yet")
    readonly property var visibleFiles: Array.prototype.slice.call(files || [], 0, maximumItems)
    signal viewAllRequested()
    signal fileRequested(var file)

    implicitHeight: LV.Theme.scaleMetric(194)
    spacing: LV.Theme.gap12

    LV.HStack {
        Layout.fillWidth: true
        Layout.preferredHeight: LV.Theme.controlHeightSm

        LV.Label {
            Layout.fillWidth: true
            Layout.leftMargin: LV.Theme.gap4
            text: root.title
            style: body
            elide: Text.ElideRight
        }

        LV.LabelButton {
            objectName: root.objectName + "ViewAll"
            text: qsTr("View all")
            tone: LV.AbstractButton.Borderless
            onClicked: root.viewAllRequested()
        }
    }

    ListView {
        id: cards
        objectName: root.cardsObjectName
        Layout.fillWidth: true
        Layout.preferredHeight: LV.Theme.scaleMetric(160)
        visible: count > 0
        model: root.visibleFiles
        orientation: ListView.Horizontal
        spacing: LV.Theme.gap8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        keyNavigationEnabled: true
        Keys.onReturnPressed: if (currentIndex >= 0) root.fileRequested(root.visibleFiles[currentIndex])
        Keys.onEnterPressed: if (currentIndex >= 0) root.fileRequested(root.visibleFiles[currentIndex])
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Home) {
                currentIndex = 0
                positionViewAtBeginning()
                event.accepted = true
            } else if (event.key === Qt.Key_End) {
                currentIndex = count - 1
                positionViewAtEnd()
                event.accepted = true
            }
        }
        WheelHandler {
            target: null
            onWheel: function(event) {
                const delta = event.pixelDelta.x !== 0 ? event.pixelDelta.x
                    : event.pixelDelta.y !== 0 ? event.pixelDelta.y
                    : (event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y) / 120 * 60
                if (delta !== 0 && cards.contentWidth > cards.width) {
                    cards.contentX = Math.max(cards.originX, Math.min(cards.originX + cards.contentWidth - cards.width,
                        cards.contentX - delta))
                    event.accepted = true
                } else event.accepted = false
            }
        }
        Controls.ScrollBar.horizontal: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }

        delegate: LV.Card {
            id: card
            required property int index
            required property var modelData
            objectName: root.itemObjectNamePrefix + index
            type: LV.Card.File
            size: LV.Card.Small
            detail: LV.Card.Brief
            width: LV.Theme.scaleMetric(140)
            height: LV.Theme.scaleMetric(160)
            filename: modelData.name || ""
            description: modelData.description || ""
            metadata: modelData.dateText || ""
            previewSource: modelData.previewSource || ""
            selectable: false
            showMenu: false
            onClicked: root.fileRequested(modelData)
            onActiveFocusChanged: if (activeFocus) {
                cards.currentIndex = index
                cards.positionViewAtIndex(index, ListView.Contain)
            }
        }
    }

    LV.ListItem {
        objectName: root.objectName + "Empty"
        visible: root.visibleFiles.length === 0
        Layout.fillWidth: true
        type: LV.ListItem.Detail
        showLeadingIcon: false
        label: root.loading ? qsTr("Reading Society…") : root.emptyText
        detail: qsTr("Society content will appear here.")
        showBookmark: false
        showDate: false
        showFolders: false
        showTags: false
    }
}
