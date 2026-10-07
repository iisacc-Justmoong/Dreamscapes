pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV
import ".." as Views
import "EditorToolDefinitions.js" as Definitions

// Layout only: all visible controls are unmodified LVRS primitives.
Item {
    id: root
    required property var field
    required property var value
    property bool sampleLayout: false
    property var capability: ({enabled: true, reason: ""})
    enabled: capability.enabled !== false
    opacity: enabled ? 1 : 0.48
    Accessible.description: capability.reason || ""
    signal edited(var value)
    signal requested()
    signal colorRequested()
    objectName: "editorControl-" + field.id
    readonly property bool toggle: field.type === "Toggle"
    readonly property bool preview: field.type === "Visual"
    readonly property bool inlineField: field.type === "Field" && (sampleLayout ? !field.desktopStacked && width >= captionMetrics.width + 218
        : (width >= 360 && field.label.length <= 23 && String(field.initial).length < 31))
    readonly property bool inlineChoices: !sampleLayout && (field.type === "Choices" || field.type === "Segmented")
        && width >= 360 && field.label.length <= 21
        && control.item && control.item.optionsWidth <= width - 118
    readonly property bool inlineControl: toggle || inlineField || inlineChoices
    readonly property bool inlineSlider: sampleLayout && field.type === "Slider" && width >= captionMetrics.width + 218
    readonly property real headingHeight: Math.max(22, caption.implicitHeight)
        + (sampleLayout && field.type === "Slider" && !inlineSlider ? 30 : 0)
    implicitHeight: sampleLayout && field.type === "Slider" ? headingHeight + 30
        : sampleLayout && field.type === "Visual" ? 44
        : inlineControl ? Math.max(caption.implicitHeight, control.implicitHeight)
        : control.y + control.implicitHeight
    height: implicitHeight

    TextMetrics {
        id: captionMetrics
        font: caption.item ? (caption.item as LV.Label).font : Qt.font({})
        text: root.field.label
    }

    Loader {
        id: caption
        visible: !root.preview
        width: root.sampleLayout && root.inlineField ? Math.max(0, root.width - Math.min(206, root.width) - 8)
            : root.toggle ? root.width - 46 : root.sampleLayout && root.field.type === "Slider"
            ? root.inlineSlider ? Math.max(0, root.width - Math.min(206, root.width) - 8) : root.width
            : root.inlineField ? 140 : root.inlineChoices ? 110 : root.width
        y: 0
        sourceComponent: root.sampleLayout ? sampleCaption : normalCaption
    }
    Component {
        id: sampleCaption
        LV.Label {
            objectName: "editorCaption-" + root.field.id
            text: root.field.label
            style: body
            wrapMode: Text.Wrap
            sizeToContentHeight: true
            implicitHeight: Math.max(17, contentHeight)
        }
    }
    Component {
        id: normalCaption
        LV.Label {
            text: root.field.label
            style: body
            wrapMode: Text.Wrap
            sizeToContentHeight: true
        }
    }
    Loader {
        id: control
        x: root.toggle ? root.width - 38 : root.sampleLayout ? root.width - width
            : root.inlineField ? 148 : root.inlineChoices ? 118 : 0
        y: root.inlineControl || root.preview || (root.sampleLayout && root.field.type === "Slider") ? 0
            : root.sampleLayout && (root.field.type === "Choices" || root.field.type === "Segmented") ? Math.max(17, caption.implicitHeight) + 8
            : caption.implicitHeight + 2
        width: root.sampleLayout && (root.field.type === "Dimensions" || root.field.type === "Field") ? Math.min(206, root.width)
            : root.sampleLayout && root.preview ? Math.min(280, root.width)
            : root.sampleLayout && root.field.type === "Color" ? Math.min(242, root.width)
            : root.toggle ? 38 : root.sampleLayout ? root.width : root.width - x
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
            height: root.sampleLayout ? 22 : 26
            fieldMinHeight: root.sampleLayout ? 22 : 26
            clearButtonVisible: false
            text: String(root.value)
            Accessible.name: root.field.label
            onTextEdited: function(text) { root.edited(text) }
        }
    }
    Component {
        id: dimensionsComponent
        Item {
            implicitHeight: root.sampleLayout ? 52 : 26
            Repeater {
                model: 2
                LV.InputField {
                    required property int index
                    objectName: "editorDimension-" + root.field.id + "-" + index
                    x: root.sampleLayout ? 0 : index * (width + 8)
                    y: root.sampleLayout ? index * 30 : 0
                    width: root.sampleLayout ? parent.width : (parent.width - 8) / 2
                    height: root.sampleLayout ? 22 : 26
                    fieldMinHeight: root.sampleLayout ? 22 : 26
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
        Item {
            id: choices
            readonly property real spacing: root.sampleLayout ? 8 : 6
            readonly property int maximumColumns: root.field.desktopColumns || Math.min(4, root.field.options.length)
            property int layoutRevision: 0
            readonly property var layout: {
                const revision = layoutRevision
                const widths = []
                for (let i = 0; i < options.count; ++i) {
                    const slot = options.itemAt(i)
                    widths.push(slot ? slot.implicitWidth : 0)
                }
                return Definitions.buttonRows(width, widths, root.sampleLayout ? maximumColumns : options.count,
                    root.sampleLayout ? 22 : 24, spacing, root.sampleLayout)
            }
            implicitHeight: layout.height
            readonly property real optionsWidth: {
                let total = Math.max(0, options.count - 1) * spacing
                for (let i = 0; i < options.count; ++i) total += options.itemAt(i).implicitWidth
                return total
            }
            Repeater {
                id: options
                model: root.field.options
                onItemAdded: Qt.callLater(function() { choices.layoutRevision++ })
                onItemRemoved: Qt.callLater(function() { choices.layoutRevision++ })
                Item {
                    required property string modelData
                    required property int index
                    id: optionSlot
                    readonly property var placement: choices.layout.items[index] || {x: 0, y: 0, width: 0}
                    x: placement.x
                    y: placement.y
                    width: placement.width
                    implicitWidth: option.implicitWidth
                    height: root.sampleLayout ? 22 : 24
                    LV.LabelButton {
                        id: option
                        objectName: "editorChoice-" + root.field.id + "-" + optionSlot.index
                        text: root.field.optionLabels ? root.field.optionLabels[optionSlot.index] : optionSlot.modelData
                        height: parent.height
                        width: Math.min(implicitWidth, parent.width)
                        tone: root.value === optionSlot.modelData ? LV.AbstractButton.Primary : LV.AbstractButton.Default
                        Accessible.name: root.field.label + ": " + optionSlot.modelData
                        Accessible.checkable: true
                        Accessible.checked: root.value === optionSlot.modelData
                        onClicked: root.edited(optionSlot.modelData)
                    }
                }
            }
        }
    }
    Component {
        id: sliderComponent
        Item {
            implicitHeight: root.sampleLayout ? root.headingHeight + 30 : 26
            LV.Slider {
                objectName: "editorSlider-" + root.field.id
                x: root.sampleLayout ? parent.width - width : 0
                width: root.sampleLayout ? Math.min(320, parent.width) : Math.max(0, parent.width - numeric.width - 8)
                height: 22
                y: root.sampleLayout ? root.headingHeight + 8 : 2
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
                x: parent.width - width
                width: root.sampleLayout ? Math.min(206, parent.width) : Array.isArray(root.value) ? 110 : 72
                y: root.sampleLayout && !root.inlineSlider ? root.headingHeight - 22 : 0
                height: root.sampleLayout ? 22 : 26
                fieldMinHeight: root.sampleLayout ? 22 : 26
                clearButtonVisible: false
                text: Definitions.formatted(root.field, root.value, root.sampleLayout)
                Accessible.name: root.field.label + " value"
                function commit() {
                    const next = Definitions.parsed(root.field, text)
                    if (next !== null) root.edited(next)
                    text = Qt.binding(function() { return Definitions.formatted(root.field, root.value, root.sampleLayout) })
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
                width: root.sampleLayout ? Math.min(206, parent.width - 36) : parent.width - 36
                height: root.sampleLayout ? 22 : 26
                fieldMinHeight: root.sampleLayout ? 22 : 26
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
        Views.PanelRow {
            objectName: "editorPreview-" + root.field.id
            type: LV.ListItem.Navigation
            label: root.field.label
            value: qsTr("View")
            showLeadingIcon: false
            showDescription: false
            standardItemHeight: root.sampleLayout ? 44 : 36
            trailingComponent: root.sampleLayout ? desktopDisclosure : null
            onClicked: root.requested()
        }
    }
    Component {
        id: desktopDisclosure
        LV.HStack {
            objectName: "editorPreviewDisclosure"
            spacing: 4
            LV.Label {
                id: previewValue
                objectName: "editorPreviewValue"
                text: qsTr("View")
                style: previewValue.caption
                color: LV.Theme.captionColor
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 72
                Layout.minimumWidth: 72
                Layout.maximumWidth: 72
                Layout.preferredHeight: 11
                Layout.maximumHeight: 11
            }
            LV.IconButton {
                objectName: "editorPreviewChevron"
                width: 18
                height: 18
                implicitWidth: 18
                implicitHeight: 18
                iconSize: 18
                iconSource: Qt.resolvedUrl("Assets/Panel/general-chevron-right.svg")
                horizontalPadding: 0
                verticalPadding: 0
                tone: LV.AbstractButton.Borderless
                Accessible.name: root.field.label + ": " + qsTr("View")
                onClicked: root.requested()
            }
        }
    }
}
