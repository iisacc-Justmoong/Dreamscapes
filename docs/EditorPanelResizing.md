# Resizable editor tool panel

Reference: [Dreamscapes Elements panel, Figma 353:22022](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=353-22022&m=dev).

The desktop editor's right panel starts at 342 logical pixels, following the
updated [Figma 353:22614 editor layout](EditorLayout.md). Drag its left
boundary horizontally to change its width. The boundary has a 10-pixel hit
area and a horizontal resize cursor; double-click restores 342 pixels. The
panel normally ranges from 280 to 720 pixels, reserving 320 pixels for the
canvas where the window permits. A smaller window clamps the effective width
without discarding the requested width. Growing the window restores it.
Closing/reopening the panel and switching tools retain both width and tool
values for the editor session. Width is not a persisted user preference.
Mobile editors keep their existing tool sheets and have no resize boundary.

Titles and field captions start at the upper left. Parameter groups align to
the right content edge, inside the existing 17-pixel panel inset. Input fields
keep their 206-by-22 size where space permits and move below their caption
when the caption and field cannot fit beside each other. Long captions wrap.
Dimension fields remain stacked. Slider value inputs also keep their 206-pixel
width; a long caption moves the input below it when the heading cannot fit.
The slider underneath aligns right, shrinking from 320 pixels as needed. Color
input and picker form one right-aligned group. Toggles and preview controls
also align right. Choice and action buttons use their LVRS intrinsic widths,
keep their original left-to-right order, wrap without overlap, and align the
last button of every row to the right edge. The design's column counts cap the
number of buttons in each row. Wrapped content increases the scroll extent.

All visible inputs, labels, buttons, sliders, toggles and preview rows reuse
installed LVRS components. The application supplies panel geometry and pointer
handling; it does not fork or override LVRS control internals. Existing native
editing, capabilities, dialogs, tool drafts and generation bindings continue
through the same tool-state signals.

`desktopEditorPanelResizes` verifies pointer dragging, both width limits,
window clamping/restoration, double-click reset, tool values across panel
collapse/reopen, and mobile boundary removal. It checks the full 19-tool design
catalog at 280/342/432/720 pixels: caption origin, parameter edges, wrapping order,
row containment and caption/control separation. Existing Figma panel tests
retain semantic defaults, option interaction and local disclosure asset checks;
fixed historical field coordinates are superseded by this responsive layout.
Existing native toolbar and mobile sheet tests verify operation compatibility.

```sh
cmake --build build --target Dreamscapes DreamscapesGuiTests Dreamscapes_qmllint --parallel 4
ctest --test-dir build -R '^Dreamscapes\.(EditorPanelResize|EditorToolPanels|EditorCanvasGui)$' --output-on-failure
```

Build logs, layout captures, test logs and bundle verification are saved under
`build/verification/editor-panel-resize/`. Software-rendered GUI tests prove
layout and interaction; installed macOS verification is recorded separately.
