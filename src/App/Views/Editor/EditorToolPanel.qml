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
    signal edited(string fieldId, var value)
    signal resetRequested()
    signal closeRequested()
    signal actionRequested(string fieldId, string label)
    signal colorRequested(string fieldId)
    spacing: 12
    readonly property int fieldCount: definition ? definition.fields.length : 0

    LV.HStack {
        width: parent.width
        spacing: 8
        LV.Label {
            Layout.fillWidth: true
            text: root.definition ? String(root.definition.number).padStart(2, "0") + "  " + root.definition.title : ""
            style: header
            font.pixelSize: 16
            wrapMode: Text.Wrap
            sizeToContentHeight: true
        }
        LV.LabelButton {
            objectName: "editorToolReset"
            text: qsTr("Reset")
            tone: LV.AbstractButton.Default
            onClicked: root.resetRequested()
        }
        LV.IconButton {
            objectName: "editorToolClose"
            iconName: "generalclose"
            tone: LV.AbstractButton.Default
            implicitWidth: 32
            implicitHeight: 32
            Accessible.name: qsTr("Close tool panel")
            onClicked: root.closeRequested()
        }
    }
    Loader {
        width: parent.width
        active: Boolean(root.definition && root.definition.selector)
        visible: active
        sourceComponent: EditorToolControl {
            field: root.definition.selector
            value: root.values.selector
            onEdited: function(value) { root.edited("selector", value) }
        }
    }
    Column {
        width: parent.width
        spacing: 10
        Repeater {
            model: Definitions.rows(root.definition, root.width < 340)
            Row {
                id: row
                required property var modelData
                width: parent.width
                spacing: 8
                Repeater {
                    model: row.modelData
                    EditorToolControl {
                        required property var modelData
                        width: root.width >= 340 && (field.type === "Slider" || field.type === "Toggle")
                            && field.label.length <= 25 ? (row.width - 8) / 2 : row.width
                        field: modelData
                        value: root.values[field.id]
                        onEdited: function(value) { root.edited(field.id, value) }
                        onRequested: root.actionRequested(field.id, field.label)
                        onColorRequested: root.colorRequested(field.id)
                    }
                }
            }
        }
    }
    Flow {
        id: actions
        width: parent.width
        spacing: 6
        Repeater {
            model: root.definition ? root.definition.fields.filter(function(field) { return field.type === "Action" }) : []
            LV.LabelButton {
                required property var modelData
                required property int index
                objectName: "editorAction-" + modelData.id
                text: modelData.label
                height: 24
                width: Math.min(implicitWidth, actions.width)
                tone: index === 0 ? LV.AbstractButton.Primary : LV.AbstractButton.Default
                onClicked: root.actionRequested(modelData.id, modelData.label)
            }
        }
    }
}
