pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV
import ".." as Shared
import "EditorToolDefinitions.js" as Definitions
import "EditorGraphicDefinitions.js" as Graphics
import "EditorGraphicState.js" as GraphicState
import "EditorCompactDefinitions.js" as Compact

LV.Sheet {
    id: root
    objectName: "editorToolSheet"
    property string toolId: "elements"
    property var engine: null
    property var projectRuntime: null
    property string fileViewId: "information"
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
    property var graphicViewsByTool: ({})
    property var compactViewsByTool: ({})
    property string elementsViewId: "rectangle"
    property bool elementsMultipleSelection: true
    property string elementsSelectionScope: "All shapes"
    property string inspectedElementId: ""
    property string inspectedElementShape: ""
    readonly property var values: {
        const stored = settingsByTool[toolId] || Definitions.defaults(definition)
        if (!engine) return stored
        const actual = Object.assign({}, stored)
        if (toolId === "elements" && engine.elementSelectionCount) {
            Object.assign(actual, engine.selectedElementSettings)
        } else if (toolId === "layers") {
            actual["field-1"] = engine.generationSeed || qsTr("Unknown")
            const layer = engine.selectedLayer
            if (layer.id) {
                actual["field-8"] = layer.lock
                actual["field-9"] = layer.preserveAlpha
                actual["field-10"] = layer.opacity * 100
                actual["field-20"] = layer.blend
                actual["field-19"] = layer.name
            }
        } else if (toolId === "canvas") actual["field-0"] = [engine.editingWidth, engine.editingHeight]
        return actual
    }
    property string notice: ""
    property string pendingTool: ""
    property string colorField: ""
    readonly property bool modalActive: visible || colorSheet.visible || actionSheet.visible
    signal actionRequested(string toolId, string fieldId, var values)
    signal valueEdited(string toolId, string fieldId, var value)

    function syncElementSettings(forceShapeView) {
        if (!engine) { inspectedElementId = ""; inspectedElementShape = ""; return }
        const selected = engine.selectedElementSettings
        if (!selected || !Object.keys(selected).length) { inspectedElementId = ""; inspectedElementShape = ""; return }
        const next = Object.assign({}, Definitions.defaults(Definitions.tool("elements")), selected)
        // Tool transitions also notify selection. Do not invalidate the values
        // binding again when it already presents these same accepted settings.
        if (JSON.stringify(settingsByTool.elements) !== JSON.stringify(next)) {
            const all = Object.assign({}, settingsByTool)
            all.elements = next
            settingsByTool = all
        }
        const selectedId = engine.selectedLayerId
        if ((forceShapeView || selectedId !== inspectedElementId || selected.selector !== inspectedElementShape)
            && ["rectangle", "ellipse", "polygon", "line"].indexOf(elementsViewId) >= 0) {
            const shapeView = {"Rectangle": "rectangle", "Ellipse": "ellipse", "Polygon": "polygon", "Line / Arrow": "line"}
            elementsViewId = shapeView[selected.selector] || "inspect"
        }
        inspectedElementId = selectedId
        inspectedElementShape = selected.selector || ""
    }
    onEngineChanged: syncElementSettings()
    Connections {
        target: root.engine
        // configureTool can emit this during the values binding's change handler.
        // Read the final native selection after that binding has finished.
        function onElementSelectionChanged() { Qt.callLater(root.syncElementSettings) }
    }

    presentation: LV.Sheet.Mobile
    detent: toolId === "file" ? LV.Sheet.Large : LV.Sheet.Fit
    scrollContent: toolId !== "file"
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
    function setGraphicValue(operation, parameter, value) {
        const patch = {}; patch[parameter] = value
        setGraphicValues(operation, patch)
    }
    function setGraphicValues(operation, patch) {
        const next = Object.assign({}, values)
        const drafts = Object.assign({}, next.graphicDrafts || {})
        drafts[operation] = Object.assign({}, drafts[operation] || {}, patch)
        next.graphicDrafts = drafts
        next.graphicOperation = operation
        next.graphicParameters = GraphicState.nested(Graphics.operationDefaults(operation), drafts[operation])
        const all = Object.assign({}, settingsByTool)
        all[toolId] = next
        settingsByTool = all
        valueEdited(toolId, "graphicDrafts", next.graphicDrafts)
        valueEdited(toolId, "graphicOperation", next.graphicOperation)
        valueEdited(toolId, "graphicParameters", next.graphicParameters)
    }
    function replaceGraphicCollection(operation, path, items) {
        const old = (values.graphicDrafts || {})[operation] || {}
        const cleaned = {}
        Object.keys(old).forEach(function(key) { if (key !== path && !key.startsWith(path + "[") && !key.startsWith(path + "[]")) cleaned[key] = old[key] })
        cleaned[path] = items
        const next = Object.assign({}, values)
        const drafts = Object.assign({}, next.graphicDrafts || {})
        drafts[operation] = cleaned
        next.graphicDrafts = drafts
        next.graphicOperation = operation
        next.graphicParameters = GraphicState.nested(Graphics.operationDefaults(operation), cleaned)
        const all = Object.assign({}, settingsByTool); all[toolId] = next; settingsByTool = all
        valueEdited(toolId, "graphicDrafts", next.graphicDrafts)
        valueEdited(toolId, "graphicOperation", next.graphicOperation)
        valueEdited(toolId, "graphicParameters", next.graphicParameters)
    }
    function resetGraphicView(operation, defaults) {
        const next = Object.assign({}, values)
        const drafts = Object.assign({}, next.graphicDrafts || {})
        const retained = {}
        const keys = Object.keys(defaults)
        Object.keys(drafts[operation] || {}).forEach(function(key) {
            if (!keys.some(function(parent) { return Array.isArray(defaults[parent]) && key.startsWith(parent + "[") })) retained[key] = drafts[operation][key]
        })
        drafts[operation] = Object.assign({}, retained, defaults)
        next.graphicDrafts = drafts
        next.graphicOperation = operation
        next.graphicParameters = GraphicState.nested(Graphics.operationDefaults(operation), drafts[operation])
        const all = Object.assign({}, settingsByTool)
        all[toolId] = next
        settingsByTool = all
        valueEdited(toolId, "graphicDrafts", next.graphicDrafts)
        valueEdited(toolId, "graphicOperation", next.graphicOperation)
        valueEdited(toolId, "graphicParameters", next.graphicParameters)
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
    contentComponent: visible ? (toolId === "file" ? filePanelComponent : toolId === "background" ? backgroundPanelComponent : toolId === "camera-photo" ? photoPanelComponent : toolId === "elements" ? elementsPanelComponent : ["color", "auto-enhance", "masking", "effects"].indexOf(toolId) >= 0 ? graphicPanelComponent : toolId === "text" ? textPanelComponent : toolId === "layers" && engine ? layerPanelComponent : Compact.tool(toolId) ? compactPanelComponent : panelComponent) : null
    Component {
        id: compactPanelComponent
        EditorCompactPanel { toolState: root }
    }
    Component {
        id: filePanelComponent
        EditorFilePanel { toolState: root }
    }
    Component {
        id: backgroundPanelComponent
        EditorBackgroundPanel { toolState: root }
    }
    Component {
        id: photoPanelComponent
        EditorCameraPhotoPanel { toolState: root }
    }
    Component {
        id: elementsPanelComponent
        EditorElementsPanel { toolState: root }
    }
    Component {
        id: graphicPanelComponent
        EditorGraphicPanel { toolState: root }
    }
    Component {
        id: textPanelComponent
        EditorTextPanel { toolState: root }
    }
    Component {
        id: layerPanelComponent
        Item {
            readonly property Item layerSheetPresenter: layerSheetLoader.item as Item
            implicitWidth: 398
            implicitHeight: layerSheetPresenter ? layerSheetPresenter.implicitHeight : 525
            Loader {
                id: layerSheetLoader
                anchors.fill: parent
                Component.onCompleted: setSource(Qt.resolvedUrl("EditorLayersPanel.qml"), {toolState: root})
            }
        }
    }
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
        contentComponent: Shared.ColorPicker {
            objectName: "editorColorPicker"
            onAccepted: function(color) {
                root.setValue(root.colorField, String(color).toUpperCase())
                colorSheet.close()
            }
            onCanceled: colorSheet.close()
        }
    }
}
