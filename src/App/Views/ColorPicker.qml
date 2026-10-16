pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV
import Dreamscapes.Storage 1.0

Item {
    id: root
    enum PickerType { HueCube, BrightnessCube, ColorWheel, GrayscaleSlider, RGBSliders, HSBSliders, CMYKSliders, LabSliders, WebColorSliders }
    property int type: ColorPicker.HueCube
    property alias currentColor: colorState.color
    readonly property alias previousColor: colorState.previousColor
    readonly property alias hex: colorState.hex
    property bool showActions: true
    property bool showRecentColors: true
    property var recentColors: ["#7A5AF8", "#0A84FF", "#32D7E8", "#38D996", "#FFD65A", "#FF9F45", "#FF647C", "#C28AFF"]
    property string applyText: qsTr("Apply")
    property string cancelText: qsTr("Cancel")
    property real padding: LV.Theme.gap12
    readonly property int effectiveType: Math.max(0, Math.min(8, type))
    readonly property bool hasPlane: effectiveType <= ColorPicker.ColorWheel
    signal colorEdited(color color)
    signal accepted(color color)
    signal canceled
    signal eyedropperRequested
    function setColor(color: color) { colorState.beginColor(color) }
    function setHex(text: string): bool { return colorState.editHex(text) }
    function accept() { root.forceActiveFocus(); colorState.accept(); root.accepted(root.currentColor) }
    function cancel() { root.forceActiveFocus(); colorState.cancel(); root.canceled() }
    implicitWidth: 320
    implicitHeight: content.implicitHeight + padding * 2
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Color picker")
    Keys.onEscapePressed: root.cancel()

    ColorManagerModel {
        id: colorState
        objectName: "colorPicker_state"
        mode: root.effectiveType
        onColorEdited: function(color) { root.colorEdited(color) }
    }
    component ColorInput: LV.InputField {
        id: entry
        property string valueText: ""
        property bool edited: false
        signal committed(string value)
        style: inlineStyle
        clearButtonVisible: false
        height: LV.Theme.controlHeightSm
        function commit() {
            if (entry.edited) { entry.edited = false; entry.committed(entry.text) }
            entry.text = entry.valueText
        }
        onValueTextChanged: if (!entry.focused || !entry.edited) entry.text = entry.valueText
        onTextEdited: function(value) { entry.edited = true }
        onAccepted: function(value) { entry.commit(); root.forceActiveFocus() }
        onFocusedChanged: if (!entry.focused && entry.edited) entry.commit()
        Component.onCompleted: entry.text = entry.valueText
    }
    component ChannelRow: Item {
        id: row
        required property int channel
        readonly property var info: { const revision = colorState.revision; return colorState.channel(channel) }
        width: content.width
        height: LV.Theme.controlHeightSm
        LV.Label { width: 16; anchors.verticalCenter: parent.verticalCenter; text: row.info.label; style: body; color: LV.Theme.descriptionColor }
        LV.Slider {
            id: slider
            objectName: "colorPicker_channel_" + row.channel
            x: 22
            width: Math.max(16, row.width - 100)
            size: LV.Slider.Mini
            from: row.info.minimum
            to: row.info.maximum
            stepSize: row.info.step
            value: row.info.value
            Accessible.name: row.info.name
            onMoved: colorState.editChannel(row.channel, value)
            background: ColorManagerSurface {
                x: slider.rangeInset
                y: (slider.height - height) / 2
                width: slider.rangeWidth
                height: slider.trackHeight
                model: colorState
                field: ColorManagerSurface.ChannelRamp
                channel: row.channel
                interactive: false
                showMarker: false
            }
        }
        ColorInput {
            objectName: "colorPicker_channelInput_" + row.channel
            x: row.width - 72; width: 54
            valueText: String(Math.round(row.info.value))
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            Accessible.name: row.info.name
            onCommitted: function(value) { if (value.trim().length && isFinite(Number(value))) colorState.editChannel(row.channel, Number(value)) }
        }
        LV.Label { x: row.width - 14; width: 14; anchors.verticalCenter: parent.verticalCenter; text: row.info.unit; style: caption; color: LV.Theme.descriptionColor }
    }
    Column {
        id: content
        x: root.padding; y: root.padding
        width: Math.max(0, root.width - root.padding * 2)
        spacing: LV.Theme.gap12
        Item {
            objectName: "colorPicker_header"
            width: parent.width
            height: Math.max(pickerTitle.implicitHeight, modeButton.implicitHeight)
            LV.Label {
                id: pickerTitle
                objectName: "colorPicker_title"
                anchors.left: parent.left
                width: Math.min(implicitWidth, Math.max(0, parent.width - modeButton.width - LV.Theme.gap8))
                height: parent.height
                text: qsTr("Color picker")
                style: body
                color: LV.Theme.titleHeaderColor
                verticalAlignment: Text.AlignVCenter
            }
            TextMetrics { id: modeMetrics; font: pickerTitle.font; text: colorState.modeName }
            LV.ComboBox {
                id: modeButton
                objectName: "colorPicker_modeButton"
                anchors.right: parent.right
                width: Math.max(0, Math.min(parent.width - pickerTitle.implicitWidth - LV.Theme.gap8,
                    Math.max(LV.Theme.scaleMetric(61), Math.ceil(modeMetrics.advanceWidth)
                        + figmaComboLeftPadding + figmaComboRightPadding + figmaIndicatorSize + LV.Theme.gap4)))
                tone: LV.ComboBox.Primary
                arrow: LV.Stepper.UpDown
                text: colorState.modeName
                activeFocusOnTab: true
                Accessible.role: Accessible.ComboBox
                Accessible.name: qsTr("Color picker mode")
                function openMenu() {
                    forceActiveFocus()
                    modeMenu.openFor(modeButton, modeButton.width - modeMenu.implicitWidth, modeButton.height)
                }
                onClicked: openMenu()
                Keys.onSpacePressed: function(event) { openMenu(); event.accepted = true }
                Keys.onReturnPressed: function(event) { openMenu(); event.accepted = true }
            }
        }
        Item {
            objectName: "colorPicker_domain"
            width: parent.width
            visible: root.hasPlane
            height: Math.min(Math.max(80, width - 36), 232)
            ColorManagerSurface {
                objectName: "colorPicker_field"
                width: root.effectiveType === ColorPicker.ColorWheel ? height : parent.width - 36
                height: parent.height
                x: root.effectiveType === ColorPicker.ColorWheel ? (parent.width - 36 - width) / 2 : 0
                model: colorState
                field: root.effectiveType === ColorPicker.HueCube ? ColorManagerSurface.HueCube : root.effectiveType === ColorPicker.BrightnessCube ? ColorManagerSurface.BrightnessCube : ColorManagerSurface.ColorWheel
                Accessible.role: Accessible.Slider
                Accessible.name: root.effectiveType === ColorPicker.HueCube ? qsTr("Saturation and brightness") : root.effectiveType === ColorPicker.BrightnessCube ? qsTr("Hue and saturation") : qsTr("Hue and saturation wheel")
                Accessible.description: "#" + root.hex
            }
            ColorManagerSurface {
                objectName: "colorPicker_rail"
                anchors.right: parent.right; width: 24; height: parent.height
                model: colorState
                field: root.effectiveType === ColorPicker.HueCube ? ColorManagerSurface.HueRail : ColorManagerSurface.BrightnessRail
                Accessible.role: Accessible.Slider
                Accessible.name: root.effectiveType === ColorPicker.HueCube ? qsTr("Hue") : qsTr("Brightness")
            }
        }
        Column {
            width: parent.width; spacing: LV.Theme.gap4
            Repeater { model: colorState.channels; delegate: ChannelRow { required property int modelData; channel: modelData } }
        }
        ColorManagerSurface {
            objectName: "colorPicker_spectrum"
            visible: !root.hasPlane
            width: parent.width; height: 16
            model: colorState; field: ColorManagerSurface.Spectrum; showMarker: false
            Accessible.name: qsTr("Color spectrum")
        }
        ChannelRow { channel: 10 }
        Row {
            objectName: "colorPicker_values"
            width: parent.width; height: 28; spacing: LV.Theme.gap8
            Rectangle { width: 20; height: 28; color: root.previousColor; border.color: LV.Theme.descriptionColor; Accessible.name: qsTr("Previous color") }
            Rectangle { width: 20; height: 28; color: root.currentColor; border.color: LV.Theme.descriptionColor; Accessible.name: qsTr("Current color") }
            ColorInput {
                objectName: "colorPicker_hexInput"
                width: Math.max(60, parent.width - 40 - sampleButton.implicitWidth - 3 * LV.Theme.gap8)
                valueText: "#" + root.hex
                Accessible.name: qsTr("Hex color")
                onCommitted: function(value) { colorState.editHex(value) }
            }
            LV.LabelButton { id: sampleButton; objectName: "colorPicker_sample"; text: qsTr("Sample"); onClicked: root.eyedropperRequested() }
        }
        LV.Label {
            width: parent.width
            visible: root.effectiveType === ColorPicker.LabSliders && colorState.outOfGamut
            text: qsTr("Outside sRGB; the preview is clipped.")
            wrapMode: Text.Wrap; sizeToContentHeight: true; style: description
        }
        LV.LabelButton {
            objectName: "colorPicker_webSafe"
            visible: root.effectiveType === ColorPicker.WebColorSliders && !colorState.webSafe
            text: qsTr("Use nearest web-safe color")
            onClicked: colorState.snapWebSafe()
        }
        Flow {
            width: parent.width; spacing: LV.Theme.gap4
            visible: root.showRecentColors
            Repeater {
                model: root.recentColors
                delegate: LV.ColorPickerButton {
                    required property color modelData
                    buttonSize: LV.ColorPickerButton.Small
                    currentColor: modelData
                    Accessible.name: qsTr("Recent color %1").arg(modelData)
                    onClicked: colorState.editColor(modelData)
                }
            }
        }
        Row {
            width: parent.width; spacing: LV.Theme.gap8
            visible: root.showActions
            LV.LabelButton { objectName: "colorPicker_cancel"; text: root.cancelText; tone: LV.AbstractButton.Default; onClicked: root.cancel() }
            LV.LabelButton { objectName: "colorPicker_apply"; text: root.applyText; tone: LV.AbstractButton.Primary; onClicked: root.accept() }
        }
    }
    LV.ContextMenu {
        id: modeMenu
        objectName: "colorPicker_modeMenu"
        showIconSlot: false
        items: colorState.modeNames.map(function(name, index) { return { label: (root.effectiveType === index ? "✓ " : "") + name } })
        onItemTriggered: function(index) { root.type = index }
    }
}
