pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import ".." as Frames
import LVRS 1.0 as LV
import ".." as Shared

Frames.PanelSheet {
    id: root
    objectName: "editorToolSheet"
    surfaceColor: "transparent"
    property alias toolId: nativeState.toolId
    property var engine: null
    property var projectRuntime: null
    property var generationRuntime: null
    property alias fileViewId: nativeState.fileViewId
    readonly property alias definition: nativeState.definition
    property alias settingsByTool: nativeState.settingsByTool
    property alias graphicViewsByTool: nativeState.graphicViewsByTool
    property alias compactViewsByTool: nativeState.compactViewsByTool
    property alias elementsViewId: nativeState.elementsViewId
    property alias elementsMultipleSelection: nativeState.elementsMultipleSelection
    property alias elementsSelectionScope: nativeState.elementsSelectionScope
    readonly property alias values: nativeState.values
    property alias notice: nativeState.notice
    property string pendingTool: ""
    property string colorField: ""
    readonly property bool modalActive: visible || colorSheet.visible || actionSheet.visible
    signal resetRequested(string toolId, var defaults)
    signal actionRequested(string toolId, string fieldId, var values)
    signal valueEdited(string toolId, string fieldId, var value)
    signal sourceRequested(string toolId, var values)
    readonly property alias nativeModel: nativeState
    NativeEditorToolState {
        id: nativeState
        objectName: "editorNativeToolState"
        catalog: NativeData
        engine: root.engine
        generationModels: root.generationRuntime ? root.generationRuntime.models : []
        onValueEdited: (tool, field, value) => root.valueEdited(tool, field, value)
        onResetRequested: (tool, defaults) => root.resetRequested(tool, defaults)
        onSourceRequested: (tool, values) => root.sourceRequested(tool, values)
    }
    function syncElementSettings(forceShapeView) { nativeState.syncElementSettings(forceShapeView === true) }

    presentation: LV.Sheet.Mobile
    detent: toolId === "file" ? LV.Sheet.Large : LV.Sheet.Fit
    scrollContent: toolId !== "file"
    cornerRadius: 0
    contentPadding: 16
    showHeader: false
    // CanvasEditor is already inset by Main for the keyboard and system safe area.
    topSafeInset: 0
    bottomSafeInset: 0

    function openTool(key) {
        if (!NativeData.tools.tool(key)) return
        if (visible) {
            pendingTool = key
            close()
            return
        }
        toolId = key
        notice = ""
        open()
    }
    function setValue(fieldId, value) { nativeState.setValue(fieldId, value) }
    function setGraphicValue(operation, parameter, value) { nativeState.setGraphicValue(operation, parameter, value) }
    function setGraphicValues(operation, patch) { nativeState.setGraphicValues(operation, patch) }
    function replaceGraphicCollection(operation, path, items) { nativeState.replaceGraphicCollection(operation, path, items) }
    function resetGraphicView(operation, defaults) { nativeState.resetGraphicView(operation, defaults) }
    function resetTool() { nativeState.resetTool() }
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
    contentComponent: visible ? (toolId === "file" ? filePanelComponent : toolId === "background" ? backgroundPanelComponent : toolId === "camera-photo" ? photoPanelComponent : toolId === "elements" ? elementsPanelComponent : ["color", "auto-enhance", "masking", "effects"].indexOf(toolId) >= 0 ? graphicPanelComponent : toolId === "text" ? textPanelComponent : toolId === "layers" && engine ? layerPanelComponent : NativeData.compact.tool(toolId) ? compactPanelComponent : panelComponent) : null
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
    Frames.PanelSheet {
        id: actionSheet
        objectName: "editorActionSheet"
        parent: root.parent
        presentation: LV.Sheet.Mobile
        description: root.notice
        topSafeInset: 0
        bottomSafeInset: 0
    }
    Frames.PanelSheet {
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
