pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

Item {
    id: root
    required property var canvas
    required property string family
    required property var definition
    readonly property string section: definition.family || family
    readonly property var values: canvas.viewOptions[section]
    readonly property bool modalActive: menu.visible
    width: parent ? parent.width : 0
    height: definition.kind === "preview" ? 84 : definition.kind === "choices" ? (definition.label ? 39 : 24) : definition.kind === "toggles" ? 24 : 26
    function edit(key, value) { return canvas.viewOptions.edit(section, key, value) }

    Row {
        visible: root.definition.kind === "toggles"
        width: parent.width; height: 24; spacing: 8
        Repeater {
            model: root.definition.kind === "toggles" ? root.definition.entries : []
            Item {
                id: toggle
                required property var modelData
                width: (root.width - (root.definition.entries.length - 1) * 8) / root.definition.entries.length
                height: 24
                LV.Label { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 42; text: toggle.modelData[1]; style: body; elide: Text.ElideRight }
                LV.ToggleSwitch {
                    objectName: "viewOption-" + root.section + "-" + toggle.modelData[0]
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    trackWidth: 38; trackHeight: 22; implicitHeight: 22
                    checked: Boolean(root.values[toggle.modelData[0]])
                    Accessible.name: toggle.modelData[1]
                    onToggled: root.edit(toggle.modelData[0], checked)
                }
            }
        }
    }
    Column {
        visible: root.definition.kind === "choices"
        width: parent.width; spacing: 4
        LV.Label { visible: Boolean(root.definition.label); height: visible ? 11 : 0; style: caption; text: root.definition.label || "" }
        Row {
            width: parent.width; height: 24; spacing: 4
            Repeater {
                model: root.definition.kind === "choices" ? root.definition.options : []
                LV.LabelButton {
                    id: choice
                    required property var modelData
                    readonly property var value: modelData.value === undefined ? modelData : modelData.value
                    readonly property bool selected: modelData.active ? NativeData.presentation.indexOf(modelData.active, root.values[root.definition.key]) >= 0 : root.values[root.definition.key] === value
                    objectName: "viewOption-" + root.family + "-" + root.definition.key + "-" + value
                    width: (root.width - (root.definition.options.length - 1) * 4) / root.definition.options.length
                    height: 24
                    text: modelData.label === undefined ? String(modelData) : modelData.label
                    tone: LV.AbstractButton.Default
                    Accessible.name: text
                    EditorButtonState { button: choice; selected: choice.selected }
                    onClicked: root.edit(root.definition.key, value)
                }
            }
        }
    }
    Row {
        visible: root.definition.kind === "fields"
        width: parent.width; height: 26; spacing: 8
        Repeater {
            model: root.definition.kind === "fields" ? root.definition.entries : []
            Item {
                id: entry
                required property var modelData
                width: (root.width - (root.definition.entries.length - 1) * 8) / root.definition.entries.length
                height: 26
                LV.Label { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; width: parent.width - input.width - (swatch.visible ? 26 : 4); text: entry.modelData[1]; style: caption; elide: Text.ElideRight }
                Rectangle {
                    id: swatch
                    visible: entry.modelData[0] === "color"
                    x: 30; anchors.verticalCenter: parent.verticalCenter
                    width: 18; height: 18; radius: LV.Theme.radiusControl
                    color: root.values.color || LV.Theme.primary
                }
                LV.InputField {
                    id: input
                    objectName: "viewOption-" + root.family + "-" + entry.modelData[0]
                    width: root.definition.entries.length === 1 ? 152 : 80
                    height: 22; fieldMinHeight: 22
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    clearButtonVisible: false
                    property bool dirty: false
                    readonly property string display: String(root.values[entry.modelData[0]]) + entry.modelData[2]
                    Accessible.name: entry.modelData[1]
                    onDisplayChanged: if (!focused || !dirty) text = display
                    Component.onCompleted: text = display
                    onTextEdited: dirty = true
                    function commit() {
                        if (!dirty) return
                        dirty = false
                        root.canvas.viewOptions.editText(root.section,entry.modelData[0],text)
                        text = display
                    }
                    onAccepted: { commit(); root.forceActiveFocus() }
                    onFocusedChanged: if (!focused) commit()
                }
            }
        }
    }
    LV.Label { visible: root.definition.kind === "select"; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; style: caption; text: root.definition.label || "" }
    LV.ComboBox {
        id: select
        visible: root.definition.kind === "select"
        objectName: "viewOption-" + root.family + "-" + (root.definition.key || "")
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        width: 208; height: 22
        text: String(root.values[root.definition.key] || "")
        tone: LV.ComboBox.Borderless; arrow: LV.Stepper.Down
        Accessible.name: root.definition.label || ""
        onClicked: menu.openFor(select, 0, height)
    }
    LV.ContextMenu {
        id: menu
        objectName: "viewOptionMenu-" + root.family + "-" + (root.definition.key || "")
        showIconSlot: false
        selectedIndex: NativeData.presentation.indexOf((root.definition.options || []), root.values[root.definition.key])
        items: NativeData.presentation.labels(root.definition.options || [], "")
        onItemTriggered: function(index) { root.edit(root.definition.key, root.definition.options[index]) }
    }
    Rectangle {
        visible: root.definition.kind === "preview"
        anchors.fill: parent; radius: LV.Theme.radiusControl; color: LV.Theme.panelBackground10
        Loader {
            anchors.fill: parent
            active: root.definition.kind === "preview"
            sourceComponent: EditorAlignmentOverlay { objectName: "viewOptionsPreview-" + root.family; canvas: root.canvas; family: root.family; preview: true }
        }
    }
}
