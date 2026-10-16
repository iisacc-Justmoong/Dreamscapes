# Editor text panel

The editor's Text tool uses the four current Dreamscapes Figma views:

| View | Figma node | Reference frame |
| --- | --- | --- |
| Content | `627:95323` | 432 × 653 |
| Typography | `631:15674` | 432 × 718 |
| Layout | `631:15675` | 432 × 949 |
| Appearance | `631:15676` | 432 × 1636 |

`EditorTextPanel.qml` provides the same LVRS-based component on the desktop dock
and the mobile sheet. The surrounding dock owns the 16 px padding, 1 px border,
16 px corner radius and vertical scroll. At the reference width, the content is
398 px wide. Controls reflow within a resized panel. Original Figma SVGs are
local resources in `Assets/Text`; the panel never depends on temporary asset URLs.

When switching between Text and Elements, an outgoing Text panel keeps its
inputs bound to its own saved text draft until its loader is destroyed. Edits
are accepted only while Text is the active tool, so focus-loss commits during
panel replacement cannot read Elements values or forward edits to another tool.

Content provides the five usage roles, multiline input, all eight `LV.Label`
typography roles and four Theme aliases. The default is Body, 13 px, Medium.
Selecting a typography token updates font size, weight and token line height
together. Usage and typography role are separate values.

Typography provides typeface selection, validated pixel size and stepping,
weight selection/presets, explicit line height/presets, six horizontal alignment
modes, vertical alignment, bidirectional text and tracking. Glyph runs use Qt's
native shaping before their outlines are passed to iiSharedCanvas. The text
remains a native vector layer while its appearance can be expressed by paths.

Numeric inputs become modified only after a real `textEdited` event. Unedited
focus loss does not emit an edit or accept a rounded display string; `accepted`
and explicit `commit()` continue to validate and accept input. For example,
13 px text using the 120% line preset stores a 15.6 px line height even when its
input displays 16 px. Switching to Color without typing retains the exact line
height, preset, native geometry and document history.

Layout provides nine anchors, Canvas/Artboard/Selection reference, auto-fit,
Auto/Fixed/Fill width, Hug/Fixed/Fill height, offsets, positional/stretch
constraints, padding and bottom inset. Fixed dimensions are exposed when fixed
sizing is selected. Choosing placement/reference/offset/constraints enables
placement; the initial Free Text usage still places text at the canvas click.
Selection-relative placement requires an actual selection. Constraints apply to
the current text layout; there is no persistent resize solver for reopened text.

Appearance provides text fill/custom color, opacity, backdrop, supported blend
modes, color/range adjustments, corners, strokes, blur, shadow, feather,
deterministic uniform/Gaussian noise, bubbles and eight pointer directions plus
None. Raster-only effects generate a bitmap from the authored text and native
paths using iiSharedCanvas rendering/effects. Clearing these effects restores
the vector representation. The selected text remains editable in the session,
including after switching between vector and bitmap representations.

The existing SDK renderer supports Normal, Multiply, Screen and Overlay. Soft
light and Difference are visible but disabled. Blue noise and proportional
Scale constraints are also disabled, with accessibility descriptions. They are
not silently mapped to another operation. Reset actions restore the current
view's settings; the Content reset restores all text settings.

Document saving and export preserve the resulting geometry, pixels, layer
properties and caption timing. As with the existing editor text tool, original
strings/font authoring settings are session data: reopening a saved document
does not restore those authoring controls. Raster effects preserve their final
pixels; this feature does not change the IISC format or introduce sidecar files.

Validation: `Dreamscapes.EditorText` covers four desktop states, local SVG
loading/18 px slots, editing through QML, resets and panel resizing.
`editorTextPanelMobile` covers the same tabs and scrolling at host-simulated mobile
widths. `Dreamscapes.EditorTools` covers native text editing, shaping, layout,
appearance, deterministic effects, vector restoration and exported pixels.
These are host checks; physical iOS/Android execution is separate.

`editorColorToolSelectionDoesNotEditDocument` includes
`color-fractional-text-desktop` and `color-fractional-text-mobile`. Each creates
actual two-line native text, applies the 120% preset at the default 13 px font,
then focuses its rounded line-height input without typing before selecting Color.
The cases compare complete IISC bytes, rendered pixels, exact 15.6 px line height,
the retained 120% preset, revision, selection and Undo/Redo state across panel
reopening, view navigation and a Brush round trip. The existing Redo is executed
to verify the original edit remains available. Run the focused target below;
pass status is determined from its execution log.

```sh
ctest --test-dir build -R '^Dreamscapes.EditorColorPassiveSelection$' --output-on-failure
```
