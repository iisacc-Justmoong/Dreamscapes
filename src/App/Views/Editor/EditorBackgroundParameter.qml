pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import LVRS 1.0 as LV

Column {
    id: root
    required property string field
    property string title: ""
    property string type: "select"
    property var value
    property var options: []
    property real minimum: 0
    property real maximum: 100
    property real step: 1
    property string unit: "%"
    property bool readOnly: false
    property real controlWidth: -1
    readonly property bool modalActive: menu.visible
    signal edited(var value)
    width: parent.width
    spacing: type === "slider" ? 4 : 8
    component Entry: LV.InputField {
        id: entry
        property bool dirty: false
        property string display: String(root.value === undefined ? "" : root.value) + (NativeData.presentation.indexOf(["slider","number"], root.type)>=0 ? (root.unit === "px" ? " px" : root.unit) : "")
        height: 22; fieldMinHeight: 22; clearButtonVisible: false
        Binding { target:entry.inputItem; property:"font.weight"; value:Font.Normal }
        Binding { target:entry.inputItem; property:"font.styleName"; value:"Regular" }
        readOnly: root.readOnly
        onDisplayChanged: if (!focused || !dirty) text = display
        Component.onCompleted: text = display
        function commit() {
            if (dirty) {
                dirty = false
                if (root.type === "slider" || root.type === "number") {
                    const next = NativeData.tools.numberInput(text,root.minimum,root.maximum,root.unit)
                    if (next !== null) root.edited(next)
                } else root.edited(text)
            }
            text = display
        }
        onTextEdited: dirty = true
        onAccepted: { commit(); root.forceActiveFocus() }
        onFocusedChanged: if (!focused && dirty) commit()
    }
    Row {
        width: parent.width; height: 22; spacing: 8
        visible: root.type !== "choices"
        LV.Label {
            visible: root.title.length > 0
            width: parent.width - control.width - 8
            height: 22; style: body; font.weight: Font.Normal; font.styleName:"Regular"
            color: LV.Theme.bodyColor; text: root.title; verticalAlignment: Text.AlignVCenter
        }
        Item {
            id: control
            width: root.controlWidth>=0 ? root.controlWidth : root.title.length ? (root.type === "slider" ? Math.min(122, root.width * 122 / 398) : root.type === "toggle" ? 38 : root.width * 230 / 398) : parent.width
            height: 22
            LV.ComboBox {
                objectName: root.type === "select" ? "background-" + root.field : ""
                visible: root.type === "select"; anchors.fill: parent
                text: String(root.value === undefined ? "" : root.value)
                tone: LV.AbstractButton.Primary; arrow: LV.Stepper.UpDown
                Accessible.name: root.title
                onClicked: menu.openFor(this, Math.min(0, root.width - x - menu.implicitWidth), height)
            }
            Entry {
                objectName: NativeData.presentation.indexOf(["text","number","slider"], root.type)>=0 ? "background-" + root.field : ""
                visible: NativeData.presentation.indexOf(["text", "number", "slider"], root.type) >= 0
                anchors.fill: parent; Accessible.name: root.title || root.field
            }
            LV.ToggleSwitch {
                objectName: root.type === "toggle" ? "background-" + root.field : ""
                visible: root.type === "toggle"; anchors.right: parent.right
                trackWidth: 38; trackHeight: 22; checked: Boolean(root.value)
                Accessible.name: root.title
                onToggled: root.edited(checked)
            }
        }
    }
    LV.Slider {
        objectName: "backgroundSlider-" + root.field
        visible: root.type === "slider"; width: parent.width
        size: LV.Slider.Mini; showMinMax: false; showLabels: false; showSymbol: false
        from: root.minimum; to: root.maximum; stepSize: root.step; value: Number(root.value || 0)
        Accessible.name: root.title
        onMoved: root.edited(value)
    }
    LV.Label { visible: root.type === "choices" && root.title.length; width: parent.width; height: 18; style: body; font.weight: Font.Normal; font.styleName:"Regular"; color: LV.Theme.bodyColor; text: root.title }
    Flow {
        visible: root.type === "choices"; width: parent.width; spacing: 8
        Repeater {
            model: root.options
            LV.LabelButton {
                id: selectionButton1
                required property var modelData
                objectName: "background-" + root.field + "-" + String(modelData)
                text: String(modelData)
                tone: LV.AbstractButton.Borderless
                onClicked: root.edited(modelData)

                Accessible.checkable: selectionButton1.checkable
                Accessible.checked: selectionButton1.checked
                EditorButtonState { button: selectionButton1; selected: Boolean(root.value === modelData) }
                Accessible.onToggleAction: selectionButton1.click()
            }
        }
    }
    LV.ContextMenu {
        id: menu
        showIconSlot: false
        items: NativeData.presentation.labels(root.options)
        onItemTriggered: function(index) {
            const option = root.options[index]
            if (option !== undefined) root.edited(option.value === undefined ? option : option.value)
        }
    }
}
