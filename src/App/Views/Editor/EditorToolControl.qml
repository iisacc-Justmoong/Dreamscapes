pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV
import "EditorToolDefinitions.js" as Definitions

// Layout only: all visible controls are unmodified LVRS primitives.
Item {
    id: root
    required property var field
    required property var value
    signal edited(var value)
    signal requested()
    signal colorRequested()
    objectName: "editorControl-" + field.id
    readonly property bool toggle: field.type === "Toggle"
    readonly property bool preview: field.type === "Visual"
    readonly property bool inlineField: field.type === "Field" && width >= 360
        && field.label.length <= 23 && String(field.initial).length < 31
    readonly property bool inlineChoices: (field.type === "Choices" || field.type === "Segmented")
        && width >= 360 && field.label.length <= 21
        && control.item && control.item.optionsWidth <= width - 118
    readonly property bool inlineControl: toggle || inlineField || inlineChoices
    implicitHeight: inlineControl ? Math.max(caption.implicitHeight, control.implicitHeight)
        : (preview ? 0 : caption.implicitHeight + 2) + control.implicitHeight
    height: implicitHeight

    LV.Label {
        id: caption
        visible: !root.preview
        width: root.toggle ? root.width - 46 : root.inlineField ? 140 : root.inlineChoices ? 110 : root.width
        y: 0
        text: root.field.label
        style: body
        wrapMode: Text.Wrap
        sizeToContentHeight: true
    }
    Loader {
        id: control
        x: root.toggle ? root.width - 38 : root.inlineField ? 148 : root.inlineChoices ? 118 : 0
        y: root.inlineControl || root.preview ? 0 : caption.implicitHeight + 2
        width: root.width - x
        height: implicitHeight
        sourceComponent: root.toggle ? switchComponent : root.preview ? previewComponent
            : root.field.type === "Slider" ? sliderComponent
            : root.field.type === "Choices" || root.field.type === "Segmented" ? choicesComponent
            : root.field.type === "Color" ? colorComponent
            : root.field.type === "Dimensions" ? dimensionsComponent : inputComponent
    }
    Component {
        id: inputComponent
        LV.InputField {
            objectName: "editorInput-" + root.field.id
            fieldMinHeight: 26
            clearButtonVisible: false
            text: String(root.value)
            Accessible.name: root.field.label
            onTextEdited: function(text) { root.edited(text) }
        }
    }
    Component {
        id: dimensionsComponent
        Row {
            spacing: 8
            Repeater {
                model: 2
                LV.InputField {
                    required property int index
                    objectName: "editorDimension-" + root.field.id + "-" + index
                    width: (parent.width - 8) / 2
                    height: 26
                    fieldMinHeight: 26
                    clearButtonVisible: false
                    text: root.value[index] + " px"
                    Accessible.name: root.field.label + (index === 0 ? " width" : " height")
                    function commit() {
                        const raw = text.trim().replace(/\s*px$/, "")
                        const number = Number(raw)
                        if (/^\d+$/.test(raw) && number >= 1 && number <= 65536) {
                            const next = root.value.slice()
                            next[index] = number
                            root.edited(next)
                        }
                        text = Qt.binding(function() { return root.value[index] + " px" })
                    }
                    onAccepted: commit()
                    onActiveFocusChanged: if (!activeFocus) commit()
                }
            }
        }
    }
    Component {
        id: switchComponent
        LV.ToggleSwitch {
            objectName: "editorToggle-" + root.field.id
            checked: Boolean(root.value)
            trackWidth: 38
            trackHeight: 22
            implicitHeight: 22
            Accessible.name: root.field.label
            Accessible.description: root.field.description || ""
            onToggled: root.edited(checked)
        }
    }
    Component {
        id: choicesComponent
        Flow {
            id: choices
            spacing: 6
            readonly property real optionsWidth: {
                let total = Math.max(0, options.count - 1) * spacing
                for (let i = 0; i < options.count; ++i) total += options.itemAt(i).implicitWidth
                return total
            }
            Repeater {
                id: options
                model: root.field.options
                LV.LabelButton {
                    required property string modelData
                    required property int index
                    objectName: "editorChoice-" + root.field.id + "-" + index
                    text: modelData
                    height: 24
                    width: Math.min(implicitWidth, choices.width)
                    tone: root.value === modelData ? LV.AbstractButton.Primary : LV.AbstractButton.Default
                    Accessible.name: root.field.label + ": " + modelData
                    Accessible.checkable: true
                    Accessible.checked: root.value === modelData
                    onClicked: root.edited(modelData)
                }
            }
        }
    }
    Component {
        id: sliderComponent
        Row {
            spacing: 8
            LV.Slider {
                objectName: "editorSlider-" + root.field.id
                width: Math.max(0, parent.width - numeric.width - 8)
                height: 22
                y: 2
                size: LV.Slider.Mini
                showSymbol: false
                showMinMax: false
                from: root.field.minimum
                to: root.field.maximum
                stepSize: root.field.step
                value: Array.isArray(root.value) ? root.value[0] : root.value
                Accessible.name: root.field.label
                onMoved: {
                    const next = Array.isArray(root.value) ? [Math.min(value, root.value[1]), root.value[1]] : value
                    root.edited(next)
                }
            }
            LV.InputField {
                id: numeric
                objectName: "editorNumeric-" + root.field.id
                width: Array.isArray(root.value) ? 110 : 72
                height: 26
                fieldMinHeight: 26
                clearButtonVisible: false
                text: Definitions.formatted(root.field, root.value)
                Accessible.name: root.field.label + " value"
                function commit() {
                    const next = Definitions.parsed(root.field, text)
                    if (next !== null) root.edited(next)
                    text = Qt.binding(function() { return Definitions.formatted(root.field, root.value) })
                }
                onAccepted: commit()
                onActiveFocusChanged: if (!activeFocus) commit()
            }
        }
    }
    Component {
        id: colorComponent
        Row {
            spacing: 8
            LV.InputField {
                objectName: "editorColorInput-" + root.field.id
                width: parent.width - 36
                height: 26
                fieldMinHeight: 26
                clearButtonVisible: false
                text: root.value
                Accessible.name: root.field.label + " hex"
                function commit() {
                    if (/^#[0-9a-fA-F]{6}$/.test(text.trim())) root.edited(text.trim().toUpperCase())
                    text = Qt.binding(function() { return root.value })
                }
                onAccepted: commit()
                onActiveFocusChanged: if (!activeFocus) commit()
            }
            LV.ColorPickerButton {
                objectName: "editorColor-" + root.field.id
                buttonSize: 28
                currentColor: root.value
                Accessible.name: root.field.label
                onClicked: root.colorRequested()
            }
        }
    }
    Component {
        id: previewComponent
        LV.ListItem {
            objectName: "editorPreview-" + root.field.id
            type: LV.ListItem.Navigation
            label: root.field.label
            value: qsTr("View")
            showLeadingIcon: false
            showDescription: false
            standardItemHeight: 36
            onClicked: root.requested()
        }
    }
}
