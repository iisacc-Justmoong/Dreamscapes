pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import QtQuick.Controls as Controls
import LVRS 1.0 as LV
import "LvrsEditorIcons.js" as LvrsIcons
import ".." as Views
import Dreamscapes.Style 1.0 as Style

Item {
    id: root
    objectName: "editorFilePanel"
    required property var toolState
    readonly property string currentViewId: toolState.fileViewId || "information"
    readonly property var selectedView: NativeData.file.view(currentViewId)
    readonly property string figmaNode: selectedView.figmaNode
    readonly property var information: toolState.projectRuntime && toolState.projectRuntime.currentCanvas === toolState.engine
        ? toolState.projectRuntime.fileInformation : toolState.engine ? toolState.engine.fileInformation : ({})
    readonly property var viewInformation: toolState.engine ? toolState.engine.fileViewInformation : ({})
    readonly property int fieldCount: NativeData.file.sectionRowCount(selectedView)
    readonly property int segmentCount: NativeData.file.views.length
    implicitWidth: 398
    implicitHeight: 766

    function selectView(id) {
        if (!NativeData.file.containsView(id)) return false
        toolState.fileViewId = id
        return true
    }
    function value(key) {
        return NativeData.presentation.fileField(viewInformation, information, key, qsTr("Not available"))
    }
    function sectionTitle(name) {
        if (name === "Sampling pass 01" && Number(information["StableDiffusionMetadata.samplingPasses"]) > 1) return qsTr("Sampling passes")
        if (name === "Model resource 01" && Number(information["StableDiffusionMetadata.models"]) > 1) return qsTr("Model resources")
        if (name === "LoRA 01" && Number(information["StableDiffusionMetadata.loras"]) > 1) return qsTr("LoRAs")
        if (name === "First editor · Account" && information["Authorship.participants"] !== "None recorded" && information["Authorship.participants"] !== "Not available") return qsTr("Recorded editors · Account")
        return name
    }
    onCurrentViewIdChanged: viewport.contentY = 0

    LV.IconSegmentedControl {
        id: segments
        objectName: "editorFileViewSegments"
        forceBorderlessTone: false
        Repeater {
            model: NativeData.file.views
            LV.IconButton {
                id: selectionButton1
                required property var modelData
                objectName: "editorFileView-" + modelData.id
                iconSource: LvrsIcons.resolve(LV.Theme, "File", modelData.icon)
                tone: LV.AbstractButton.Borderless
                Accessible.name: modelData.name
                Accessible.role: Accessible.PageTab
                Accessible.checkable: true
                Accessible.checked: root.currentViewId === modelData.id
                onClicked: root.selectView(modelData.id)

                EditorButtonState { button: selectionButton1; selected: Boolean(root.currentViewId === modelData.id) }
                Accessible.onToggleAction: selectionButton1.click()
            }
        }
    }
    Row {
        id: context
        objectName: "editorFileContext"
        y: segments.height + Style.Tokens.metric("Theme Metrics/Space/16")
        height: 20
        spacing: Style.Tokens.metric("Theme Metrics/Space/8")
        Image {
            objectName: "editorFileContextIcon"
            width: 18; height: 18; y: 1
            source: LvrsIcons.resolve(LV.Theme, "File", root.selectedView.icon)
            fillMode: Image.PreserveAspectFit
            sourceSize: Qt.size(72, 72)
        }
        LV.Label {
            objectName: "editorFileContextTitle"
            text: root.selectedView.name
            style: body
            font.pixelSize: Style.Tokens.metric("Theme Typography/Subheader/textSubheader")
            font.weight: Style.Tokens.metric("Theme Typography/Weight/Medium")
            font.styleName: LV.Theme.textBodyStyleName
            color: Style.Tokens.color("Theme Colors/Text/bodyColor")
            height: 20
            lineHeight: 20
            lineHeightMode: Text.FixedHeight
            textFormat: Text.PlainText
        }
    }
    Flickable {
        id: viewport
        objectName: "editorFileViewport"
        anchors.top: context.bottom
        anchors.topMargin: Style.Tokens.metric("Theme Metrics/Space/16")
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: sections.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        onContentHeightChanged: contentY = Math.max(0, Math.min(contentY, Math.max(0, contentHeight - height)))
        onHeightChanged: contentY = Math.max(0, Math.min(contentY, Math.max(0, contentHeight - height)))
        Controls.ScrollBar.vertical: Controls.ScrollBar { policy: Controls.ScrollBar.AsNeeded }
        Column {
            id: sections
            width: viewport.width
            spacing: Style.Tokens.metric("Theme Metrics/Space/16")
            Repeater {
                model: root.selectedView.sections
                Column {
                    id: section
                    required property var modelData
                    width: sections.width
                    spacing: Style.Tokens.metric("Theme Metrics/Space/8")
                    Rectangle { width: section.width; height: 1; color: Style.Tokens.color("Theme Colors/Panel/surfaceRaised") }
                    LV.Label {
                        text: root.sectionTitle(section.modelData.name)
                        style: body
                        font.pixelSize: Style.Tokens.metric("Theme Typography/Subheader/textSubheader")
                        font.weight: Style.Tokens.metric("Theme Typography/Weight/Medium")
                        font.styleName: LV.Theme.textBodyStyleName
                        color: Style.Tokens.color("Theme Colors/Text/titleHeaderColor")
                        width: section.width
                        height: 20
                        lineHeight: 20
                        lineHeightMode: Text.FixedHeight
                        textFormat: Text.PlainText
                    }
                    Column {
                        width: section.width
                        spacing: Style.Tokens.metric("Theme Metrics/Space/8")
                        Repeater {
                            model: section.modelData.rows
                            Item {
                                id: field
                                required property var modelData
                                readonly property string bindingKey: modelData.key
                                objectName: "editorFileRow-" + bindingKey
                                width: section.width
                                height: Math.max(22, fieldValue.implicitHeight)
                                Views.PanelRow {
                                    objectName: "editorFileLabel-" + field.bindingKey
                                    width: Math.min(152, Math.max(0, (field.width - 12) * 0.4))
                                    height: 22
                                    minItemWidth: 0
                                    miniItemWidth: 0
                                    label: field.modelData.key === "Alias.version" && !/^[0-9a-f]{64}$/i.test(root.value(field.bindingKey)) ? qsTr("File version") : field.modelData.label
                                    showLeadingIcon: false
                                    showDescription: false
                                    showValue: false
                                    showTrailingIcon: false
                                    showPrimaryAction: false
                                    showSecondaryAction: false
                                    showMoreMenu: false
                                    navigationEnabled: false
                                }
                                LV.Label {
                                    id: fieldValue
                                    objectName: "editorFileValue-" + field.bindingKey
                                    x: Math.min(152, Math.max(0, (field.width - 12) * 0.4)) + 12
                                    y: Math.max(0, (22 - implicitHeight) / 2)
                                    width: Math.max(0, field.width - x)
                                    text: root.value(field.bindingKey)
                                    style: body
                                    color: Style.Tokens.color("Theme Colors/Text/bodyColor")
                                    wrapMode: Text.Wrap
                                    lineHeight: 13
                                    lineHeightMode: Text.FixedHeight
                                    textFormat: Text.PlainText
                                    Accessible.name: field.modelData.label + ": " + text
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
