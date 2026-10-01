pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.VStack {
    id: root
    objectName: "desktopPromptStarters"
    readonly property var starters: [
        {tag: "PRODUCT", title: qsTr("Make the ordinary iconic"), prompt: qsTr("A sculptural object, soft studio light and a simple backdrop.")},
        {tag: "SCENE", title: qsTr("Build a place to escape"), prompt: qsTr("A quiet landscape, gentle atmosphere and room to breathe.")},
        {tag: "CAMPAIGN", title: qsTr("Find your visual voice"), prompt: qsTr("A graphic composition with strong shapes and a warm palette.")}
    ]
    signal promptRequested(string prompt)
    spacing: 16
    LV.HStack {
        Layout.fillWidth: true
        spacing: 8
        LV.VStack {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 8
            LV.Label {
                text: qsTr("A starting point, not a blank page")
                style: header
                font.pixelSize: 18
                font.weight: Font.DemiBold
                font.styleName: "SemiBold"
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                elide: Text.ElideRight
            }
            LV.Label {
                text: qsTr("Use a prompt, then make it your own.")
                style: description
                color: LV.Theme.descriptionColor
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                sizeToContentHeight: true
            }
        }
        LV.LabelButton {
            id: browseButton
            objectName: "homeBrowsePrompts"
            text: qsTr("Browse prompts")
            tone: LV.AbstractButton.Borderless
            onClicked: browseMenu.openFor(browseButton, 0, browseButton.height + 2)
        }
    }
    GridLayout {
        id: promptCards
        Layout.fillWidth: true
        columns: root.width >= 700 ? 3 : 1
        columnSpacing: 16
        rowSpacing: 16
        activeFocusOnTab: true
        Repeater {
            model: root.starters
            delegate: Rectangle {
                id: card
                required property int index
                required property var modelData
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                implicitHeight: Math.max(170, contents.implicitHeight + 32)
                radius: 12
                color: LV.Theme.panelBackground08
                LV.VStack {
                    id: contents
                    alignment: Qt.AlignLeft
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 16
                    LV.Label { text: card.modelData.tag; style: description; font.pixelSize: 11; font.weight: Font.DemiBold; color: LV.Theme.descriptionColor }
                    LV.Label { text: card.modelData.title; style: header; font.pixelSize: 18; font.weight: Font.DemiBold; font.styleName: "SemiBold"; Layout.fillWidth: true; wrapMode: Text.Wrap; sizeToContentHeight: true }
                    LV.Label { text: card.modelData.prompt; style: body; Layout.fillWidth: true; wrapMode: Text.Wrap; sizeToContentHeight: true; color: LV.Theme.descriptionColor }
                    LV.LabelButton {
                        objectName: "homeUsePrompt" + card.index
                        text: qsTr("Use prompt")
                        tone: LV.AbstractButton.Borderless
                        onClicked: root.promptRequested(card.modelData.prompt)
                    }
                }
            }
        }
    }
    LV.ContextMenu {
        id: browseMenu
        objectName: "homePromptMenu"
        showIconSlot: false
        itemWidth: Math.max(0, Math.min(300, root.width - leftPadding - rightPadding - edgeMargin * 2))
        items: root.starters.map(function(entry) { return entry.title })
        onItemTriggered: function(index, entry) { root.promptRequested(root.starters[index].prompt) }
    }

}
