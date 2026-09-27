pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV
import "EditorToolDefinitions.js" as Definitions

LV.Sheet {
    id: root
    objectName: "editorToolSheet"
    property string toolId: "elements"
    readonly property var definition: Definitions.tool(toolId)
    property var settingsByTool: ({})
    readonly property var values: settingsByTool[toolId] || Definitions.defaults(definition)
    property string notice: ""
    property string pendingTool: ""
    property string colorField: ""
    signal actionRequested(string toolId, string fieldId, var values)

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
    }
    function resetTool() {
        const all = Object.assign({}, settingsByTool)
        all[toolId] = Definitions.defaults(definition)
        settingsByTool = all
        notice = ""
    }
    function dismiss() {
        pendingTool = ""
        colorSheet.close()
        actionSheet.close()
        close()
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
            definition: root.definition
            values: root.values
            onEdited: function(fieldId, value) { root.setValue(fieldId, value) }
            onResetRequested: root.resetTool()
            onCloseRequested: root.dismiss()
            onActionRequested: function(fieldId, label) {
                root.actionRequested(root.toolId, fieldId, root.values)
                root.notice = qsTr("%1 is not connected to canvas editing yet.").arg(label)
                actionSheet.title = label
                actionSheet.open()
            }
            onColorRequested: function(fieldId) {
                root.colorField = fieldId
                colorSheet.open()
                colorSheet.loadedContent.previousColor = root.values[fieldId]
                colorSheet.loadedContent.setColor(root.values[fieldId])
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
