# Native editor layer timeline

The editor implements the finalized Figma `1065:90081` layout. The design's
Shot terminology is replaced with **Layer**. `EditorTimelineModel` reads the
open native IISC document; the Video generation workspace retains its separate
shot recipe.

`Dreamscapes.Storage` is a static `NO_PLUGIN` module. The production entry point
registers `EditorTimelineModel` explicitly alongside `EditorCanvas`; the packaged
application startup regression verifies this path independently of test hosts.
Tool availability observes canvas/selection notifications, including documents
opened after the view exists and lock changes on the selected layer.

## Data and timing

- Visual rows use actual layer IDs and reverse document stack order, scoped to
  the active artboard. Native audio tracks follow the visual rows.
- Each visual layer has an independent duration. Parallel layers may overlap.
  There is no summing of layer durations into a serial sequence.
- An inclusive native range `[firstFrame, lastFrame]` appears as In
  `firstFrame`, exclusive Out `lastFrame + 1`, and Duration `Out - In`.
  Without an explicit range the layer spans `[0, document.frameCount)`.
- Static bitmap/vector layers keep their source image/geometry throughout that
  range. Dynamic sources use `resolveAssetAt`/native rendering for every frame;
  existing sparse keys hold their previous content until the next key. Video
  layers retain native source-in/out and end behavior.
- New static layers without an authored range span at least one second at the
  current native FPS: 24 frames at 24 fps, 30 at 30 fps, and 24 at 24000/1001 fps
  (rounded up). A longer document gives the new layer its full existing extent.
  Canvas backgrounds, image imports, paint, shapes and the Layer menu share this
  default. One source asset is held across the range; no duplicate images or
  synthetic frame keys are created.
- A legacy one-frame, all-static document with no authored ranges, motion, frame
  keys or audio receives the same default when opened. Normal file opening
  synchronizes the updated native duration; opening a copy preserves its source.
  Explicit short ranges remain editable, including a deliberately trimmed
  one-frame still. Extending a document for a new still freezes any existing
  implicit layer range at its previous extent, preserving animation timing.
  Static layers spanning the complete document remain eligible for Merge Down,
  whether their full range is explicit or implicit; trimmed ranges are protected.
- The ruler, playback and precision controls use native frame count and
  rational FPS. Ranges that extend past the document grow its frame extent;
  invalid ranges never commit a partial change.
- Thumbnails render the actual layer through `renderFrameLayerTiles`, including
  its appearance, at a bounded 64-pixel output. Audio waveforms summarize the
  clip's actual PCM samples and source offset. There are no sample assets.
- Keyframes shows native content and motion keys. Curves shows native opacity
  motion with Hold, Linear and SmoothStep interpolation.

## Editing

The model delegates document changes to the existing canvas commit/history and
working-file synchronization path. Range changes, native layer creation,
rename, stack order, visibility, split, duplicate, removal, frame content and
audio changes persist in IISC and participate in canvas undo/redo. Moving a
dynamic layer shifts its content/motion keys and preserves the mandatory
frame-zero source owner, while other overlapping layers remain in place.

The Layer menu creates static/dynamic bitmap/vector layers, imports a native
image layer, or adds an empty native audio track. A dynamic bitmap's double-click
opens a file picker for content at the captured frame; switching selection while
the picker is open does not change the destination layer ID/frame. A deleted or
incompatible destination rejects the edit.

Selection moves a clip. Link moves the explicit multi-selection together.
Magnetic Snap uses nearby edit boundaries, the playhead and marked range;
precision fields and frame nudges remain exact. Range marks/selects content in
an inclusive dragged frame interval. Blade splits at the playhead, and Trim
changes the selected edge. Slip uses native video/audio source offsets. Roll,
Slide and Ripple trim operate on native audio clips when the selected track has
the required neighbors. These serial-clip tools are disabled for an individual
visual layer row. Ripple deletion rejects intervals occupied by other parallel
content, preventing accidental truncation of artwork.

L/S/M controls use existing layer locks, solo visibility and native visibility
or audio mute. Layer/audio locks and editor selection/tool/zoom are session view
state; native visibility/mute changes are document edits. No caption track is
invented because the current IISC document schema has no caption-track type.

## Layout and verification

The LVRS implementation retains the Figma transport, Layers/Keyframes/Curves,
eight icon tools, filters, snap/link, zoom, fitted ruler, L/S/M headings, compact
cards, exclusive-Out precision row and status footer. Selected buttons use their
pressed state without changing type to Accent. Transport, tool, precision and
footer controls each stay on one horizontal row at every width. Their contents
keep their natural sizes and are clipped at the row's right boundary when space
runs out; hidden controls cannot receive pointer clicks outside that boundary.
Narrowing the dock does not add control rows or increase their heights. Only
zoomed lanes scroll horizontally, while the outer dock scrolls vertically.

The timeline surface is transparent, including transport/tools, track headings,
precision controls and footer. Buttons, inputs, clips, grid lines and the
playhead retain their own theme colors.

The displayed time extent is separate from the native document duration. At
Fit/100%, it contains at least one second of the native frame rate (or three
frames for very low rates). An explicitly trimmed one-frame still therefore has
a readable ruler with frame labels and tick marks beyond the playhead marker;
its native duration remains one frame. Every grid line, clip, key, curve, drag conversion,
selection and playhead uses this same displayed extent. Zoom/pan changes the
displayed range reported in the footer, without adding frames to the document.

The 44-pixel ruler follows the supplied reference and the editable Figma view:
14-pixel major marks and 10-pixel minor marks are two pixels wide, with a shared
bottom edge above a four-pixel yellow baseline. Minor steps divide the adaptive
major interval into five or two integer-frame intervals; they never invent
fractional frames. The playhead uses a 22-by-26-pixel downward-pointing pentagon
with a two-pixel vertical stem, replacing the previous numbered rounded badge.
Its tip stays on the stem at the baseline while zooming or panning. The reference
sRGB colors are tick `#8d8d8d`, baseline `#f0f000`, and playhead `#4096f3`.

`Dreamscapes.EditorLayerTimeline` uses native bitmap animation with different
content on every frame, a static image with a shorter duration, and actual PCM
audio. It checks forward/backward frame previews, IISC snapshot and working-file
round trips, independent overlapping ranges, movement, history, track edits,
locks, solo and responsive layouts. `Dreamscapes.EditorTimeline` checks the
mounted editor, Layer menu, native selection/precision edits, playback, keyboard
controls and clipped single-row layout at six window sizes. Layer-menu
interaction uses a width where its button is visible. Overflow tests also check
that a clipped button ignores pointer input and works after widening. Tests use isolated files
and settings under `build/`.

Ruler regressions cover one-, six- and 120-frame documents at widths of 320,
900 and 1,568 pixels, plus zoom/pan. They verify visible ticks beyond the badge,
matching clip/ruler/playhead coordinates and unchanged IISC bytes. A rendered
background test checks that the parent surface shows through all panel regions.
The reference-style regression also verifies rendered tick/baseline/marker
colors, the marker silhouette and tip alignment, and unchanged IISC bytes.

```sh
cmake --build build --target DreamscapesEditorLayerTimelineTests DreamscapesGuiTests
ctest --test-dir build -R '^Dreamscapes\.(EditorLayerTimeline|EditorTimeline)$' --output-on-failure
```
