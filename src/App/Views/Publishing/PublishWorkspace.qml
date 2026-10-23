pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

Item {
    id: root
    objectName: "publishWorkspace"
    required property var project
    required property var backend
    readonly property alias draft: draft
    readonly property bool compact: width < 1056
    property string section: "content"
    property bool previewing: false
    property bool reviewing: false
    property string notice: ""
    readonly property var sectionNames: [qsTr("Content"),qsTr("Cover & preview"),qsTr("Ownership"),qsTr("Project details"),qsTr("Rights & safety"),qsTr("Publishing")]
    readonly property var sectionKeys: ["content","cover","ownership","details","rights","publishing"]
    signal backRequested()
    function open() { draft.begin(project); previewing = false; reviewing = false; notice = ""; section = "content" }
    function reviewDestination(platform) { reviewing = true; reviewModel.platform = platform; notice = qsTr("Review inherited content and destination options before uploading.") }
    function saveDraft() { if (draft.path.length) draft.save(project.currentCanvas.localFileSource(draft.path)); else saveDialog.open() }
    function selectSection(key) {
        section = key
        Qt.callLater(function() {
            if (key === "content") document.positionViewAtBeginning()
            else if (key === "ownership") document.positionViewAtEnd()
            else inspectorScroll.contentY = Math.max(0,Math.min(inspectorScroll.contentHeight-inspectorScroll.height,inspector.positionFor(key)))
        })
    }
    PublishDraft { id: draft; objectName: "publishDraft" }
    PublishReview { id: reviewModel; objectName: "publishReview"; draft: root.reviewing ? root.draft : null }
    Flickable {
        id: headerViewport
        objectName: "publishHeader"
        x: 24; y: 24; width: Math.max(0,root.width-48); height: 22
        contentWidth: Math.max(width, root.compact ? 500 : 650); contentHeight: height
        boundsBehavior: Flickable.StopAtBounds; flickableDirection: Flickable.HorizontalFlick; clip: true
        RowLayout {
            width: headerViewport.contentWidth; height: 22; spacing: LV.Theme.gap12
            LV.LabelButton { objectName: "publishBack"; text: qsTr("Back"); Layout.preferredHeight: 22; tone: LV.AbstractButton.Default; onClicked: root.backRequested() }
            LV.Label { text: root.previewing ? qsTr("Preview") : qsTr("New post"); style: header }
            Item { Layout.fillWidth: true }
            LV.LabelButton {
                id: drafts
                objectName: "publishDrafts"
                text: qsTr("Drafts"); Layout.preferredHeight: 22; rightPadding: 22
                tone: LV.AbstractButton.Default
                onClicked: draftsMenu.openFor(drafts,0,height+4)
                Image { anchors.right: parent.right; anchors.rightMargin: 2; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18; source: "Assets/chevron-down.svg"; sourceSize.width: 18; sourceSize.height: 18 }
            }
            LV.Label { objectName: "publishDraftStatus"; visible: !root.compact; text: draft.dirty ? qsTr("Draft · Unsaved changes") : draft.path.length ? qsTr("Draft · All changes saved") : qsTr("Draft · Not saved"); style: caption }
            LV.LabelButton { objectName: "publishPreview"; text: root.previewing ? qsTr("Edit") : qsTr("Preview"); Layout.preferredHeight: 22; tone: LV.AbstractButton.Default; onClicked: root.previewing = !root.previewing }
            LV.LabelButton { objectName: "publishSaveDraft"; text: qsTr("Save draft"); Layout.preferredHeight: 22; tone: LV.AbstractButton.Default; onClicked: root.saveDraft() }
            LV.LabelButton {
                objectName: "publishSubmit"
                text: qsTr("Publish"); Layout.preferredHeight: 22; tone: LV.AbstractButton.Primary
                backgroundColor: LV.Theme.accentGreen
                backgroundColorHover: Qt.darker(LV.Theme.accentGreen,1.12)
                backgroundColorPressed: Qt.darker(LV.Theme.accentGreen,1.2)
                textColor: LV.Theme.windowAlt
                onClicked: root.reviewDestination(reviewModel.platform)
            }
        }
    }
    Rectangle { x: 24; y: headerViewport.y+headerViewport.height+20; width: Math.max(0,root.width-48); height: 1; color: LV.Theme.panelBackground10 }
    PublishChoice {
        id: compactNavigation
        objectName: "publishCompactNavigation"
        x: 24; y: 78; width: Math.max(0,root.width-48); height: visible ? implicitHeight : 0
        visible: root.compact && !root.reviewing; value: root.sectionNames[root.sectionKeys.indexOf(root.section)]; options: root.sectionNames
        onChosen: next => root.selectSection(root.sectionKeys[root.sectionNames.indexOf(next)])
    }
    Item {
        id: authoringBody
        visible: !root.reviewing
        x: (root.width-width)/2; y: root.compact ? compactNavigation.y+compactNavigation.height+20 : 87
        width: Math.min(1328,Math.max(0,root.width-48)); height: Math.max(0,root.height-y-24-footer.height)
        ListView {
            id: document
            objectName: "publishDocument"
            width: root.compact ? authoringBody.width : Math.max(0,authoringBody.width-364)
            height: authoringBody.height
            visible: !root.compact || root.section === "content" || root.section === "ownership"
            model: draft.canvases; spacing: LV.Theme.gap16
            boundsBehavior: Flickable.StopAtBounds; clip: true; cacheBuffer: 200
            header: Column {
                width: document.width; spacing: LV.Theme.gap12
                Row {
                    width: parent.width
                    LV.Label { width: Math.max(0,parent.width-110); text: qsTr("TITLE"); style: description }
                    LV.Label { width: 110; text: qsTr("%1 characters").arg(String(draft.values.title || "").length); style: caption; horizontalAlignment: Text.AlignRight }
                }
                PublishField { objectName: "publishTitle"; width: parent.width; label: ""; value: draft.values.title || ""; placeholder: qsTr("Post title"); readOnly: root.previewing; onEdited: next => draft.edit("title",next) }
                Item { width: 1; height: 4 }
            }
            delegate: Column {
                id: block
                required property int index
                required property string canvasId
                required property string canvasName
                required property int sourceIndex
                required property int canvasWidth
                required property int canvasHeight
                required property string caption
                required property string altText
                objectName: "publishCanvas-" + index
                width: document.width
                padding: 16; spacing: LV.Theme.gap12
                Row {
                    visible: !root.previewing
                    width: parent.width-32; spacing: LV.Theme.gap8
                    Item { width: Math.max(0,parent.width-moveUp.width-moveDown.width-16); height: 22 }
                    LV.LabelButton { id: moveUp; objectName: "publishMoveUp-" + block.index; text: qsTr("Move ↑"); height: 22; tone: LV.AbstractButton.Borderless; textColor: LV.Theme.accentGreen; enabled: block.index > 0; onClicked: draft.moveCanvas(block.index,-1) }
                    LV.LabelButton { id: moveDown; objectName: "publishMoveDown-" + block.index; text: qsTr("Move ↓"); height: 22; tone: LV.AbstractButton.Borderless; textColor: LV.Theme.accentGreen; enabled: block.index+1 < draft.count; onClicked: draft.moveCanvas(block.index,1) }
                }
                PublishCanvasPreview {
                    objectName: "publishCanvasPreview-" + block.index
                    width: Math.max(0,block.width-32)
                    height: width * Math.max(1,block.canvasHeight)/Math.max(1,block.canvasWidth)
                    project: root.project; sourceIndex: block.sourceIndex
                    requested: document.visible && block.y+block.height >= document.contentY && block.y <= document.contentY+document.height+200
                }
                PublishField { objectName: "publishCaption-" + block.index; width: Math.max(0,block.width-32); label: qsTr("Caption"); value: block.caption; placeholder: qsTr("Add a caption"); readOnly: root.previewing; onEdited: next => draft.editCanvas(block.canvasId,"caption",next) }
                PublishField { objectName: "publishAltText-" + block.index; width: Math.max(0,block.width-32); label: qsTr("Alt text"); value: block.altText; placeholder: qsTr("Describe this canvas for accessibility"); readOnly: root.previewing; onEdited: next => draft.editCanvas(block.canvasId,"altText",next) }
            }
            footer: Column {
                width: document.width; spacing: LV.Theme.gap16
                LV.LabelButton { objectName: "publishAddCanvas"; width: parent.width; text: qsTr("Add ↓"); tone: LV.AbstractButton.Default; visible: !root.previewing; onClicked: addMenu.openFor(this,0,height+4) }
                Rectangle {
                    width: parent.width; height: credits.implicitHeight+32
                    color: "transparent"; radius: LV.Theme.radiusMd; border.width: 1; border.color: LV.Theme.panelBackground10
                    Column {
                        id: credits
                        x: 16; y: 16; width: parent.width-32; spacing: LV.Theme.gap8
                        LV.Label { text: qsTr("Credits & source"); style: header2 }
                        PublishField { objectName: "publishCredits"; width: parent.width; label: qsTr("Attribution"); value: draft.values.credits || ""; placeholder: qsTr("Authors, collaborators and attribution"); readOnly: root.previewing; onEdited: next => draft.edit("credits",next) }
                        LV.Label { width: parent.width; text: draft.sourceName + (root.project.filePath.length ? " · " + root.project.filePath : ""); style: caption; wrapMode: Text.WrapAnywhere; sizeToContentHeight: true }
                    }
                }
            }
        }
        Flickable {
            id: inspectorScroll
            objectName: "publishInspectorScroll"
            x: root.compact ? 0 : authoringBody.width-340
            width: root.compact ? authoringBody.width : 340; height: authoringBody.height
            visible: !root.compact || (root.section !== "content" && root.section !== "ownership")
            contentWidth: width; contentHeight: inspector.implicitHeight
            boundsBehavior: Flickable.StopAtBounds; clip: true
            PublishInspector { id: inspector; width: inspectorScroll.width; draft: root.draft; project: root.project; readOnly: root.previewing; providers: root.backend.providers; onReviewRequested: platform => root.reviewDestination(platform) }
        }
    }
    PublishDestinationReview {
        x: authoringBody.x; y: 78; width: authoringBody.width; height: Math.max(0,root.height-y-24-footer.height)
        visible: root.reviewing
        review: reviewModel; draft: root.draft; project: root.project; backend: root.backend
        onClosed: root.reviewing = false
    }
    LV.Label {
        id: footer
        objectName: "publishNotice"
        x: 24; width: Math.max(0,root.width-48); anchors.bottom: parent.bottom; anchors.bottomMargin: 8
        height: visible ? implicitHeight+8 : 0
        visible: text.length > 0
        text: draft.error || root.notice
        style: description; wrapMode: Text.Wrap; sizeToContentHeight: true
    }
    LV.ContextMenu { id: draftsMenu; objectName: "publishDraftsMenu"; showIconSlot: false; items: [qsTr("Open draft…"),qsTr("Save draft as…")]; onItemTriggered: index => index === 0 ? openDialog.open() : saveDialog.open() }
    LV.ContextMenu { id: addMenu; objectName: "publishAddCanvasMenu"; showIconSlot: false; items: [qsTr("Edit source canvases…")]; onItemTriggered: root.backRequested() }
    FileDialog { id: saveDialog; objectName: "publishSaveDialog"; title: qsTr("Save publication canvas"); fileMode: FileDialog.SaveFile; defaultSuffix: "iisc"; nameFilters: [qsTr("Shared canvas (*.iisc)")]; onAccepted: draft.save(selectedFile) }
    FileDialog { id: openDialog; objectName: "publishOpenDialog"; title: qsTr("Open publication canvas"); nameFilters: [qsTr("Shared canvas (*.iisc)")]; onAccepted: draft.load(selectedFile) }
    Shortcut { sequence: StandardKey.Save; enabled: root.visible; onActivated: root.saveDraft() }
    onVisibleChanged: if (!visible) { draftsMenu.close(); addMenu.close(); openDialog.close(); saveDialog.close() }
}
