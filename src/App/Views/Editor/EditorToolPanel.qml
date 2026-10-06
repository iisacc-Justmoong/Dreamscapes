pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV
import "EditorToolDefinitions.js" as Definitions

Column {
    id: root
    objectName: "editorToolPanel"
    required property var definition
    required property var values
    property bool sampleLayout: false
    property var engine: null
    property string notice: ""
    function capability(field) {
        if (!engine) return ({enabled: true, reason: ""})
        const revision = engine.revision
        const state = engine.toolState
        return engine.toolControlState(definition.key, field, values)
    }
    signal edited(string fieldId, var value)
    signal resetRequested()
    signal closeRequested()
    signal actionRequested(string fieldId, string label)
    signal colorRequested(string fieldId)
    spacing: sampleLayout ? 16 : 12
    readonly property var presentedFields: {
        if (!definition) return []
        if (!engine) return definition.fields
        return Definitions.applicableFields(definition, values).filter(function(field) {
            return root.capability(field.id).supported
        })
    }
    readonly property int fieldCount: presentedFields.length

    LV.HStack {
        width: parent.width
        spacing: 8
        Loader {
            Layout.fillWidth: true
            sourceComponent: root.sampleLayout ? sampleHeader : normalHeader
        }
        LV.LabelButton {
            visible: Boolean(root.engine)
            text: qsTr("Info")
            Accessible.name: qsTr("Tool availability")
            tone: LV.AbstractButton.Default
            onClicked: root.actionRequested("capabilities", qsTr("Tool availability"))
        }
        LV.LabelButton {
            objectName: "editorToolReset"
            text: qsTr("Reset")
            tone: LV.AbstractButton.Default
            onClicked: root.resetRequested()
        }
        LV.IconButton {
            objectName: "editorToolClose"
            visible: !root.sampleLayout
            iconName: "generalclose"
            tone: LV.AbstractButton.Default
            implicitWidth: 32
            implicitHeight: 32
            Accessible.name: qsTr("Close tool panel")
            onClicked: root.closeRequested()
        }
    }
    Component {
        id: sampleHeader
        LV.ListItem {
            type: LV.ListItem.Mini
            label: root.definition ? String(root.definition.number).padStart(2, "0") + "  " + root.definition.title : ""
            showLeadingIcon: false
            minItemWidth: 0
            miniItemWidth: 0
        }
    }
    Component {
        id: normalHeader
        LV.Label {
            text: root.definition ? String(root.definition.number).padStart(2, "0") + "  " + root.definition.title : ""
            style: header
            font.pixelSize: 16
            wrapMode: Text.Wrap
            sizeToContentHeight: true
        }
    }
    Loader {
        width: parent.width
        active: Boolean(root.definition && root.definition.selector)
        visible: active
        sourceComponent: EditorToolControl {
            sampleLayout: root.sampleLayout
            field: root.definition.selector || { id: "selector", type: "Choices", label: "", options: [], initial: "" }
            value: root.values.selector === undefined ? field.initial : root.values.selector
            onEdited: function(value) { root.edited("selector", value) }
        }
    }
    Column {
        width: parent.width
        spacing: root.sampleLayout ? 16 : 10
        Repeater {
            model: Definitions.rows({fields: root.presentedFields}, root.sampleLayout || root.width < 340)
            Row {
                id: row
                required property var modelData
                width: parent.width
                spacing: 8
                Repeater {
                    model: row.modelData
                    EditorToolControl {
                        required property var modelData
                        sampleLayout: root.sampleLayout
                        width: !root.sampleLayout && root.width >= 340 && (field.type === "Slider" || field.type === "Toggle")
                            && field.label.length <= 25 ? (row.width - 8) / 2 : row.width
                        field: modelData
                        capability: root.capability(field.id)
                        value: root.values[field.id] === undefined ? field.initial : root.values[field.id]
                        onEdited: function(value) { root.edited(field.id, value) }
                        onRequested: root.actionRequested(field.id, field.label)
                        onColorRequested: root.colorRequested(field.id)
                    }
                }
            }
        }
    }
    Item {
        id: actions
        width: parent.width
        readonly property real spacing: root.sampleLayout ? 8 : 6
        property int layoutRevision: 0
        readonly property var layout: {
            const revision = layoutRevision
            const widths = []
            for (let i = 0; i < actionOptions.count; ++i) {
                const slot = actionOptions.itemAt(i)
                widths.push(slot ? slot.implicitWidth : 0)
            }
            return Definitions.buttonRows(width, widths,
                root.sampleLayout ? root.definition.desktopActionColumns || 2 : actionOptions.count,
                root.sampleLayout ? 22 : 24, spacing, root.sampleLayout)
        }
        implicitHeight: layout.height
        Repeater {
            id: actionOptions
            model: root.presentedFields.filter(function(field) { return field.type === "Action" })
            onItemAdded: Qt.callLater(function() { actions.layoutRevision++ })
            onItemRemoved: Qt.callLater(function() { actions.layoutRevision++ })
            Item {
                id: actionSlot
                required property var modelData
                required property int index
                readonly property var placement: actions.layout.items[index] || {x: 0, y: 0, width: 0}
                x: placement.x
                y: placement.y
                width: placement.width
                implicitWidth: action.implicitWidth
                height: root.sampleLayout ? 22 : 24
                LV.LabelButton {
                    id: action
                    objectName: "editorAction-" + actionSlot.modelData.id
                    text: actionSlot.modelData.label
                    height: parent.height
                    width: Math.min(implicitWidth, parent.width)
                    tone: actionSlot.index === 0 ? LV.AbstractButton.Primary : LV.AbstractButton.Default
                    enabled: root.capability(actionSlot.modelData.id).enabled
                    Accessible.description: root.capability(actionSlot.modelData.id).reason || ""
                    onClicked: root.actionRequested(actionSlot.modelData.id, actionSlot.modelData.label)
                }
            }
        }
    }
}
