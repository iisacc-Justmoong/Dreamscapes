# Passive parameter rows

Editor, Layer, image generation and video generation panels use `App/Views/PanelRow.qml` for headings and parameter rows. The row is a plain `Item` composed from the installed LVRS labels, layouts and value controls. It does not inherit `ListItem` or `AbstractButton`, accept mouse buttons, observe hover, or enter the keyboard focus chain. Labels and unused row space therefore have no hover, pressed, selected or focus surface.

The row preserves the existing LVRS geometry: mini headings use 4 px horizontal and 2 px vertical insets; standard rows use 12 px horizontal and 8 px vertical insets with a 44 px minimum height. Label and description typography, selector, input and action widths retain their native LVRS values. Editor preview overrides retain the original 280 by 44 px geometry and disclosure position.

Interactions belong to the value control. Toggles, selectors, text inputs and action buttons retain their editing signals, validation, menu and keyboard behavior. Expanding a section, adding a control and opening a preview use the right-hand control instead of clicking the label. Video validation tooltips are attached to the input field. Actual selectable layer hierarchy items, asset cards and application navigation remain interactive.

`panelRowsKeepLabelsPassive` checks all seven used row variants. Rendered pixels must remain identical while the pointer hovers over or clicks a label, and no edit, navigation or action signal may fire. It then exercises the actual value controls. The editor panel catalog, Layer four-state, advanced-image draft/control and video workspace tests check integration with the real panel presenters and retain their geometry and editing contracts.
