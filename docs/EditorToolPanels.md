# Editor tool bottom sheets

Source: [Dreamscapes / Editor / Unified Tool Detail Panels](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=165-3042).

The mobile toolbar opens one LVRS `Sheet` for the selected tool. All 19 panels and 428 fields/actions are represented. The sheet rises from the bottom; long panels scroll vertically. Close, Escape, a background press, and dragging the native grabber down dismiss it. Escape closes the panel before leaving the editor. Main owns system/keyboard insets; the sheet uses the editor's remaining bounds without applying those insets twice. Changing to a desktop layout or leaving the editor dismisses both the tool sheet and color picker.

## Component mapping

Only existing LVRS visual controls are used: `Sheet`, `Label`, `LabelButton`, `IconButton`, `InputField`, `ToggleSwitch`, `Slider`, `ListItem`, `ColorPickerButton`, and `ColorPicker`. The local QML files compose layouts and state; they introduce no custom visual control, paint implementation, or component library.

`EditorToolDefinitions.js` is the reviewed field catalog with Figma node IDs, stable field IDs, initial values, options, and numeric bounds. `EditorToolControl.qml` renders a field; `EditorToolPanel.qml` arranges its rows and actions; `EditorToolSheet.qml` manages the selected tool and its drafts. `CanvasEditor` connects toolbar selection to the sheet. Figma's compact 2-column slider/switch rows become 1-column below 340px of content width. Option buttons wrap; dimensions remain two labelled inputs. The native sheet adds a grabber and close affordance to the Figma panel.

| Toolbar | Fields/actions | Figma node |
| --- | ---: | --- |
| Elements | 18 | 196:3034 |
| Text | 16 | 196:3413 |
| Camera / Photo | 16 | 196:3703 |
| Asset | 16 | 196:3911 |
| File | 13 | 197:3529 |
| Background | 16 | 197:3649 |
| Audio track | 16 | 197:3907 |
| Canvas | 20 | 197:4160 |
| Generative | 31 | 197:4366 |
| Layers | 27 | 197:4816 |
| Select | 17 | 197:5165 |
| Color | 48 | 197:5507 |
| Effects | 48 | 197:6429 |
| Retouch | 23 | 197:7454 |
| Fill | 16 | 197:7857 |
| Brush | 27 | 197:8150 |
| Auto enhance | 20 | 197:8764 |
| Masking | 26 | 197:9090 |
| Eraser | 14 | 197:9637 |

## Interaction contract

Each tool keeps an independent in-memory draft for the editor's lifetime. Dismissing/reopening, tool switching, or resizing preserves it. Reset restores only the current tool's initial values. Options, selectors, switches, text, dimensions, sliders, and color controls update these drafts. Numeric inputs commit on Enter or focus loss; malformed, out-of-bounds, and inverted ranges restore the prior value. Numeric ranges are explicit application constraints because the design supplies examples, not editing-engine limits. Depth range preserves both endpoints. Colors commit through the LVRS picker or a six-digit hex field; cancelling the picker preserves the prior color.

This change implements the control panels, not a document model or rendering engine. Action buttons and preview rows emit `CanvasEditor.toolActionRequested(toolId, fieldId, values)` and display that the operation is not connected yet. They do not claim successful image editing, file saving, camera capture, or generation. Drafts are not persisted to disk and do not alter the source image. Existing Home/result canvas routing and generation controllers remain separate.

## Verification

Build in `build/` only:

```sh
cmake -S . -B build
cmake --build build --target DreamscapesGuiTests -j 6
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_DISABLE_DISK_CACHE=1 \
  build/bin/DreamscapesGuiTests editorToolSheets mobileEditorToolbarSlidesAndSelects canvasRoutesPreserveSelectionAndDraft
```

`editorToolSheets` covers all 19 panels at 402px and 320px, field counts, horizontal bounds, bottom anchoring, vertical scrolling and final-control reachability, Escape dismissal, actual option/switch/text/slider interactions, numeric rejection, color acceptance/cancellation, dimensions input, independent drafts, Reset, and reduced viewport bounds. The existing toolbar tests cover touch/mouse sliding, icons, keyboard navigation, resizing, and desktop visibility with sheet dismissal between selections. Set `DREAMSCAPES_CAPTURE_DIR` to a directory under `build/` to capture native Qt renders of Elements, Color, Effects, and Eraser. Physical iOS/Android behavior requires a separate device run.

The latest design context was retrieved for every panel. Figma's MCP plan limit interrupted some Effects row lookups; the saved unified field inventory and matching LVRS control specifications cover those remaining rows. No paid upgrade was used.

Verified on 2026-09-27: 19 panels / 428 rendered controls, 320px and 402px sheet tests, six toolbar layouts, desktop/mobile canvas routes, and the 402px sheet interaction suite in a real macOS Cocoa window. Logs and captures are under `build/editor-panel-verification/`. All 19 panels also passed a direct open-tool transition/render sweep. Physical iOS/Android validation is outstanding.

The final canonical bundle built and passed its `codesign --verify --deep --strict` post-step. Production-default bundle startup reached `root-loaded` with one window and no load errors in 35.65 seconds. The existing `packagedApplicationStarts` test still failed its 30-second root-load limit; this startup latency is not counted as a passing regression test. See `packaged-start-final.txt` and `packaged-native-start.json` in the verification directory.
