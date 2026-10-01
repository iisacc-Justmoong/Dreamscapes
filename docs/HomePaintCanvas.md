# Home paint canvas

Dreamscapes implements Figma `bn8O4AHKr1X9DWnhR1TgEy`, node `261:3206`,
with the installed LVRS controls and iiSharedCanvas editing engine. The requested
icon toolbar replaces the design's text buttons. `HomePaintCanvas.qml` owns the
presentation, `HomeCanvas` owns the consumer's attachment and input conversion,
and iiSharedCanvas owns raster pixels, strokes, rendering and undo history.

The current desktop Home removes the inline paint canvas and its toggle, placing
QuickGenerate's prompt first. QuickGenerate keeps the existing hidden component
as its reference-image backing state; its attachment cards render in the external
slot below the prompt, with canvas drag gestures disabled. Validation errors appear
in the composer notice. The standalone component's painting behavior described
below remains available for component-level use and tests.

The plus button to the
left of Image opens a multiple-image file picker. Attached images appear between the prompt and generation controls; dragging a thumbnail into the canvas composites its decoded
pixels into the selected raster layer at the drop location. The image is centered
at the pointer and kept within the canvas. Small inputs retain their native pixel
size; oversized inputs shrink to fit without changing their ratio. A paste is one
undoable edit, supports redo and subsequent brush/eraser edits, and remains after
the source attachment is removed. Removing the reference does not erase pixels.
An empty attachment list consumes no space.

The 22px toolbar has an 18px exported Figma brush icon and installed LVRS eraser,
undo, redo and clear icons. LVRS ColorPickerButton opens the native
LVRS wheel ColorPicker; Apply commits the brush color and Cancel preserves it.
The toolbar displays unchanged fixed-width LVRS Mini Slider controls to the left of the color wheel:
brush size is 1–500 px in 1 px steps, and opacity is 0–100% in 1% steps (mapped
to the SDK's 0–1 value). Icons identify size and opacity; numerical readouts show their current values.
At narrow widths the fixed-width control groups wrap. A 0% stroke paints no pixels.
Hover tooltips and accessible names identify the icon actions. Narrow homes keep the plus button and use a
22px Generate arrow to avoid squeezing the ratio and quantity controls.

Canvas output dimensions follow QuickGenerate: 1:1 = 1024 × 1024, 4:3 = 1368 ×
1024, 3:4 = 1024 × 1368, 16:9 = 1824 × 1024, 9:16 = 1024 × 1824. Changing ratio
fits existing pixels into the centered new document without stretching. This
creates a new document and resets previous undo history. The displayed canvas is
limited to **1080 logical pixels wide**, and scales down to fit narrower panels:
`displayWidth = min(1080, panelContentWidth)` and
`displayHeight = displayWidth * canvasHeight / canvasWidth`.
Wider panels center the canvas; narrower panels show the complete document without
horizontal scrolling. The existing 12px panel padding remains outside the viewport.
Painting temporarily disables flicking so a brush stroke cannot become a scroll gesture. There is no fixed
stage height. Portrait ratios extend the scrolling home body;
they do not resize the application window. HomeCanvas passes wheel input to its
scrolling host instead of zooming the fitted document, keeping the prompt and
generation controls below it reachable. The SDK's general editor zoom behavior
is unchanged. Display scale never changes output dimensions. Draft pixels
and attachments belong to the shared QuickGenerate instance and survive route
changes. Mobile and the result composer retain their compact presentation.

The application window's move handle is limited to its top 40 logical pixels. LVRS's
macOS solid-chrome backend disables native whole-window background movement, so
canvas strokes remain canvas input rather than moving the window. The existing
48px native control layout and 8px content separation remain in place.

## Generation inputs

A nonempty canvas is exported as an immutable PNG at output resolution with the
same cream background shown in the UI. It is the first `referenceImages` entry;
attached source images follow it. Empty canvas pixels add no image. If both are
empty, the original text-only `enqueue` path remains in use. The existing native
advanced generation path receives image references, prompt, model, output size
and count. Process-only runtimes reject unsupported image references explicitly.
`enqueueHomeCanvas` preserves the selected ratio label and copies every reference
into a unique Society AssetLibrary `Dreamscapes/GenerationInputs` directory before
enqueueing, so queued jobs are unaffected by draft edits or deleting the original.
Failed submissions remove their input directory; successful jobs retain inputs
for reproducibility alongside their recorded parameters.

QuickGenerate identifies its active submission independently of the recipe type.
Home reference submissions therefore open the shared result screen immediately,
showing all accepted jobs while the model loads. Advanced-workspace submissions
continue to use that workspace's own progress view.

Only readable local images are accepted, with a 256 MiB / 64 megapixel limit.
Up to 19 attachments leave one slot for the canvas within the SDK's 20-reference
limit. Unsupported or removed files surface an input error without changing
canvas pixels. No placeholder scene or sample file is installed in the app.

## Verification

- `DreamscapesGuiTests homeCanvasMaxWidthAndAutoHeight` covers all five ratios at
  panel widths 248, 432, 1103, 1104, 1105, 1121 and 1600px. It verifies the 1080px maximum,
  scaling below that limit, both edges within the frame, centering, automatic
  stage height, output dimensions, and that resizing preserves pasted pixels and undo.
- `homeCanvasScalesToFramePreservingPainting` verifies that the right edge is visible
  without horizontal scrolling, wheel input does not change zoom, painting stays
  responsive, and undo/redo plus resizing narrow/wide/narrow preserve pixels.
- `homePromptDragDoesNotMoveWindow` loads the real Main/Home hierarchy at desktop and
  compact widths, checks the 39px/40px move boundary, selects prompt text without
  changing the draft or moving the window, and verifies the top-strip move handler.
- Home interaction tests scroll controls into view before pointer input when a
  auto-height canvas extends beyond the window's visible body.
- `desktopHomeContinuousRowsAndPromptStarters` verifies that the prompt is first,
  the canvas and toggle are absent, and reference attachments remain visible,
  removable and included in generation inputs without revealing the paint area.
- `Dreamscapes.HomeCanvas` checks actual pixels, drop validation, undo/redo/clear,
  removal semantics, ratio preservation and the exported generation PNG.
- `DreamscapesGuiTests homeCanvasSliderRanges` verifies both slider limits, SDK brush
  updates, mouse input, 0% strokes and narrow layout.
- `DreamscapesGuiTests homeCanvasDragPaintAndColorPicker` uses mouse events to
  drag a thumbnail, undo/redo, select a color, paint and erase raster pixels.
  Set `DREAMSCAPES_HOME_CANVAS_CAPTURE` to a PNG path for a rendered capture.
- `DreamscapesGenerationTests homeCanvasOwnsReferencesAndForwardsNativePixels`
  deletes an original after enqueue and verifies that the native callback receives
  its original RGB pixels from the owned snapshot and that the job completes.
  The native callback is a fixture; it does not run a diffusion model.
- Existing QuickGenerate, home, navigation and generation regressions cover the
  shared draft, compact layout, count and result contracts.
- `referenceGenerateOpensResultImmediately` covers actual Generate clicks with
  attached images for single and batch outputs, immediate feedback, owned input
  pixels and final 3:4 PNG dimensions through a native callback fixture.

Use `cmake -S . -B build`, `cmake --build build`, and `ctest --test-dir build`.
The product remains the single canonical `build/bin/Dreamscapes.app` bundle.

## Home body layout (Figma 261:3148)

Figma `261:3217` supplies the ratio-aware paint stage and automatic height. The
standalone paint component caps its width at 1080px and scales it to narrower frames;
the current desktop Home does not display this stage.
The existing 12px panel padding
stays outside the canvas; the document covers the complete stage. Empty-state
instructions wrap within the visible viewport. The illustrative Figma landscape is editable sample content, not
an installed default drawing or generated result.

The current Home omits the canvas and begins with the prompt. In the standalone
paint component, brush and eraser are followed by the
brush-size icon plus fixed 120px LVRS Mini slider (1–500px), the opacity icon
plus fixed 120px LVRS Mini slider (0–100%), and the LVRS color wheel. Numerical
readouts remain visible; word labels are replaced by the installed LVRS SVGs.
The toolbar wraps groups at narrow widths and keeps undo/redo/clear accessible.
The ratio status reports actual canvas dimensions. The initial brush stays
24px; the design's 51px value is an example rather than a changed default.

Attachments are 340 × 64 cards with a 48px image preview, filename, size and
Remove action. In the shared desktop composer their visual host sits between
prompt and controls. HomePaintCanvas continues owning the existing HomeCanvas
and native drag/drop logic, so moving the attachment host does not recreate or
lose the draft. A standalone HomePaintCanvas retains attachments below its
canvas. The existing reference-image generation path is unchanged. Attachments
are supported image formats; the illustrative PDF in Figma is not passed to an
image-only inference API.
