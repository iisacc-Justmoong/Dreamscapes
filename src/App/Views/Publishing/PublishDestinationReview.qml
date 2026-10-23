pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import LVRS 1.0 as LV

Item {
    id: root
    required property var review
    required property var draft
    required property var project
    required property var backend
    property string accountId: ""
    property string lastJob: ""
    readonly property var providers: backend.providers
    readonly property var availableAccounts: backend.accounts.filter(a => a.provider === review.platform)
    signal closed()
    function matchingAccount() {
        return availableAccounts.some(a => a.id === accountId) ? accountId : availableAccounts.length ? availableAccounts[0].id : ""
    }
    objectName: "publishDestinationReview"
    RowLayout {
        id: heading
        x: 16; y: 8; width: Math.max(0,root.width-32); height: 48
        spacing: LV.Theme.gap8
        LV.LabelButton { objectName: "publishReviewBack"; text: qsTr("← Content"); tone: LV.AbstractButton.Default; onClicked: root.closed() }
        PublishChoice {
            objectName: "publishReviewPlatform"
            Layout.fillWidth: true
            value: root.review.view.name || "X"
            options: root.providers.map(p => p.name)
            onChosen: next => root.review.platform = root.providers.find(p => p.name === next).id
        }
    }
    Flickable {
        id: viewport
        objectName: "publishReviewScroll"
        x: 16; y: heading.y+heading.height+8; width: Math.max(0,root.width-32); height: Math.max(0,root.height-y-8)
        contentWidth: width; contentHeight: content.implicitHeight
        clip: true; boundsBehavior: Flickable.StopAtBounds
        GridLayout {
            id: content
            width: viewport.width
            columns: width < 800 ? 1 : 2
            columnSpacing: LV.Theme.gap20; rowSpacing: LV.Theme.gap16
            Column {
                id: settings
                Layout.fillWidth: true
                Layout.preferredWidth: content.columns === 1 ? viewport.width : 360
                Layout.alignment: Qt.AlignTop
                spacing: LV.Theme.gap12
                LV.Label { width: parent.width; text: qsTr("%1 · Review").arg(root.review.view.name || ""); style: header; wrapMode: Text.Wrap; sizeToContentHeight: true }
                LV.Label { width: parent.width; text: root.review.view.sharedSummary || ""; style: caption; wrapMode: Text.Wrap; sizeToContentHeight: true }
                LV.Label { width: parent.width; text: qsTr("Shared fields follow Society. Destination edits are saved inside the same .iisc file."); style: description; wrapMode: Text.Wrap; sizeToContentHeight: true }
                LV.LabelButton { objectName: "publishReviewResetAll"; text: qsTr("Reset destination to source"); tone: LV.AbstractButton.Default; onClicked: root.review.reset("") }
                Repeater {
                    model: root.review.view.fields || []
                    delegate: Column {
                        id: field
                        required property var modelData
                        width: settings.width
                        spacing: LV.Theme.gap4
                        RowLayout {
                            width: parent.width
                            LV.Label { Layout.fillWidth: true; text: field.modelData.label; style: caption; wrapMode: Text.Wrap; sizeToContentHeight: true }
                            LV.CheckBox {
                                objectName: "publishReviewCheck-"+field.modelData.key
                                visible: field.modelData.type === "bool"
                                checked: Boolean(field.modelData.value)
                                Accessible.name: field.modelData.label
                                onToggled: root.review.edit(field.modelData.key,checked)
                            }
                            LV.Label { text: field.modelData.inherited ? field.modelData.shared ? qsTr("Society") : qsTr("Auto") : qsTr("Override"); style: caption }
                            LV.LabelButton { text: "↺"; visible: !field.modelData.inherited; tone: LV.AbstractButton.Borderless; Accessible.name: qsTr("Reset %1").arg(field.modelData.label); onClicked: root.review.reset(field.modelData.key) }
                        }
                        PublishField {
                            objectName: "publishReviewField-"+field.modelData.key
                            width: parent.width; visible: field.modelData.type === "text"
                            value: String(field.modelData.value); onEdited: next => root.review.edit(field.modelData.key,next)
                        }
                        PublishChoice {
                            objectName: "publishReviewChoice-"+field.modelData.key
                            width: parent.width; visible: field.modelData.type === "choice"
                            value: String(field.modelData.value); options: field.modelData.options
                            onChosen: next => root.review.edit(field.modelData.key,next)
                        }
                    }
                }
                LV.Label { width: parent.width; text: qsTr("Publishing account"); style: header2 }
                PublishChoice {
                    objectName: "publishReviewAccount"
                    width: parent.width
                    value: root.matchingAccount() || qsTr("No connected account")
                    options: root.availableAccounts.map(a => a.id)
                    onChosen: next => root.accountId = next
                }
                LV.Label { width: parent.width; visible: root.availableAccounts.length === 0; text: qsTr("Connect an account through the native account service to enable uploading."); style: description; wrapMode: Text.Wrap; sizeToContentHeight: true }
                LV.LabelButton {
                    objectName: "publishReviewSubmit"
                    text: root.review.view.supported ? qsTr("Publish to %1").arg(root.review.view.name) : qsTr("Finish on site")
                    tone: LV.AbstractButton.Default
                    enabled: root.review.view.supported ? root.review.view.ready && root.matchingAccount().length > 0 && !root.review.exporting : true
                    onClicked: {
                        if (!root.review.view.supported) Qt.openUrlExternally(root.review.platform === "pixiv" ? "https://www.pixiv.net/" : "https://www.artstation.com/")
                        else root.lastJob = root.review.submit(root.backend,root.matchingAccount())
                    }
                }
                LV.Label { width: parent.width; text: root.review.error || root.backend.errorString || (root.lastJob ? qsTr("Job: %1").arg(root.lastJob) : ""); style: description; wrapMode: Text.Wrap; sizeToContentHeight: true; visible: text.length > 0 }
                Repeater {
                    model: root.backend.jobs.filter(j => j.id === root.lastJob)
                    LV.Label { required property var modelData; width: settings.width; text: modelData.state + (modelData.result ? " · " + modelData.result.message : ""); style: description; wrapMode: Text.Wrap; sizeToContentHeight: true }
                }
            }
            Column {
                id: preview
                Layout.fillWidth: true
                Layout.preferredWidth: content.columns === 1 ? viewport.width : viewport.width-380
                Layout.alignment: Qt.AlignTop
                spacing: LV.Theme.gap12
                LV.Label { objectName: "publishReviewSummary"; width: parent.width; text: root.review.view.summary || ""; style: header2; wrapMode: Text.Wrap; sizeToContentHeight: true }
                Repeater {
                    model: root.review.view.issues || []
                    LV.Label { required property var modelData; width: preview.width; text: (modelData.blocking ? "! " : "· ")+modelData.message; style: description; wrapMode: Text.Wrap; sizeToContentHeight: true }
                }
                LV.LabelButton { objectName: "publishReviewExportFrames"; text: root.review.exporting ? qsTr("Exporting…") : qsTr("Export current frames…"); tone: LV.AbstractButton.Default; enabled: !root.review.exporting; onClicked: exportFolder.open() }
                Repeater {
                    model: root.review.view.steps || []
                    Column {
                        id: step
                        required property var modelData
                        width: preview.width; spacing: LV.Theme.gap8
                        Rectangle { width: parent.width; height: 1; color: LV.Theme.panelBackground10 }
                        LV.Label { width: parent.width; text: qsTr("Post %1 · %2 attachments").arg(step.modelData.index+1).arg(step.modelData.media.length)+(step.modelData.replyTo>=0 ? qsTr(" · Reply to %1").arg(step.modelData.replyTo+1) : ""); style: header2; wrapMode: Text.Wrap; sizeToContentHeight: true }
                        LV.Label { width: parent.width; text: step.modelData.text; style: body; wrapMode: Text.WrapAnywhere; sizeToContentHeight: true }
                        Flow {
                            id: thumbnails
                            width: parent.width; spacing: LV.Theme.gap8
                            Repeater {
                                model: step.modelData.media
                                Column {
                                    id: asset
                                    required property int modelData
                                    readonly property var info: (root.review.view.media || [])[modelData] || ({})
                                    width: Math.min(180,preview.width)
                                    spacing: LV.Theme.gap4
                                    PublishCanvasPreview {
                                        objectName: "publishReviewPreview-"+asset.modelData
                                        width: parent.width; height: 100; project: root.project; sourceIndex: asset.info.page || 0
                                        requested: root.visible && preview.y+step.y+thumbnails.y+asset.y >= viewport.contentY-200 && preview.y+step.y+thumbnails.y+asset.y <= viewport.contentY+viewport.height+200
                                    }
                                    LV.Label { width: parent.width; text: (asset.modelData+1)+" · "+asset.info.name; style: caption; wrapMode: Text.WrapAnywhere; sizeToContentHeight: true }
                                    LV.Label { width: parent.width; text: asset.info.altText || qsTr("No alt text"); style: caption; wrapMode: Text.WrapAnywhere; sizeToContentHeight: true }
                                }
                            }
                        }
                    }
                }
                LV.Label { text: qsTr("Exported media / public URLs"); style: header2 }
                Repeater {
                    model: root.review.view.media || []
                    Column {
                        id: reference
                        required property var modelData
                        width: preview.width; spacing: LV.Theme.gap6
                        LV.Label { width: parent.width; text: reference.modelData.name; style: caption; wrapMode: Text.WrapAnywhere; sizeToContentHeight: true }
                        PublishChoice { width: parent.width; value: reference.modelData.kind; options: ["image","video"]; onChosen: next => root.review.edit("media."+reference.modelData.id+".kind",next) }
                        PublishField { width: parent.width; label: qsTr("Local export path"); value: reference.modelData.localFile; onEdited: next => root.review.edit("media."+reference.modelData.id+".localFile",next) }
                        PublishField { width: parent.width; label: qsTr("Public HTTPS URL"); value: reference.modelData.publicUrl; onEdited: next => root.review.edit("media."+reference.modelData.id+".publicUrl",next) }
                    }
                }
            }
        }
    }
    FolderDialog { id: exportFolder; title: qsTr("Export publication frames"); onAccepted: root.review.exportFrames(selectedFolder) }
    onVisibleChanged: if (!visible) exportFolder.close()
}
