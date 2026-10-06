# Desktop editor layout

References: [Dreamscapes Editor, Figma 353:22614](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=353-22614&m=dev)
and [Editor Toolbar, Figma 353:21758](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=353-21758&m=dev).

The desktop editor uses a 54-pixel toolbar above the canvas work area. Its 19
LVRS tabs are 36 by 36 pixels, with a 4-pixel gap and 8-pixel outer inset.
The desktop toolbar is flat and opaque: `LV.Theme.surfaceSolid` fills its
entire rectangle, with zero corner radius and zero border width. Selected
desktop tabs use the LVRS selected fill without a border; their existing
LVRS corner radius and hover/pressed states remain available. Mobile keeps
its original rounded, bordered toolbar and selected tab styling.
Their original SVGs occupy 22-pixel slots at (7, 8). Desktop tabs display
icons and preserve tool names for accessibility and keyboard navigation.
All 19 Figma exports are byte-identical to the existing local SVGs; their
natural bounds, strokes, colors and source files are reused unchanged.

The left sidebar remains 181 pixels wide (48 in compact windows). The right
tool panel starts at 342 pixels with a 17-pixel content inset, producing
308 pixels of content. Titles stay upper-left and parameter groups align
right. Existing drag resizing, responsive wrapping, tool drafts and native
tool actions follow [EditorPanelResizing.md](EditorPanelResizing.md).

The native iiSharedCanvas surface fills the remaining work area below the
toolbar. Fitting a document centers both axes in that surface, unlike the
reference's bottom-aligned sample rectangle. Window resizing, panel resizing
and panel collapse/reopen refit the current document using the SDK's existing
viewport transform. Document dimensions and image content are dynamic.
Multiple-canvas navigation and frame playback reserve their own space when
needed; neither toolbar nor tool panel overlaps the canvas surface.

Desktop document commands no longer consume a second toolbar row. Open,
Save, Save As, Undo and Redo retain their standard shortcuts. A native LVRS
canvas context menu exposes Open, Save, Save As, Undo, Redo, Layers, Fit and
Paint Color through the same existing document dialogs and operations.
Right-click opens it; left-button editing and middle-button panning remain
with iiSharedCanvas. Multi-canvas Previous/Next buttons remain available.
Mobile retains its 84-pixel bottom toolbar, captions and document buttons.

`desktopEditorLayoutCentersCanvas` checks the reference geometry, all 19
icon slots, tool selection, zero desktop border/radius, opaque fill and
rendered corner pixels, centered native transforms across portrait,
landscape and square documents, three window sizes, three panel widths and
panel collapse/reopen. It tests the real context-menu Fit command after
panning and the mobile layout switch. Existing native editor tests use
standard shortcuts and real context-menu commands to verify painting,
erasing, history, color picking, layers and document persistence. Historical
432-pixel tool fixtures explicitly request that width; the current window
layout checks the new 342-pixel default.

```sh
cmake --build build --target Dreamscapes DreamscapesGuiTests Dreamscapes_qmllint --parallel 4
ctest --test-dir build -R '^Dreamscapes\.(EditorLayout|EditorPanelResize|EditorToolPanels|EditorCanvasGui|MultiCanvasGui|EditorElements)$' --output-on-failure
```

Build logs, asset hashes, native layout captures and bundle verification are
stored under `build/verification/editor-layout-353-22614/`. The flat toolbar
refinement is verified under `build/verification/editor-toolbar-flat/`.
Installed macOS verification is recorded separately from software-rendered
GUI tests.
