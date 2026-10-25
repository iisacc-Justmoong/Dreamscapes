pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import LVRS 1.0 as LV

Column {
    id: root
    objectName: "editorCompactPanel"
    required property var toolState
    readonly property string toolId: toolState.toolId
    readonly property var definition: toolState.definition
    readonly property var values: toolState.values
    readonly property var viewCatalog: NativeData.compact.viewsFor(toolId)
    readonly property string currentViewId: {
        const saved = NativeData.compact.view(toolId, toolState.compactViewsByTool[toolId])
        const operation = viewCatalog.find(function(view) { return view.mode === values.selector })
        return saved && (!saved.mode || saved.mode === values.selector) ? saved.id
            : operation ? operation.id : viewCatalog.length ? viewCatalog[0].id : ""
    }
    readonly property var selectedView: NativeData.compact.view(toolId, currentViewId)
    readonly property var presentedFields: selectedView ? selectedView.fields.map(function(row) {
        const field = definition.fields.find(function(entry) { return entry.id === row.id })
        // Live model choices come from the generation runtime, not the atlas examples.
        if (toolId === "generative" && row.id === "field-2" && toolState.generationRuntime)
            return Object.assign({}, field, {icon: row.icon, height: row.height})
        return Object.assign({}, field, row)
    }) : []
    readonly property int fieldCount: presentedFields.length
    spacing: 12
    width: 398

    function selectView(id) {
        const view = NativeData.compact.view(toolId, id)
        if (!view) return
        const next = Object.assign({}, toolState.compactViewsByTool)
        next[toolId] = id
        toolState.compactViewsByTool = next
        if (view.mode && values.selector !== view.mode) toolState.setValue("selector", view.mode)
    }
    function capability(field) {
        if (field.unsupported) return {supported: false, enabled: false, reason: qsTr("This option is unavailable in the connected editing engine.")}
        if (!toolState.engine) return {supported: true, enabled: true, reason: ""}
        const revision = toolState.engine.revision
        const state = toolState.engine.toolState
        return toolState.engine.toolControlState(toolId, field.id, values)
    }
    function request(field) { toolState.actionRequested(toolId, field.id, values) }
    function resetCurrentView() {
        const view = selectedView
        toolState.resetTool()
        if (view && view.mode) toolState.setValue("selector", view.mode)
    }

    Row {
        width: parent.width
        height: 30
        Item {
            width: Math.max(0, parent.width - 30)
            height: parent.height
            clip: true
            Row {
                spacing: 8
                Repeater {
                    // Generative uses two groups of four, exactly as the source view.
                    model: root.toolId === "generative" ? 2 : 1
                    LV.IconSegmentedControl {
                        id: group
                        required property int index
                        objectName: "compactNavigation-" + index
                        forceBorderlessTone: false
                        Repeater {
                            model: root.toolId === "generative" ? root.viewCatalog.slice(group.index * 4, (group.index + 1) * 4) : root.viewCatalog
                            LV.IconButton {
                                id: tab
                                required property var modelData
                                objectName: "compactTab-" + modelData.id
                                width: 22; height: 22; iconSize: 18
                                horizontalPadding: 2; verticalPadding: 2
                                iconSource: LV.Theme.iconPath(modelData.icon)
                                tone: LV.AbstractButton.Borderless
                                Accessible.name: modelData.title
                                Accessible.checkable: true
                                Accessible.checked: root.currentViewId === modelData.id
                                LV.Tooltip {
                                    target: tab
                                    text: tab.modelData.title
                                }
                                onClicked: root.selectView(modelData.id)
                                EditorButtonState { button: tab; selected: root.currentViewId === tab.modelData.id }
                                Accessible.onToggleAction: tab.click()
                            }
                        }
                    }
                }
            }
        }
        Item { width: 8; height: 1 }
        LV.IconButton {
            id: resetButton
            objectName: "editorToolReset"
            y: 4
            width: 22; height: 22; iconSize: 18
            horizontalPadding: 2; verticalPadding: 2
            iconSource: LV.Theme.iconPath("reset")
            tone: LV.AbstractButton.Borderless
            Accessible.name: qsTr("Reset")
            LV.Tooltip {
                target: resetButton
                text: resetButton.Accessible.name
            }
            onClicked: root.resetCurrentView()
        }
    }
    Row {
        objectName: "compactCurrentContext"
        width: parent.width; height: 20; spacing: 8
        Image {
            y: 1; width: 18; height: 18
            source: root.selectedView ? LV.Theme.iconPath(root.selectedView.icon) : ""
            sourceSize: Qt.size(72, 72)
        }
        LV.Label {
            width: parent.width - 26; height: 20
            style: body; font.pixelSize: 15; font.weight: Font.Medium
            text: root.selectedView ? root.selectedView.title : ""
            elide: Text.ElideRight
        }
    }
    Column {
        width: parent.width; spacing: 12
        Repeater {
            model: root.presentedFields
            Loader {
                id: row
                required property var modelData
                width: parent.width
                readonly property var availability: root.capability(modelData)
                sourceComponent: modelData.type === "Action" ? action : control
                Component {
                    id: control
                    EditorToolControl {
                        width: row.width
                        field: row.modelData
                        value: root.values[field.id] === undefined ? field.initial : root.values[field.id]
                        sampleLayout: true
                        compactLayout: true
                        capability: row.availability
                        onEdited: function(value) { root.toolState.setValue(field.id, value) }
                        onRequested: root.request(field)
                        onColorRequested: root.toolState.chooseColor(field.id)
                    }
                }
                Component {
                    id: action
                    Row {
                        height: 22; spacing: 8
                        enabled: row.availability.enabled !== false
                        opacity: enabled ? 1 : 0.48
                        LV.IconButton {
                            objectName: "editorAction-" + row.modelData.id
                            width: 22; height: 22; iconSize: 18
                            horizontalPadding: 2; verticalPadding: 2
                            iconSource: LV.Theme.iconPath(row.modelData.icon)
                            tone: LV.AbstractButton.Default
                            Accessible.name: row.modelData.label
                            Accessible.description: row.availability.reason || ""
                            onClicked: root.request(row.modelData)
                        }
                        LV.Label {
                            width: Math.max(0, row.width - 30); height: 22
                            style: caption; color: LV.Theme.bodyColor
                            text: row.modelData.label
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
    LV.Label {
        visible: root.toolState.notice.length > 0
        width: parent.width
        text: root.toolState.notice
        style: caption; wrapMode: Text.Wrap; sizeToContentHeight: true
    }
}
