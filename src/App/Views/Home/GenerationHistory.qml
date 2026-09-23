pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.VStack {
    id: root
    objectName: "generationHistory"
    property var files: []
    property bool loading: false
    property string errorText: ""
    signal viewAllRequested()
    spacing: LV.Theme.gap12

    LV.HStack {
        Layout.fillWidth: true
        Layout.preferredHeight: LV.Theme.controlHeightSm
        LV.Label {
            Layout.fillWidth: true
            Layout.leftMargin: LV.Theme.gap4
            text: qsTr("Generate history")
            style: body
            elide: Text.ElideRight
        }
        LV.LabelButton {
            objectName: "viewAllGenerationHistory"
            text: qsTr("View all")
            tone: LV.AbstractButton.Borderless
            onClicked: root.viewAllRequested()
        }
    }
    ListView {
        id: cards
        objectName: "generationHistoryCards"
        Layout.fillWidth: true
        implicitHeight: LV.Theme.scaleMetric(160)
        visible: count > 0
        model: root.files
        orientation: ListView.Horizontal
        spacing: LV.Theme.gap8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        keyNavigationEnabled: true
        Keys.onReturnPressed: root.viewAllRequested()
        Keys.onEnterPressed: root.viewAllRequested()
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
        // Vertical mouse wheels also move the row; touch keeps native flicking.
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
            objectName: "generationHistoryCard" + index
            type: LV.Card.File
            size: LV.Card.Small
            detail: LV.Card.Brief
            width: LV.Theme.scaleMetric(140)
            height: LV.Theme.scaleMetric(160)
            filename: modelData.name
            description: modelData.description || ""
            metadata: modelData.dateText
            previewSource: modelData.previewSource || ""
            selectable: false
            showMenu: false
            onClicked: root.viewAllRequested()
            onActiveFocusChanged: if (activeFocus) {
                cards.currentIndex = index
                cards.positionViewAtIndex(index, ListView.Contain)
            }
        }
    }
    LV.ListItem {
        objectName: "emptyGenerationHistory"
        visible: root.files.length === 0
        Layout.fillWidth: true
        type: LV.ListItem.Detail
        showLeadingIcon: false
        label: root.loading ? qsTr("Reading Society…") : qsTr("No generated images yet")
        detail: qsTr("Images saved in Society will appear here.")
        showBookmark: false
        showDate: false
        showFolders: false
        showTags: false
    }
    LV.Label {
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.errorText
        style: caption
        wrapMode: Text.Wrap
    }
}
