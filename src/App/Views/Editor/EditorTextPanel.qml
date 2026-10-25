pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import LVRS 1.0 as LV
import ".." as Views

Column {
    id: root
    objectName: "editorTextPanel"
    required property var toolState
    property int panelIndex: 0
    readonly property var values: Object.assign({}, NativeData.text.defaultsFor(NativeData.text.definition().fields),
        toolState.toolId === "text" ? toolState.values : (toolState.settingsByTool.text || {}))
    readonly property var engine: toolState.engine
    readonly property var tabs: ["Content", "Typography", "Layout", "Appearance"]
    readonly property string readout: NativeData.text.readout(values)
    width: 398
    spacing: 12
    function asset(name) { return Qt.resolvedUrl("Assets/Text/" + name + ".svg") }
    function value(id) { return values[id] }
    function edit(id, next) {
        if (toolState.toolId !== "text" || next === value(id)) return
        toolState.setValue(id, next)
        if (id === "field-5" || id.indexOf("Constraint") >= 0 || id === "textReference" || id === "textOffsetX" || id === "textOffsetY") toolState.setValue("textLayoutEnabled", true)
        if (id === "textHorizontalConstraint" && ["Left", "Center", "Right"].indexOf(next) >= 0) {
            const placement = String(value("field-5"))
            const vertical = placement.indexOf("Top") === 0 ? "Top" : placement.indexOf("Bottom") === 0 ? "Bottom" : ""
            toolState.setValue("field-5", vertical ? vertical + (next === "Center" ? "" : " " + next.toLowerCase()) : next)
        }
        if (id === "textVerticalConstraint" && ["Top", "Center", "Bottom"].indexOf(next) >= 0) {
            const placement = String(value("field-5"))
            const horizontal = placement.toLowerCase().indexOf("left") >= 0 ? "left" : placement.toLowerCase().indexOf("right") >= 0 ? "right" : ""
            toolState.setValue("field-5", next === "Center" ? (horizontal ? horizontal.charAt(0).toUpperCase() + horizontal.slice(1) : "Center") : next + (horizontal ? " " + horizontal : ""))
        }
        if (id === "field-2" && value("textLinePreset") === "Token") toolState.setValue("textLineHeight", next)
        if (id === "textLineHeight") toolState.setValue("textLinePreset", "Token")
    }
    function patch(next) { Object.keys(next).forEach(function(id) { root.edit(id, next[id]) }) }
    function resetView() {
        patch(NativeData.text.defaultsFor(panelIndex === 1 ? NativeData.text.typography : panelIndex === 2 ? NativeData.text.layout : NativeData.text.appearance))
        if (panelIndex === 2) edit("textLayoutEnabled", false)
    }
    function applyRole(role) { patch(NativeData.text.roleValues(role)) }
    function optionAvailable(fieldId, option) {
        return !(fieldId === "textBlend" && ["Soft light", "Difference"].indexOf(option) >= 0)
            && !(fieldId === "textNoisePattern" && option === "Blue noise")
            && !(["textHorizontalConstraint", "textVerticalConstraint"].indexOf(fieldId) >= 0 && option === "Scale")
    }
    function linePreset(preset) {
        edit("textLinePreset", preset)
        toolState.setValue("textLineHeight", Math.round(Number(value("field-2")) * (preset === "Token" ? 1 : parseInt(preset) / 100) * 100) / 100)
    }

    component Heading: Views.PanelRow {
        type: LV.ListItem.Mini
        showLeadingIcon: false
        iconSize: 13
        minItemWidth: 0
        miniItemWidth: 0
        width: parent.width
        height: 17
    }
    component Caption: LV.Label {
        style: body
        width: parent.width
        height: 13
    }
    component NumberField: LV.InputField {
        id: numberField
        required property string fieldId
        readonly property var spec: NativeData.text.byId(fieldId)
        objectName: "textInput-" + fieldId
        height: 22
        fieldMinHeight: 22
        clearButtonVisible: false
        property bool textModified: false
        text: Number(root.value(fieldId)).toFixed(fieldId === "textGamma" ? 2 : 0) + (spec.unit === "px" ? " px" : spec.unit === "%" ? "%" : spec.unit === "°" ? "°" : "")
        Accessible.name: spec.label
        function commit() {
            textModified = false
            const input = text.trim().replace(/\s*(px|%|°)$/, "")
            const next = Number(input)
            if (input.length && isFinite(next) && next >= spec.minimum && next <= spec.maximum) root.edit(fieldId, next)
            text = Qt.binding(function() { return Number(root.value(numberField.fieldId)).toFixed(numberField.fieldId === "textGamma" ? 2 : 0) + (numberField.spec.unit === "px" ? " px" : numberField.spec.unit === "%" ? "%" : numberField.spec.unit === "°" ? "°" : "") })
        }
        onAccepted: commit()
        onTextEdited: textModified = true
        onActiveFocusChanged: if (!activeFocus && textModified) commit()
    }
    component NumericRow: Row {
        id: numericRow
        required property string fieldId
        property bool stepper: false
        width: parent.width
        height: 22
        spacing: 8
        Heading { label: NativeData.text.byId(numericRow.fieldId).label; width: parent.width - input.width - 8; height: 22 }
        Item {
            id: input
            width: Math.min(206, parent.width * 0.56)
            height: 22
            NumberField { fieldId: numericRow.fieldId; width: parent.width - (numericRow.stepper ? 20 : 0) }
            LV.Stepper {
                visible: numericRow.stepper
                x: parent.width - 18; y: 2
                objectName: "textFontSizeStepper"
                Accessible.name: qsTr("Font size")
                onStepped: function(direction) { root.edit(numericRow.fieldId, Math.max(1, Math.min(512, Number(root.value(numericRow.fieldId)) + direction))) }
            }
        }
    }
    component ScopeRow: Row {
        id: scope
        required property string fieldId
        width: parent.width
        height: 22
        spacing: 8
        Heading { label: NativeData.text.byId(scope.fieldId).label; width: parent.width - combo.width - 8; height: 22 }
        LV.ComboBox {
            id: combo
            objectName: "textSelect-" + scope.fieldId
            text: root.value(scope.fieldId)
            width: Math.min(206, parent.width * 0.56)
            height: 20; y: 1
            arrow: LV.Stepper.Down
            Accessible.name: NativeData.text.byId(scope.fieldId).label
            onClicked: { options.fieldId = scope.fieldId; options.openFor(this, 0, height) }
        }
    }
    component ChoiceGroup: Column {
        id: choices
        required property string fieldId
        property var entries: NativeData.text.byId(fieldId).options
        property var icons: []
        property int columns: entries.length
        property real buttonHeight: 26
        property bool heading: true
        property string headingText: NativeData.text.byId(fieldId).label
        property bool alignmentGallery: false
        width: parent.width
        spacing: 8
        Caption { visible: choices.heading; text: choices.headingText }
        Item {
            id: grid
            width: parent.width
            readonly property int columns: Math.min(choices.columns, root.width < 290 && choices.columns > 3 ? 2 : choices.columns)
            height: Math.ceil(choices.entries.length / columns) * choices.buttonHeight + Math.max(0, Math.ceil(choices.entries.length / columns) - 1) * 8
            Repeater {
                model: choices.entries
                Item {
                    id: choice
                    required property string modelData
                    required property int index
                    readonly property int rowColumns: Math.min(grid.columns, choices.entries.length - Math.floor(index / grid.columns) * grid.columns)
                    width: (grid.width - (rowColumns - 1) * 8) / rowColumns
                    height: choices.buttonHeight
                    x: (index % grid.columns) * (width + 8)
                    y: Math.floor(index / grid.columns) * (height + 8)
                    LV.LabelButton {
                        id: selectionButton1
                        visible: !choices.alignmentGallery
                        objectName: "textChoice-" + choices.fieldId + "-" + choice.index
                        width: parent.width; height: parent.height
                        text: choices.fieldId === "field-11" ? ["↖  Top left", "↑  Top", "↗  Top right", "←  Left", "None", "Right  →", "↙  Bottom left", "↓  Bottom", "↘  Bottom right"][choice.index] : choice.modelData
                        tone: LV.AbstractButton.Default
                        enabled: root.optionAvailable(choices.fieldId, choice.modelData)
                        Accessible.description: enabled ? "" : qsTr("Unavailable in the current canvas renderer")
                        Accessible.name: NativeData.text.byId(choices.fieldId).label + ": " + choice.modelData
                        Accessible.checkable: true
                        Accessible.checked: root.value(choices.fieldId) === choice.modelData
                        onClicked: choices.fieldId === "textLinePreset" ? root.linePreset(choice.modelData) : root.edit(choices.fieldId, choice.modelData)

                        EditorButtonState { button: selectionButton1; selected: Boolean(root.value(choices.fieldId) === choice.modelData) }
                        Accessible.onToggleAction: selectionButton1.click()
                    }
                    LV.IconButton {
                        id: alignmentButton
                        visible: choices.alignmentGallery
                        objectName: "textAlign-" + choices.fieldId + "-" + choice.index
                        width: 30; height: 30
                        horizontalPadding: 6; verticalPadding: 6
                        iconSource: choices.icons.length ? root.asset(choices.icons[choice.index]) : ""
                        contentItem: Item {
                            implicitWidth: 18; implicitHeight: 18
                            Image {
                                readonly property bool insetArtwork: ["LeftAlign", "CenterAlign", "RightAlign"].indexOf(choices.icons[choice.index]) >= 0
                                width: insetArtwork ? 16 : 18; height: width
                                source: alignmentButton.iconSource
                                sourceSize: Qt.size(width * 4, height * 4)
                                fillMode: Image.PreserveAspectFit
                            }
                        }
                        tone: LV.AbstractButton.Default
                        Accessible.name: NativeData.text.byId(choices.fieldId).label + ": " + choice.modelData
                        Accessible.checkable: true
                        Accessible.checked: root.value(choices.fieldId) === choice.modelData
                        onClicked: root.edit(choices.fieldId, choice.modelData)

                        EditorButtonState { button: alignmentButton; selected: Boolean(root.value(choices.fieldId) === choice.modelData) }
                        Accessible.onToggleAction: alignmentButton.click()
                    }
                    LV.Label {
                        visible: choices.alignmentGallery
                        x: 36; height: 30; width: parent.width - x
                        style: body; text: choice.modelData; verticalAlignment: Text.AlignVCenter
                        TapHandler { onTapped: root.edit(choices.fieldId, choice.modelData) }
                    }
                }
            }
        }
    }
    component Pair: Row {
        id: pair
        required property var fields
        width: parent.width
        spacing: 8
        height: 43
        Repeater {
            model: pair.fields
            Column {
                id: parameter
                required property string modelData
                width: (pair.width - 8) / 2
                spacing: 8
                Caption { text: NativeData.text.byId(parameter.modelData).label }
                NumberField { fieldId: parameter.modelData; width: parent.width }
            }
        }
    }
    component SliderRow: Column {
        id: sliderRow
        required property string fieldId
        width: parent.width
        spacing: 8
        NumericRow { fieldId: sliderRow.fieldId }
        LV.Slider {
            objectName: "textSlider-" + sliderRow.fieldId
            x: 8; width: parent.width - 16; height: 22
            from: NativeData.text.byId(sliderRow.fieldId).minimum
            to: NativeData.text.byId(sliderRow.fieldId).maximum
            value: Number(root.value(sliderRow.fieldId))
            stepSize: NativeData.text.byId(sliderRow.fieldId).step
            size: LV.Slider.Mini
            showSymbol: false; showMinMax: false
            Accessible.name: NativeData.text.byId(sliderRow.fieldId).label
            onMoved: root.edit(sliderRow.fieldId, value)
        }
    }
    component ToggleRow: Row {
        id: toggleRow
        required property string fieldId
        width: parent.width
        height: 22
        Heading { label: NativeData.text.byId(toggleRow.fieldId).label; width: parent.width - 46; height: 22 }
        Item { width: 8; height: 22 }
        LV.ToggleSwitch {
            objectName: "textToggle-" + toggleRow.fieldId
            trackWidth: 38; trackHeight: 22
            checked: Boolean(root.value(toggleRow.fieldId))
            Accessible.name: NativeData.text.byId(toggleRow.fieldId).label
            onToggled: root.edit(toggleRow.fieldId, checked)
        }
    }
    component Gallery: Row {
        id: gallery
        required property var entries
        required property string fieldId
        width: parent.width
        height: 72
        spacing: 8
        Repeater {
            model: gallery.entries
            Column {
                id: tile
                required property var modelData
                width: (gallery.width - (gallery.entries.length - 1) * gallery.spacing) / gallery.entries.length
                height: 72
                spacing: 4
                topPadding: 4
                LV.IconButton {
                    id: selectionButton3
                    objectName: "textUsage-" + tile.modelData.name
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 34; height: 34
                    horizontalPadding: 8; verticalPadding: 8
                    iconSource: root.asset(tile.modelData.icon)
                    tone: LV.AbstractButton.Default
                    Accessible.name: tile.modelData.name
                    Accessible.checkable: true
                    Accessible.checked: root.value(gallery.fieldId) === tile.modelData.name
                    onClicked: root.edit(gallery.fieldId, tile.modelData.name)

                    EditorButtonState { button: selectionButton3; selected: Boolean(root.value(gallery.fieldId) === tile.modelData.name) }
                    Accessible.onToggleAction: selectionButton3.click()
                }
                LV.Label {
                    style: body; text: tile.modelData.name
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 8; height: 26
                    wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    TapHandler { onTapped: root.edit(gallery.fieldId, tile.modelData.name) }
                }
            }
        }
    }

    Row {
        width: parent.width
        height: 30
        LV.IconSegmentedControl {
            forceBorderlessTone: false
            LV.IconButton { id: selectionButton4; objectName: "textTab-content"; iconSource: root.asset("Text"); Accessible.name: root.tabs[0]; tone: LV.AbstractButton.Borderless; onClicked: root.panelIndex = 0
                Accessible.checkable: selectionButton4.checkable
                Accessible.checked: selectionButton4.checked
                EditorButtonState { button: selectionButton4; selected: Boolean(root.panelIndex === 0) }
                Accessible.onToggleAction: selectionButton4.click()
            }
            LV.IconButton { id: selectionButton5; objectName: "textTab-typography"; iconSource: root.asset("Font"); Accessible.name: root.tabs[1]; tone: LV.AbstractButton.Borderless; onClicked: root.panelIndex = 1
                Accessible.checkable: selectionButton5.checkable
                Accessible.checked: selectionButton5.checked
                EditorButtonState { button: selectionButton5; selected: Boolean(root.panelIndex === 1) }
                Accessible.onToggleAction: selectionButton5.click()
            }
            LV.IconButton { id: selectionButton6; objectName: "textTab-layout"; iconSource: root.asset("AlignCenter"); Accessible.name: root.tabs[2]; tone: LV.AbstractButton.Borderless; onClicked: root.panelIndex = 2
                Accessible.checkable: selectionButton6.checkable
                Accessible.checked: selectionButton6.checked
                EditorButtonState { button: selectionButton6; selected: Boolean(root.panelIndex === 2) }
                Accessible.onToggleAction: selectionButton6.click()
            }
            LV.IconButton { id: selectionButton7; objectName: "textTab-appearance"; iconSource: root.asset("Callout"); Accessible.name: root.tabs[3]; tone: LV.AbstractButton.Borderless; onClicked: root.panelIndex = 3
                Accessible.checkable: selectionButton7.checkable
                Accessible.checked: selectionButton7.checked
                EditorButtonState { button: selectionButton7; selected: Boolean(root.panelIndex === 3) }
                Accessible.onToggleAction: selectionButton7.click()
            }
        }
        Item { width: parent.width - 132; height: 30 }
        LV.IconButton {
            visible: root.panelIndex === 0
            objectName: "textResetAll"
            width: 30; height: 30
            iconSource: root.asset("AutoRefresh")
            iconSize: 16
            tone: LV.AbstractButton.Borderless
            Accessible.name: qsTr("Reset text settings")
            onClicked: root.toolState.resetTool()
        }
    }
    Column {
        visible: root.panelIndex === 0
        width: parent.width
        spacing: 8
        Gallery { entries: NativeData.text.usages; fieldId: "selector" }
        Column {
            width: parent.width; spacing: 8
            Heading { label: qsTr("Content") }
            LV.InputField {
                id: contentSurface
                objectName: "textContentSurface"
                width: parent.width; height: 64; fieldMinHeight: 64
                clearButtonVisible: false; readOnly: true
                inputItem.visible: false
                TextEdit {
                    objectName: "textContentInput"
                    anchors.fill: parent; anchors.leftMargin: 7; anchors.rightMargin: 7; anchors.topMargin: 8; anchors.bottomMargin: 8
                    color: LV.Theme.titleHeaderColor
                    font.family: LV.Theme.fontBody; font.pixelSize: LV.Theme.textBody; font.weight: Font.Medium
                    selectionColor: LV.Theme.accent
                    selectedTextColor: LV.Theme.titleHeaderColor
                    text: root.value("field-0")
                    wrapMode: TextEdit.Wrap; selectByMouse: true; clip: true
                    Accessible.name: qsTr("Text content")
                    onTextChanged: if (text !== root.value("field-0")) root.edit("field-0", text)
                }
            }
        }
        Caption { text: qsTr("LVRS · Text role") }
        Column {
            width: parent.width; spacing: 8
            Repeater {
                model: 4
                Row {
                    id: roleRow
                    required property int index
                    width: parent.width; spacing: 8
                    Repeater {
                        model: NativeData.text.roles.slice(roleRow.index * 2, roleRow.index * 2 + 2)
                        Rectangle {
                            id: roleTile
                            required property var modelData
                            width: (roleRow.width - 8) / 2
                            height: Math.max(NativeData.text.roles[roleRow.index * 2].size, NativeData.text.roles[roleRow.index * 2 + 1].size) + 46
                            color: LV.Theme.panelBackground08; radius: LV.Theme.radiusMd
                            Column {
                                anchors.fill: parent; anchors.margins: 8; spacing: 8
                                LV.Label { width: parent.width; height: roleTile.height - 46; text: roleTile.modelData.name; style: roleTile.modelData.style }
                                LV.LabelButton {
                                    id: selectionButton8
                                    objectName: "textRole-" + roleTile.modelData.name
                                    width: parent.width; height: 22
                                    text: roleTile.modelData.size + " px · " + roleTile.modelData.weight
                                    tone: LV.AbstractButton.Default
                                    Accessible.name: roleTile.modelData.name
                                    onClicked: root.applyRole(roleTile.modelData)

                                    Accessible.checkable: selectionButton8.checkable
                                    Accessible.checked: selectionButton8.checked
                                    EditorButtonState { button: selectionButton8; selected: Boolean(root.value("textRole") === roleTile.modelData.name) }
                                    Accessible.onToggleAction: selectionButton8.click()
                                }
                            }
                            TapHandler { onTapped: root.applyRole(roleTile.modelData) }
                        }
                    }
                }
            }
        }
        Caption { text: qsTr("Theme aliases") }
        Grid {
            id: aliasGrid
            width: parent.width; columns: 2; spacing: 8
            Repeater {
                model: NativeData.text.aliases
                LV.LabelButton {
                    id: selectionButton9
                    required property var modelData
                    objectName: "textRole-" + modelData.name
                    width: (aliasGrid.width - 8) / 2; height: 22
                    text: modelData.name + " · " + modelData.size + " px"
                    tone: LV.AbstractButton.Default
                    onClicked: root.applyRole(modelData)

                    Accessible.checkable: selectionButton9.checkable
                    Accessible.checked: selectionButton9.checked
                    EditorButtonState { button: selectionButton9; selected: Boolean(root.value("textRole") === modelData.name) }
                    Accessible.onToggleAction: selectionButton9.click()
                }
            }
        }
    }
    LV.Label { visible: root.panelIndex === 0; width: parent.width; height: 11; style: caption; text: root.readout }

    Heading { visible: root.panelIndex > 0; label: "Text · " + root.tabs[root.panelIndex] }
    LV.InputField {
        id: preview
        visible: root.panelIndex > 0
        objectName: "textPreview"
        width: parent.width; height: root.panelIndex === 1 ? 46 : 80
        fieldMinHeight: root.panelIndex === 1 ? 46 : 80; clearButtonVisible: false; readOnly: true
        inputItem.visible: false
        Row {
            visible: root.panelIndex === 1
            anchors.fill: parent; anchors.margins: 12; spacing: 8
            LV.Label { width: 82; height: parent.height; style: body; text: "Aa"; verticalAlignment: Text.AlignVCenter }
            Column {
                width: parent.width - 90; spacing: 4
                LV.Label { width: parent.width; height: 13; style: body; text: root.value("field-1") }
                LV.Label { width: parent.width; height: 11; style: caption; color: LV.Theme.bodyColor; text: root.readout }
            }
        }
        LV.Label { visible: root.panelIndex === 2; x: 12; y: 12; width: parent.width - 24; height: 11; style: caption; color: LV.Theme.bodyColor; text: "Canvas · " + (root.engine && root.engine.editingHeight > 0 ? (root.engine.editingWidth / root.engine.editingHeight > 1.6 ? "16:9" : root.engine.editingWidth + ":" + root.engine.editingHeight) : "16:9") }
        Rectangle {
            visible: root.panelIndex === 3
            x: parent.width * 0.18; y: 20; width: parent.width * 0.64; height: 40
            radius: root.value("field-10") === "Sharp" ? 0 : Number(root.value("textCornerRadius"))
            color: Qt.rgba(0.17, 0.17, 0.17, root.value("field-6") / 100)
        }
        LV.Label {
            visible: root.panelIndex !== 1
            x: 12; width: parent.width - 24; height: 22
            y: root.panelIndex === 3 ? 29 : String(root.value("field-5")).indexOf("Top") === 0 ? 28 : String(root.value("field-5")).indexOf("Bottom") === 0 ? 48 : 38
            style: body
            text: root.panelIndex === 3 && root.value("field-0") === "Enter your text…" ? qsTr("Your words, in focus") : root.value("field-0")
            color: root.panelIndex === 3 ? NativeData.text.fillColor(root.value("textFill"), root.value("textCustomFill")) : LV.Theme.bodyColor
            opacity: root.panelIndex === 3 ? root.value("textOpacity") / 100 : 1
            horizontalAlignment: root.panelIndex === 3 || ["Bottom", "Center", "Top"].indexOf(root.value("field-5")) >= 0 ? Text.AlignHCenter : String(root.value("field-5")).indexOf("right") >= 0 || root.value("field-5") === "Right" ? Text.AlignRight : Text.AlignLeft
            elide: Text.ElideRight
        }
    }
    Column {
        visible: root.panelIndex === 1
        width: parent.width; spacing: 12
        ScopeRow { fieldId: "field-1" }
        NumericRow { fieldId: "field-2"; stepper: true }
        ScopeRow { fieldId: "textWeight" }
        NumericRow { fieldId: "textLineHeight" }
        ChoiceGroup { fieldId: "textWeight"; headingText: qsTr("Weight presets") }
        ChoiceGroup { fieldId: "textLinePreset" }
        Column {
            width: parent.width; spacing: 8
            ChoiceGroup { fieldId: "field-3"; columns: 3; buttonHeight: 30; alignmentGallery: true; icons: ["LeftAlign", "CenterAlign", "RightAlign", "ParagraphJustify", "LeftAlign", "RightAlign"] }
            ChoiceGroup { fieldId: "textVerticalAlignment"; columns: 3; buttonHeight: 30; alignmentGallery: true; icons: ["AlignTop", "AlignMiddle", "AlignBottom"] }
            ChoiceGroup { fieldId: "textDirection" }
        }
        SliderRow { fieldId: "field-8" }
    }
    Column {
        visible: root.panelIndex === 2
        width: parent.width; spacing: 12
        Column {
            width: parent.width; spacing: 8
            ChoiceGroup { fieldId: "field-5"; columns: 3; buttonHeight: 30 }
            LV.Label {
                width: parent.width; height: 11; style: caption; objectName: "textPlacementReadout"
                text: {
                    const anchor = String(root.value("field-5"))
                    const horizontal = anchor.toLowerCase().indexOf("left") >= 0 ? "AlignLeft" : anchor.toLowerCase().indexOf("right") >= 0 ? "AlignRight" : "AlignHCenter"
                    const vertical = anchor.indexOf("Top") === 0 ? "AlignTop" : anchor.indexOf("Bottom") === 0 ? "AlignBottom" : "AlignVCenter"
                    return horizontal + " | " + vertical
                }
            }
        }
        ChoiceGroup { fieldId: "textReference" }
        ToggleRow { fieldId: "field-9" }
        Column { width: parent.width; spacing: 8; ChoiceGroup { fieldId: "textWidthMode" } ChoiceGroup { fieldId: "textHeightMode" } }
        Pair { fields: ["textOffsetX", "textOffsetY"] }
        Pair { visible: root.value("textWidthMode") === "Fixed" || root.value("textHeightMode") === "Fixed"; fields: ["textBoxWidth", "textBoxHeight"] }
        Column { width: parent.width; spacing: 8; ChoiceGroup { fieldId: "textHorizontalConstraint"; columns: 3 } ChoiceGroup { fieldId: "textVerticalConstraint"; columns: 3 } }
        SliderRow { fieldId: "field-12" }
        SliderRow { fieldId: "field-14" }
    }
    Column {
        visible: root.panelIndex === 3
        width: parent.width; spacing: 12
        Column {
            width: parent.width; spacing: 8
            Caption { text: qsTr("Fill & opacity") }
            ScopeRow { fieldId: "textFill" }
            LV.InputField { visible: root.value("textFill") === "Custom"; width: parent.width; height: 22; fieldMinHeight: 22; clearButtonVisible: false; text: root.value("textCustomFill"); Accessible.name: qsTr("Custom text fill"); onAccepted: if (/^#[0-9a-fA-F]{6,8}$/.test(text)) root.edit("textCustomFill", text) }
            SliderRow { fieldId: "textOpacity" }
            SliderRow { fieldId: "field-6" }
            ChoiceGroup { fieldId: "textBlend"; columns: 3 }
        }
        Column {
            width: parent.width; spacing: 8
            Caption { text: qsTr("Color & range") }
            Pair { fields: ["textBrightness", "textContrast"] }
            Pair { fields: ["textSaturation", "textHue"] }
            Pair { fields: ["textGamma", "textSoftness"] }
            Pair { fields: ["textRangeMin", "textRangeMax"] }
            ChoiceGroup { fieldId: "textRangeChannel"; heading: false }
        }
        Column {
            width: parent.width; spacing: 8
            Caption { text: qsTr("Edge & shadow") }
            Pair { fields: ["textCornerRadius", "textStrokeWidth"] }
            Pair { fields: ["textStrokeOpacity", "textBlur"] }
            ScopeRow { fieldId: "textStrokeColor" }
            ToggleRow { fieldId: "textShadow" }
            Pair { fields: ["textShadowOpacity", "textShadowBlur"] }
            Pair { fields: ["textShadowX", "textShadowY"] }
            Pair { fields: ["textShadowSpread", "textFeather"] }
        }
        Column {
            width: parent.width; spacing: 8
            Caption { text: qsTr("Noise") }
            SliderRow { fieldId: "textNoise" }
            Pair { fields: ["textNoiseScale", "textNoiseSeed"] }
            ChoiceGroup { fieldId: "textNoisePattern"; heading: false }
            ToggleRow { fieldId: "textMonochrome" }
        }
        Column {
            width: parent.width; spacing: 8
            Heading { label: qsTr("Bubble") }
            Gallery { fieldId: "field-10"; entries: [{name: "Round", icon: "Comment"}, {name: "Sharp", icon: "Callout"}, {name: "Cloud", icon: "CloudBubbleEditableVector"}] }
        }
        Column {
            width: parent.width; spacing: 8
            ChoiceGroup { fieldId: "field-11"; columns: 3; buttonHeight: 30 }
            Pair { fields: ["textPointerLength", "textPointerWidth"] }
            Pair { fields: ["textPointerOffset", "textPointerRoundness"] }
        }
    }
    LV.LabelButton {
        visible: root.panelIndex > 0
        objectName: "textResetView"
        text: "Reset " + root.tabs[root.panelIndex].toLowerCase()
        height: 22
        tone: LV.AbstractButton.Borderless
        onClicked: root.resetView()
    }
    LV.ContextMenu {
        id: options
        property string fieldId: "field-1"
        showIconSlot: false
        items: NativeData.text.byId(fieldId).options.map(function(option) { return {label: option} })
        onItemTriggered: function(index) { root.edit(fieldId, items[index].label) }
    }
    LV.Label { visible: root.toolState.notice.length > 0; width: parent.width; style: caption; text: root.toolState.notice; wrapMode: Text.Wrap; sizeToContentHeight: true }
}
