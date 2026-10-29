pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import LVRS 1.0 as LV

Column {
    id: root
    required property var draft
    required property var project
    property bool readOnly: false
    property var providers: []
    signal reviewRequested(string platform)
    spacing: LV.Theme.gap20
    function positionFor(section) {
        const sections = {cover: cover, details: details, rights: rights, publishing: publishing}
        return sections[section] ? sections[section].y : 0
    }
    function coverPage() { draft.values; draft.count; return draft.coverPage() }
    component Field: PublishField {
        property string field
        width: root.width
        value: root.draft.values[field] || ""
        readOnly: root.readOnly
        onEdited: next => root.draft.edit(field, next)
    }
    component Choice: PublishChoice {
        property string field
        width: root.width
        value: root.draft.values[field] || ""
        enabled: !root.readOnly
        onChosen: next => root.draft.edit(field, next)
    }
    component Check: LV.CheckBox {
        property string field
        width: root.width
        checked: Boolean(root.draft.values[field])
        enabled: !root.readOnly
        checkedColor: LV.Theme.accentGreen
        useFigmaCheckedAssets: true
        checkedAssetSourceEnabled: Qt.resolvedUrl("Assets/checkbox.svg")
        onToggled: root.draft.edit(field, checked)
    }
    component Heading: LV.Label { width: root.width; style: header2 }
    component Divider: Rectangle { width: root.width; height: 1; color: LV.Theme.panelBackground10 }
    Column {
        id: cover
        objectName: "publishCoverSection"
        width: parent.width; spacing: LV.Theme.gap12
        Heading { text: qsTr("Cover & preview") }
        PublishCanvasPreview { objectName: "publishCoverPreview"; width: root.width; height: width * 0.52; project: root.project; sourceIndex: root.coverPage() }
        PublishChoice {
            objectName: "publishCoverChoice"
            width: root.width; label: qsTr("Cover canvas"); enabled: !root.readOnly
            property var names: {
                const result = []
                for (let i = 0; i < root.draft.count; ++i) result.push((i+1) + " / " + root.draft.canvas(i).name)
                return result
            }
            options: names
            value: {
                for (let i = 0; i < root.draft.count; ++i) if (root.draft.canvas(i).id === root.draft.values.coverId) return (i+1) + " / " + root.draft.canvas(i).name
                return ""
            }
            onChosen: next => {
                const index = NativeData.presentation.indexOf(names, next)
                if (index >= 0) root.draft.edit("coverId", root.draft.canvas(index).id)
            }
        }
    }
    Column {
        id: details
        objectName: "publishDetailsSection"
        width: parent.width; spacing: LV.Theme.gap12
        Heading { text: qsTr("Project details") }
        Field { objectName: "publishDescription"; field: "description"; label: qsTr("Description"); placeholder: qsTr("Describe this work") }
        Choice { objectName: "publishContentType"; field: "contentType"; label: qsTr("Content type"); options: ["Artwork","Environment concept","Illustration","Photography","Animation","Video","Design"] }
        Choice { objectName: "publishTopic"; field: "topic"; label: qsTr("Topic"); options: ["Environment art / Worldbuilding","Character art","Architecture","Photography","Motion design","Graphic design","Other"] }
        Field { objectName: "publishSoftware"; field: "software"; label: qsTr("Software") }
        Field { id: tags; objectName: "publishTags"; field: "tags"; label: qsTr("Tags"); placeholder: qsTr("Separate tags with commas") }
        Flow {
            width: root.width; spacing: LV.Theme.gap4
            Repeater {
                model: { root.draft.values; return root.draft.tags() }
                Rectangle {
                    id: tag
                    required property string modelData
                    width: tagLabel.implicitWidth + 16; height: 20; radius: 10
                    color: Qt.rgba(LV.Theme.accentGreen.r, LV.Theme.accentGreen.g, LV.Theme.accentGreen.b, 0.25)
                    LV.Label { id: tagLabel; anchors.centerIn: parent; text: tag.modelData; style: caption; color: LV.Theme.accentGreen }
                }
            }
        }
    }
    Divider {}
    Column {
        id: rights
        objectName: "publishRightsSection"
        width: parent.width; spacing: LV.Theme.gap12
        Heading { text: qsTr("Rights & safety") }
        Choice { objectName: "publishLicense"; field: "license"; label: qsTr("License"); options: ["All rights reserved","CC BY 4.0","CC BY-SA 4.0","CC BY-NC 4.0","CC0"] }
        Check { objectName: "publishAdult"; field: "adult"; text: qsTr("Contains adult content") }
        Check { objectName: "publishViolence"; field: "violence"; text: qsTr("Contains graphic violence") }
        Check { objectName: "publishChildlike"; field: "childlike"; text: qsTr("Contains childlike elements") }
        Check { objectName: "publishPeople"; field: "realPeople"; text: qsTr("Contains real people") }
    }
    Divider {}
    Column {
        id: publishing
        objectName: "publishPublishingSection"
        width: parent.width; spacing: LV.Theme.gap12
        Heading { text: qsTr("Publishing") }
        Choice { objectName: "publishTime"; field: "publishTime"; label: qsTr("Publish time"); options: ["Publish immediately","Schedule"] }
        Field { objectName: "publishScheduledAt"; field: "scheduledAt"; label: qsTr("Scheduled time"); placeholder: "YYYY-MM-DD HH:MM UTC"; visible: root.draft.values.publishTime === "Schedule"; height: visible ? implicitHeight : 0 }
        Choice { objectName: "publishComments"; field: "comments"; label: qsTr("Who can comment"); options: ["Everyone","Followers","Nobody"] }
        Check { objectName: "publishReactionCounts"; field: "reactionCounts"; text: qsTr("Show reaction counts") }
        Check { objectName: "publishCommentCounts"; field: "commentCounts"; text: qsTr("Show comment counts") }
        Field { id: mentions; objectName: "publishMentions"; field: "mentions"; label: qsTr("@Mention"); placeholder: qsTr("@user1, @user2") }
        LV.IconButton {
            objectName: "publishAddMention"
            width: root.width; height: 22; iconSize: 18
            tone: LV.AbstractButton.Default
            enabled: !root.readOnly
            iconSource: Qt.resolvedUrl("Assets/add.svg")
            Accessible.name: qsTr("Add mention")
            onClicked: { mentions.inputItem.forceActiveFocus(); mentions.inputItem.cursorPosition = mentions.inputItem.text.length }
        }
    }
    Divider {}
    Column {
        width: parent.width; spacing: LV.Theme.gap8
        Heading { text: qsTr("Destinations") }
        Repeater {
            model: root.providers
            Row {
                id: destination
                required property var modelData
                width: root.width; spacing: LV.Theme.gap8
                LV.Label { width: Math.max(0,parent.width-reviewButton.width-8); text: destination.modelData.name; style: body }
                LV.LabelButton { id: reviewButton; objectName: "publishReview-"+destination.modelData.id; text: qsTr("Review"); tone: LV.AbstractButton.Default; onClicked: root.reviewRequested(destination.modelData.id) }
            }
        }
    }
}
