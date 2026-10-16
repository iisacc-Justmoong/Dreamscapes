import QtQuick
import LVRS 1.0 as LV

// Selection is the pressed surface of the button's existing role, never a tone.
QtObject {
    id: state
    objectName: "editorButtonState"
    required property LV.AbstractButton button
    property bool selected: false
    property bool selectable: true

    readonly property Binding checkableBinding: Binding {
        target: state.button
        property: "checkable"
        value: state.selectable
    }
    readonly property Binding checkedBinding: Binding {
        target: state.button
        property: "checked"
        value: state.selectable && state.selected
    }
    readonly property Connections checkedEvents: Connections {
        target: state.button
        function onToggled() {
            // The accepted model owns selection, including rejected edits and
            // repeated mouse, keyboard or accessibility activation.
            state.button.checked = Qt.binding(function() { return state.selectable && state.selected })
        }
    }
    readonly property Binding surfaceBinding: Binding {
        target: state.button.background
        property: "color"
        value: !state.button.effectiveEnabled ? state.button.backgroundColorDisabled
            : state.button.interaction.surfacePhase === "press" || (state.selectable && state.selected) ? state.button.backgroundColorPressed
            : state.button.interaction.surfacePhase === "hover" ? state.button.backgroundColorHover
            : state.button.backgroundColor
    }
}
