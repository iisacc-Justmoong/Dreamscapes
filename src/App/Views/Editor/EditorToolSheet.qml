pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV
import "EditorToolDefinitions.js" as Definitions

LV.Sheet {
    id: root
    objectName: "editorToolSheet"
    property string toolId: "elements"
    property var engine: null
    property var generationRuntime: null
    signal resetRequested(string toolId, var defaults)
    readonly property var definition: {
        const catalog = Definitions.tool(toolId)
        const source = engine ? Definitions.runtimeDefinition(catalog) : catalog
        if (!source || toolId !== "generative" || !generationRuntime) return source
        const runtime = Object.assign({}, source)
        runtime.fields = source.fields.map(function(field) {
            if (field.id !== "field-2") return field
            return Object.assign({}, field, {initial: "Auto", options: ["Auto"].concat(generationRuntime.models.map(function(model) { return model.id })), optionLabels: [qsTr("Current model")].concat(generationRuntime.models.map(function(model) { return model.name || model.id }))})
        })
        return runtime
    }
    property var settingsByTool: ({})
    readonly property var values: {
        const stored = settingsByTool[toolId] || Definitions.defaults(definition)
        if (!engine) return stored
        const actual = Object.assign({}, stored)
        if (toolId === "layers") {
            actual["field-1"] = engine.generationSeed || qsTr("Unknown")
            const layer = engine.selectedLayer
            if (layer.id) {
                actual["field-8"] = layer.lock
                actual["field-9"] = layer.preserveAlpha
                actual["field-10"] = layer.opacity * 100
                actual["field-20"] = layer.blend
                actual["field-19"] = layer.name
            }
        } else if (toolId === "canvas") actual["field-0"] = [engine.canvasWidth, engine.canvasHeight]
        return actual
    }
    property string notice: ""
    property string pendingTool: ""
    property string colorField: ""
    readonly property bool modalActive: visible || colorSheet.visible || actionSheet.visible
    signal actionRequested(string toolId, string fieldId, var values)
    signal valueEdited(string toolId, string fieldId, var value)

    presentation: LV.Sheet.Mobile
    detent: LV.Sheet.Fit
    cornerRadius: 16
    contentPadding: 16
    showHeader: false
    // CanvasEditor is already inset by Main for the keyboard and system safe area.
    topSafeInset: 0
    bottomSafeInset: 0

    function openTool(key) {
        if (!Definitions.tool(key)) return
        if (visible) {
            pendingTool = key
            close()
            return
        }
        toolId = key
        notice = ""
        open()
    }
    function setValue(fieldId, value) {
        const next = Object.assign({}, values)
        next[fieldId] = value
        const all = Object.assign({}, settingsByTool)
        all[toolId] = next
        settingsByTool = all
        valueEdited(toolId, fieldId, value)
    }
    function resetTool() {
        const all = Object.assign({}, settingsByTool)
        all[toolId] = Definitions.defaults(definition)
        settingsByTool = all
        notice = ""
        resetRequested(toolId, Definitions.defaults(definition))
    }
    function chooseColor(fieldId) {
        root.colorField = fieldId
        colorSheet.open()
        colorSheet.loadedContent.previousColor = root.values[fieldId]
        colorSheet.loadedContent.setColor(root.values[fieldId])
    }
    function dismiss() {
        pendingTool = ""
        colorSheet.close()
        actionSheet.close()
        close()
    }
    function showActionStatus(label, message) {
        notice = message
        actionSheet.title = label
        actionSheet.open()
    }
    onClosed: {
        colorSheet.close()
        actionSheet.close()
        if (pendingTool) {
            Qt.callLater(function() {
                if (!root.pendingTool) return
                const next = root.pendingTool
                root.pendingTool = ""
                root.openTool(next)
            })
        }
    }
    contentComponent: visible ? panelComponent : null
    Component {
        id: panelComponent
        EditorToolPanel {
            engine: root.engine
            notice: root.notice
            definition: root.definition
            values: root.values
            onEdited: function(fieldId, value) { root.setValue(fieldId, value) }
            onResetRequested: root.resetTool()
            onCloseRequested: root.dismiss()
            onActionRequested: function(fieldId, label) {
                root.actionRequested(root.toolId, fieldId, root.values)
            }
            onColorRequested: function(fieldId) {
                root.chooseColor(fieldId)
            }
        }
    }
    LV.Sheet {
        id: actionSheet
        objectName: "editorActionSheet"
        parent: root.parent
        presentation: LV.Sheet.Mobile
        description: root.notice
        topSafeInset: 0
        bottomSafeInset: 0
    }
    LV.Sheet {
        id: colorSheet
        objectName: "editorColorSheet"
        parent: root.parent
        presentation: LV.Sheet.Mobile
        title: qsTr("Color")
        topSafeInset: 0
        bottomSafeInset: 0
        contentPadding: 16
        contentComponent: LV.ColorPicker {
            objectName: "editorColorPicker"
            onAccepted: function(color) {
                root.setValue(root.colorField, String(color).toUpperCase())
                colorSheet.close()
            }
            onCanceled: colorSheet.close()
        }
    }
}
