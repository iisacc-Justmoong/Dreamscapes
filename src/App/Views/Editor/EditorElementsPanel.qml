pragma ComponentBehavior: Bound
import QtQuick
import Dreamscapes.Storage 1.0
import LVRS 1.0 as LV
import ".." as Views

Column {
    id: root
    objectName: "editorElementsPanel"
    required property var toolState
    readonly property var engine: toolState.engine
    // Focus loss can run while the loader is replacing this panel. Keep its
    // inputs bound to Elements rather than the newly selected tool's fields.
    readonly property var values: toolState.nativeModel.elementValues
    readonly property string currentViewId: toolState.elementsViewId
    readonly property var view: NativeData.elements.view(currentViewId)
    readonly property var candidates: engine ? engine.elementCandidates : []
    readonly property var activeCandidate: toolState.nativeModel.activeElementCandidate
    readonly property int selectionCount: engine ? engine.elementSelectionCount : 0
    readonly property bool editable: !activeCandidate || activeCandidate.editable
    readonly property var presentedFields: toolState.nativeModel.elementFields
    readonly property bool modalActive: options.visible
    readonly property real inputWidth: Math.min(206, width * 206 / 398)
    readonly property var scopedCandidates: toolState.nativeModel.scopedElementCandidates
    width: 398
    spacing: 12

    function asset(name) { return Qt.resolvedUrl("Assets/Elements/" + name + ".svg") }
    function buttonColor(control) {
        return control.interaction.surfacePhase === "disabled" ? control.backgroundColorDisabled
            : control.interaction.surfacePhase === "press" || control.checked ? control.backgroundColorPressed
            : control.interaction.surfacePhase === "hover" ? control.backgroundColorHover
            : control.backgroundColor
    }
    function value(id) { return values[id] }
    function fieldEnabled(id) { const revision = toolState.nativeModel.revision; return toolState.nativeModel.elementFieldEnabled(id) }
    function selectView(id) { toolState.nativeModel.selectElementView(id); options.close() }
    function edit(id, next) { toolState.nativeModel.editElement(id, next) }
    function chooseShape(entry) { toolState.nativeModel.chooseElementShape(entry); options.close() }
    function resetView() { toolState.nativeModel.resetElementView() }
    function setSelected(item, checked) { toolState.nativeModel.setElementSelected(item.id, checked) }
    function selectionResult(success) { toolState.notice = success ? "" : engine.error }
    function selectAll() { toolState.nativeModel.elementSelectionAction("all") }
    function invertSelection() { toolState.nativeModel.elementSelectionAction("invert") }
    function clearSelection() { toolState.nativeModel.elementSelectionAction("clear") }
    function inspectFirst() { toolState.nativeModel.elementSelectionAction("inspect"); options.close() }
    function formatted(id, index) { const revision = toolState.nativeModel.revision; return toolState.nativeModel.formattedElement(id, index === undefined ? -1 : index) }
    function commitNumber(id, index, text) { toolState.nativeModel.commitElementNumber(id, index === undefined ? -1 : index, text) }

    component Heading: Views.PanelRow {
        type: LV.ListItem.Mini
        showLeadingIcon: false
        minItemWidth: 0
        miniItemWidth: 0
        width: parent.width
        height: 17
    }
    component Description: LV.Label {
        style: caption
        color: LV.Theme.bodyColor
        opacity: 0.8
        width: parent.width
        wrapMode: Text.WordWrap
        sizeToContentHeight: true
        lineHeight: 14
    }
    component ElementIcon: LV.IconButton {
        id: button
        required property string assetName
        property bool selectionChecked: false
        property int artworkSize: assetName === "auto-refresh" ? 16 : 18
        property real artworkOffsetX: 0
        checked: selectionChecked
        autoExclusive: checkable
        // Navigation remains owned by the panel view, including repeated clicks.
        onToggled: checked = Qt.binding(function() { return button.selectionChecked })
        Accessible.checkable: button.checkable
        Accessible.checked: button.checked
        Accessible.onToggleAction: button.click()
        // Selection holds the pressed surface of this fixed button tone.
        Binding {
            target: button.background
            property: "color"
            when: button.checkable
            value: root.buttonColor(button)
        }
        width: 34; height: 34
        iconSize: 18
        horizontalPadding: 8; verticalPadding: 8
        tone: LV.AbstractButton.Default
        iconSource: root.asset(assetName)
        contentItem: Item {
            implicitWidth: 18; implicitHeight: 18
            Image {
                objectName: button.objectName + "_icon"
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: button.artworkOffsetX
                width: button.artworkSize; height: width
                source: button.iconSource
                sourceSize: Qt.size(width * 4, height * 4)
                fillMode: Image.PreserveAspectFit
            }
        }
    }
    component NumberField: LV.InputField {
        id: input
        required property string fieldId
        property int dimension: -1
        objectName: "elementsInput-" + fieldId + (dimension < 0 ? "" : "-" + dimension)
        height: 22; fieldMinHeight: 22
        clearButtonVisible: false
        property bool textModified: false
        enabled: root.fieldEnabled(fieldId)
        text: root.formatted(fieldId, dimension)
        Accessible.name: NativeData.elements.field(fieldId).label + (dimension === 0 ? " Width" : dimension === 1 ? " Height" : "")
        function commit() {
            textModified = false
            root.commitNumber(fieldId, dimension, text)
            text = Qt.binding(function() { return root.formatted(input.fieldId, input.dimension) })
        }
        onAccepted: commit()
        onTextEdited: textModified = true
        onActiveFocusChanged: if (!activeFocus && textModified) commit()
    }
    component NumericControl: Column {
        id: numeric
        required property string fieldId
        width: parent.width
        spacing: 8
        Row {
            width: parent.width; height: 22; spacing: 22
            Heading { label: NativeData.elements.field(numeric.fieldId).label; width: parent.width - root.inputWidth - 22; height: 22 }
            NumberField { fieldId: numeric.fieldId; width: root.inputWidth }
        }
        LV.Slider {
            id: slider
            objectName: "elementsSlider-" + numeric.fieldId
            width: parent.width; height: 22
            enabled: root.fieldEnabled(numeric.fieldId)
            from: NativeData.elements.field(numeric.fieldId).minimum
            to: NativeData.elements.field(numeric.fieldId).maximum
            stepSize: NativeData.elements.field(numeric.fieldId).step
            value: Number(root.value(numeric.fieldId))
            size: LV.Slider.Mini
            showMinMax: false; showSymbol: false; showTicks: false; showLabels: false
            Accessible.name: NativeData.elements.field(numeric.fieldId).label
            onMoved: root.edit(numeric.fieldId, value)
            handle: Item {
                implicitWidth: slider.rangeInset * 2
                implicitHeight: slider.controlHeight
                width: Math.min(implicitWidth, slider.availableWidth); height: slider.height
                x: slider.leftPadding + Math.max(0, Math.min(1, slider.displayedPosition)) * slider.rangeWidth
                Image {
                    objectName: "elementsThumb-" + numeric.fieldId
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 1
                    width: 18; height: 18
                    source: root.asset(root.currentViewId === "polygon" ? "thumb-650-15793" : root.currentViewId === "fill" ? "thumb-650-15795" : root.currentViewId === "stroke" ? "thumb-650-15796" : "thumb")
                    sourceSize: Qt.size(72, 72)
                    visible: slider.effectiveThumbVisible
                }
                Image {
                    anchors.centerIn: parent
                    width: 18; height: 18
                    source: root.asset("focus-ring")
                    sourceSize: Qt.size(72, 72)
                    visible: slider.effectiveFocusRing
                }
            }
        }
    }
    component DimensionsControl: Column {
        width: parent.width
        spacing: 2
        Heading { label: qsTr("Size") }
        Row {
            width: parent.width; spacing: 8; height: 22
            Repeater {
                model: 2
                Row {
                    id: dimensionRow
                    required property int index
                    width: (parent.width - 8) / 2; height: 22; spacing: 8
                    LV.Label { style: description; text: dimensionRow.index === 0 ? qsTr("Width") : qsTr("Height"); height: 22; width: implicitWidth; verticalAlignment: Text.AlignVCenter }
                    NumberField { fieldId: "field-0"; dimension: dimensionRow.index; width: parent.width - parent.children[0].width - 8 }
                }
            }
        }
    }
    component ToggleControl: Row {
        id: toggle
        required property string fieldId
        width: parent.width; height: 22; spacing: 8
        Heading { label: NativeData.elements.field(toggle.fieldId).label; width: parent.width - 46; height: 22 }
        LV.ToggleSwitch {
            objectName: "elementsToggle-" + toggle.fieldId
            trackWidth: 38; trackHeight: 22
            enabled: root.editable
            checked: Boolean(root.value(toggle.fieldId))
            Accessible.name: NativeData.elements.field(toggle.fieldId).label
            onToggled: root.edit(toggle.fieldId, checked)
        }
    }
    component ChoicesControl: Column {
        id: choices
        required property string fieldId
        readonly property var entries: NativeData.elements.field(fieldId).options
        width: parent.width; spacing: 8
        Heading { label: NativeData.elements.field(choices.fieldId).label }
        Row {
            width: parent.width; spacing: 8; height: 22
            Repeater {
                model: choices.entries
                Item {
                    id: slot
                    required property int index
                    required property string modelData
                    width: (parent.width - (choices.entries.length - 1) * 8) / choices.entries.length
                    height: 22
                    LV.LabelButton {
                        id: choiceButton
                        readonly property bool selectionChecked: root.value(choices.fieldId) === slot.modelData
                        objectName: "elementsChoice-" + choices.fieldId + "-" + slot.index
                        text: slot.modelData
                        width: Math.min(implicitWidth, parent.width); height: 22
                        horizontalPadding: 8; verticalPadding: 4.5
                        enabled: root.fieldEnabled(choices.fieldId)
                        checkable: true
                        autoExclusive: true
                        checked: selectionChecked
                        // Image opens a picker before changing the accepted fill.
                        onToggled: checked = Qt.binding(function() { return choiceButton.selectionChecked })
                        tone: LV.AbstractButton.Default
                        Accessible.name: NativeData.elements.field(choices.fieldId).label + ": " + slot.modelData
                        Accessible.checkable: choiceButton.checkable
                        Accessible.checked: choiceButton.checked
                        Accessible.onToggleAction: choiceButton.click()
                        Binding {
                            target: choiceButton.background
                            property: "color"
                            value: root.buttonColor(choiceButton)
                        }
                        onClicked: {
                            if (choices.fieldId === "field-2" && slot.modelData === "Image") root.toolState.nativeModel.requestElementImageFill()
                            else root.edit(choices.fieldId, slot.modelData)
                        }
                    }
                }
            }
        }
    }
    component ColorControl: Column {
        id: colorControl
        required property string fieldId
        width: parent.width; spacing: 8
        Heading { label: NativeData.elements.field(colorControl.fieldId).label }
        Row {
            width: parent.width; height: 28; spacing: 8
            LV.InputField {
                id: colorInput
                objectName: "elementsInput-" + colorControl.fieldId
                width: parent.width - 36; height: 22; y: 3; fieldMinHeight: 22
                clearButtonVisible: false
                property bool textModified: false
                enabled: root.editable
                text: root.value(colorControl.fieldId)
                Accessible.name: NativeData.elements.field(colorControl.fieldId).label
                function commit() {
                    textModified = false
                    const hex = text.trim().toUpperCase()
                    if (/^#[0-9A-F]{6}([0-9A-F]{2})?$/.test(hex)) root.edit(colorControl.fieldId, hex)
                    text = Qt.binding(function() { return root.value(colorControl.fieldId) })
                }
                onAccepted: commit()
                onTextEdited: textModified = true
                onActiveFocusChanged: if (!activeFocus && textModified) commit()
            }
            LV.ColorPickerButton {
                objectName: "elementsColorPicker-" + colorControl.fieldId
                width: 28; height: 28
                enabled: root.editable
                currentColor: root.value(colorControl.fieldId)
                Accessible.name: NativeData.elements.field(colorControl.fieldId).label
                onClicked: root.toolState.chooseColor(colorControl.fieldId)
            }
        }
    }
    component ScopeControl: Row {
        id: scope
        required property string scopeId
        required property string label
        property string display: ""
        width: parent.width; height: 22; spacing: 22
        Heading { label: scope.label; width: parent.width - root.inputWidth - 22; height: 22 }
        LV.ComboBox {
            id: combo
            objectName: "elementsScope-" + scope.scopeId
            text: scope.display
            width: root.inputWidth; height: 20; y: 1
            arrow: LV.Stepper.Down
            Accessible.name: scope.label
            enabled: scope.scopeId !== "active" || root.candidates.length > 0
            onClicked: { options.scopeId = scope.scopeId; options.openFor(this, 0, height) }
            // Preserve the native LVRS interaction and use the supplied full
            // 18 px Figma arrow export in its original slot.
            Component.onCompleted: {
                for (let i = 0; i < children.length; ++i) {
                    const child = children[i]
                    if (child.arrow !== undefined && child.tone !== undefined) child.visible = false
                }
            }
            Image {
                objectName: "elementsScopeArrow-" + scope.scopeId
                x: combo.indicatorX; y: combo.indicatorY
                width: 18; height: 18
                source: root.asset("arrow-down-tone-primary")
                sourceSize: Qt.size(72, 72)
                opacity: combo.enabled ? 1 : 0.38
            }
        }
    }
    component ActionPalette: Row {
        id: palette
        required property var entries
        property string selectedId: ""
        property bool selectionActions: false
        width: parent.width; height: 72; spacing: 8
        Repeater {
            model: palette.entries
            Column {
                id: action
                required property var modelData
                width: (palette.width - (palette.entries.length - 1) * 8) / palette.entries.length
                height: 72; spacing: 4; topPadding: 4
                ElementIcon {
                    objectName: palette.selectionActions ? "elementsSelectionAction-" + action.modelData.id : action.modelData.objectName
                    assetName: action.modelData.icon
                    anchors.horizontalCenter: parent.horizontalCenter
                    tone: LV.AbstractButton.Default
                    Accessible.name: action.modelData.name
                    checkable: palette.selectionActions
                    selectionChecked: palette.selectedId === action.modelData.id
                    onClicked: {
                        if (palette.selectionActions) {
                            if (action.modelData.id === "clear") root.clearSelection()
                            root.selectView(action.modelData.id)
                        } else if (action.modelData.id === "all") root.selectAll()
                        else if (action.modelData.id === "invert") root.invertSelection()
                        else root.clearSelection()
                    }
                }
                LV.Label {
                    style: body; text: action.modelData.name
                    width: parent.width; height: 26
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
    component SelectionList: Column {
        width: parent.width; spacing: 8
        Repeater {
            model: root.scopedCandidates
            Rectangle {
                id: entry
                required property int index
                required property var modelData
                objectName: "elementsSelectionRow-" + index
                width: parent.width; height: 36
                radius: LV.Theme.radiusMd
                color: "transparent"
                TapHandler { onTapped: function(eventPoint) { if (eventPoint.position.x >= 37) root.setSelected(entry.modelData, !entry.modelData.selected) } }
                LV.CheckBox {
                    objectName: "elementsSelectionCheck-" + entry.index
                    x: 12; anchors.verticalCenter: parent.verticalCenter
                    boxSize: 17
                    checkedAssetSourceEnabled: root.asset("checkbox")
                    checked: entry.modelData.selected
                    Accessible.name: entry.modelData.name
                    onClicked: root.setSelected(entry.modelData, checked)
                }
                LV.Label { x: 37; width: parent.width - 49; height: 36; style: body; text: entry.modelData.name; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
            }
        }
    }

    Item {
        objectName: "elementsHeader"
        width: parent.width; height: 30
        LV.IconSegmentedControl {
            forceBorderlessTone: false
            ElementIcon { objectName: "elementsViewTab0"; assetName: "direct-select"; width: 22; height: 22; horizontalPadding: 2; verticalPadding: 2; tone: LV.AbstractButton.Borderless; Accessible.name: qsTr("Selection"); checkable: true; selectionChecked: root.view.tab === 0; onClicked: root.selectView("selection") }
            ElementIcon { objectName: "elementsViewTab1"; assetName: "rectangle"; width: 22; height: 22; horizontalPadding: 2; verticalPadding: 2; tone: LV.AbstractButton.Borderless; Accessible.name: qsTr("Shape"); checkable: true; selectionChecked: root.view.tab === 1; onClicked: root.selectView(NativeData.elements.shapeView(root.value("selector"))) }
            ElementIcon { objectName: "elementsViewTab2"; assetName: "fill"; width: 22; height: 22; horizontalPadding: 2; verticalPadding: 2; tone: LV.AbstractButton.Borderless; Accessible.name: qsTr("Fill"); checkable: true; selectionChecked: root.view.tab === 2; onClicked: root.selectView("fill") }
            ElementIcon { objectName: "elementsViewTab3"; assetName: "stroke"; width: 22; height: 22; horizontalPadding: 2; verticalPadding: 2; tone: LV.AbstractButton.Borderless; Accessible.name: qsTr("Stroke"); checkable: true; selectionChecked: root.view.tab === 3; onClicked: root.selectView("stroke") }
        }
        ElementIcon {
            objectName: "elementsHeaderAction"
            anchors.right: parent.right
            width: 30; height: 30; horizontalPadding: 6; verticalPadding: 6
            assetName: root.view.tab === 0 ? "unselect-all" : "auto-refresh"
            artworkOffsetX: 2
            tone: LV.AbstractButton.Borderless
            enabled: root.view.tab === 0 || root.editable
            Accessible.name: root.view.tab === 0 ? qsTr("Clear selection") : qsTr("Reset Elements settings")
            onClicked: root.view.tab === 0 ? root.clearSelection() : root.resetView()
        }
    }
    Heading {
        objectName: "elementsHeading"
        visible: root.view.tab !== 0
        label: "Shape · " + (root.view.shape || (root.currentViewId === "fill" ? "Fill" : "Stroke"))
    }
    Row {
        objectName: "elementsSelectionCount"
        visible: root.view.tab === 0
        width: parent.width; height: 22
        Row {
            width: parent.width - 70; height: 22; spacing: 4
            LV.Label { text: root.selectionCount; style: body; width: implicitWidth; height: 22; verticalAlignment: Text.AlignVCenter }
            LV.Label { text: qsTr("selected"); style: body; width: implicitWidth; height: 22; verticalAlignment: Text.AlignVCenter }
        }
        LV.LabelButton { objectName: "elementsSelectAll"; text: qsTr("Select all"); width: 70; height: 22; tone: LV.AbstractButton.Borderless; textColor: LV.Theme.accent; onClicked: root.selectAll() }
    }
    Row {
        objectName: "elementsShapePalette"
        visible: root.view.tab === 1
        width: parent.width; height: 72; spacing: 8
        Repeater {
            model: NativeData.elements.shapes
            Column {
                id: shape
                required property int index
                required property var modelData
                width: (parent.width - 24) / 4; height: 72; spacing: 4; topPadding: 4
                ElementIcon {
                    objectName: "elementsShapeChoice-" + shape.index
                    assetName: shape.modelData.icon
                    anchors.horizontalCenter: parent.horizontalCenter
                    tone: LV.AbstractButton.Default
                    Accessible.name: shape.modelData.name
                    checkable: true; selectionChecked: root.currentViewId === shape.modelData.id
                    onClicked: root.chooseShape(shape.modelData)
                }
                LV.Label { style: body; text: shape.modelData.name; width: parent.width; height: 26; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap }
            }
        }
    }
    ActionPalette {
        objectName: "elementsSelectionPalette"
        visible: root.view.tab === 0
        selectionActions: true
        selectedId: root.currentViewId
        entries: [{id: "inspect", name: qsTr("Inspect"), icon: "direct-select"}, {id: "multiple", name: qsTr("Multiple"), icon: "group"}, {id: "clear", name: qsTr("Clear"), icon: "unselect-all"}]
    }
    Heading {
        objectName: "elementsSelectionHeading"
        visible: root.view.tab === 0 && root.currentViewId !== "selection"
        label: root.currentViewId === "inspect" ? "Inspect · " + (root.activeCandidate ? root.activeCandidate.shape : qsTr("No selection")) : root.currentViewId === "multiple" ? qsTr("Multiple · Shape selection") : qsTr("Clear · Selection reset")
    }
    ScopeControl {
        visible: root.currentViewId === "inspect"
        scopeId: "active"; label: qsTr("Active shape")
        display: root.activeCandidate ? root.activeCandidate.name : qsTr("No selection")
    }
    Row {
        visible: root.currentViewId === "multiple"
        width: parent.width; height: 22; spacing: 8
        Heading { label: qsTr("Multiple selection"); width: parent.width - 46; height: 22 }
        LV.ToggleSwitch { objectName: "elementsMultipleSelectionToggle"; trackWidth: 38; trackHeight: 22; checked: root.toolState.elementsMultipleSelection; onToggled: { root.toolState.elementsMultipleSelection = checked; if (!checked && root.engine && root.activeCandidate) root.selectionResult(root.engine.selectElement(root.activeCandidate.id)) } }
    }
    ScopeControl {
        visible: root.currentViewId === "multiple" || root.currentViewId === "clear"
        scopeId: "selection"; label: qsTr("Selection scope")
        display: root.toolState.elementsSelectionScope
    }
    Description {
        visible: root.currentViewId === "clear"
        text: root.selectionCount ? qsTr("%1 shapes selected. Choose a shape to inspect.").arg(root.selectionCount) : qsTr("No shapes selected. Choose a shape to start again.")
    }
    Repeater {
        model: root.currentViewId === "inspect" && !root.activeCandidate ? [] : root.presentedFields
        Loader {
            id: fieldLoader
            required property string modelData
            readonly property string fieldId: modelData
            readonly property var spec: NativeData.elements.field(fieldId)
            objectName: "elementsControl-" + fieldId
            width: root.width
            height: item ? item.implicitHeight : 0
            sourceComponent: spec.type === "Dimensions" ? dimensionComponent : spec.type === "Toggle" ? toggleComponent : spec.type === "Color" ? colorComponent : spec.type === "Choices" || spec.type === "Segmented" ? choiceComponent : numericComponent
            Component { id: dimensionComponent; DimensionsControl {} }
            Component { id: toggleComponent; ToggleControl { fieldId: fieldLoader.fieldId } }
            Component { id: colorComponent; ColorControl { fieldId: fieldLoader.fieldId } }
            Component { id: choiceComponent; ChoicesControl { fieldId: fieldLoader.fieldId } }
            Component { id: numericComponent; NumericControl { fieldId: fieldLoader.fieldId } }
        }
    }
    ScopeControl { visible: root.currentViewId === "fill"; scopeId: "apply"; label: qsTr("Apply to"); display: qsTr("Selected shape") }
    SelectionList { visible: root.currentViewId === "multiple" || root.currentViewId === "clear" }
    ActionPalette {
        visible: root.currentViewId === "multiple"
        entries: [{id: "all", name: qsTr("Select all"), icon: "group", objectName: "elementsMultipleSelectAll"}, {id: "invert", name: qsTr("Invert"), icon: "auto-refresh", objectName: "elementsInvert"}, {id: "clear", name: qsTr("Clear"), icon: "unselect-all", objectName: "elementsMultipleClear"}]
    }
    Row {
        visible: root.currentViewId === "clear"
        width: parent.width; height: 28; spacing: 12
        LV.LabelButton { objectName: "elementsClearSelectAll"; width: (parent.width - 12) / 2; height: 28; text: qsTr("Select all"); tone: LV.AbstractButton.Primary; enabled: root.candidates.length > 0; onClicked: root.selectAll() }
        LV.LabelButton { objectName: "elementsInspectFirst"; width: (parent.width - 12) / 2; height: 28; text: qsTr("Inspect %1").arg(root.candidates.length ? root.candidates[0].name : qsTr("shape")); tone: LV.AbstractButton.Default; enabled: root.candidates.length > 0; onClicked: root.inspectFirst() }
    }
    Description {
        visible: root.currentViewId === "line"
        text: qsTr("%1 px stroke · %2 cap · %3 at end").arg(root.value("field-10")).arg(root.value("field-11")).arg(root.value("field-13"))
    }
    Row {
        visible: root.currentViewId === "line"
        width: parent.width; height: 34; spacing: 8
        ElementIcon { objectName: "elementsEditStroke"; assetName: "stroke"; onClicked: root.selectView("stroke"); Accessible.name: qsTr("Edit stroke and arrowheads") }
        LV.Label { style: body; text: qsTr("Edit stroke and arrowheads"); width: parent.width - 42; height: 34; verticalAlignment: Text.AlignVCenter; TapHandler { onTapped: root.selectView("stroke") } }
    }
    Description {
        visible: NativeData.presentation.indexOf(["selection", "inspect", "multiple", "clear", "fill"], root.currentViewId) >= 0
        text: root.currentViewId === "selection" ? qsTr("Choose a shape to inspect its geometry.") : root.currentViewId === "inspect" ? (!root.activeCandidate ? qsTr("Choose a shape to inspect its geometry.") : !root.editable ? qsTr("This shape can be inspected but its geometry cannot be edited.") : qsTr("Inspect the active shape and its appearance.")) : root.currentViewId === "multiple" ? qsTr("Click a row or checkbox to add or remove a shape.") : root.currentViewId === "clear" ? qsTr("Use Inspect for one shape or Multiple for a selection.") : qsTr("Fill and opacity apply to the selected shape.")
    }
    LV.ContextMenu {
        id: options
        property string scopeId: "active"
        showIconSlot: false
        items: scopeId === "active" ? NativeData.presentation.choices(root.candidates, "name", "id", "elementId") : scopeId === "selection" ? [{label: "All shapes"}, {label: "Selected shapes"}] : [{label: "Selected shape"}]
        onItemTriggered: function(index) {
            if (scopeId === "active" && root.engine) root.selectionResult(root.engine.selectElement(items[index].elementId))
            else if (scopeId === "selection") root.toolState.elementsSelectionScope = items[index].label
        }
    }
    Description { visible: root.toolState.notice.length > 0; text: root.toolState.notice }
}
