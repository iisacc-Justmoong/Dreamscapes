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
cmake --build build --target DreamscapesTimelineViewportTests DreamscapesEditorLayerTimelineTests DreamscapesGuiTests
ctest --test-dir build -R '^Dreamscapes\.(TimelineViewport|EditorLayerTimeline|EditorTimeline)$' --output-on-failure
```

## FilmCraft interaction architecture (2026-10-20)

The local reference is FilmCraft, checked out at
`Reverse/Projects/Adobe/PremierePro`, revision
`105c09bd80e2c86ec7464a2c59a2501eb89ffc5c`. Its
`crates/ui-egui/src/panels/timeline.rs` separates transient `TlState`/`Pressed`
from engine commands, culls rows and clips to the viewport, and preserves the
pointer's time when zooming. Dreamscapes adapts those patterns to its existing
C++23/Qt Quick presentation and native IISC Layer/Audio model. The reference
checkout is unchanged; no Rust runtime, project format, media assets or arbitrary
extra sequence duration is introduced.

`EditorTimelineViewport` owns indexed presentation geometry: integer frame
conversion, adaptive visible ruler ticks, overlapping clip interval lookup,
vertically visible rows, visible keys and curve neighbours. It consumes the
model's published metadata and has no canvas, document, storage or history
reference. The horizontal Flickable remains the scroll owner. Geometry changes
publish a new delegate list only when membership or metadata changes; scrolling
by a pixel within the same range retains existing delegates. A 64-pixel overscan
keeps items ready near viewport boundaries. Thumbnails are requested only for
visible rows/clips. Waveform and curve paint surfaces are bounded by the viewport,
even when a clip spans a greatly magnified timeline.

A single stable pointer area captures the pressed target and trim edge. Clip and
row delegates only draw, so culling the pressed clip never destroys its pointer
grab. Drag updates remain native metadata previews; release commits once through
`EditorTimelineModel`. Escape suppresses release until the button comes up.
Pointer cancellation, canvas/document replacement and hiding/disabling the view
also discard the gesture. Selection, pan, zoom and cancellation do not revise or
save IISC data.

Navigation supports Control/Command + wheel zoom anchored under the pointer,
Shift + wheel or horizontal trackpad movement for horizontal scrolling, and the
middle mouse button for panning. `+`/`-` zoom about the viewport centre; `0` and
Fit restore the fitted view. Dragging near a horizontal edge scrolls that view
with a bounded native velocity while updating the same preview. Magnetic Snap
uses a nine-pixel screen tolerance converted to whole frames at the current zoom.
Unmodified vertical scrolling and dragging empty lane space continue to scroll the dock.

`Dreamscapes.TimelineViewport` checks 10,000 tracks, overlapping clip lookup,
visible tick/key limits, curve continuity neighbours, preview/cancel projection
preservation, cursor-anchored zoom, edge scrolling and non-finite input handling.
`virtualizedNavigationRetainsPointerAndCommitsOnce` renders 1,500 native layers,
checks bounded delegate counts and wheel/pan coordinates, scrolls to a distant
row, then removes the pressed card from the viewport. The stable input owner must
retain the gesture: Escape leaves byte-identical IISC, while release produces
one revision and Undo restores the original document. These focused fixtures do
not measure whole-application frame rate or trained-model throughput.
The same native UI regression checks actual edge autoscroll and Escape without a
revision. A separate curve/key fixture verifies neighbour continuity, pointer
selection of a visible native key, bounded waveform/curve paint surfaces at
128× zoom, and fitting from the far end without changing IISC bytes.

The pressed gesture also captures the affected clip metadata and a sorted index
of other clips' edit boundaries once at press time. Pointer updates search the
two neighbouring indexed boundaries, rather than walking every clip or looking
up each selected ID again. Live playhead and In/Out marks remain separate snap
candidates, so changing navigation or marks does not make the index stale.
Equal-distance candidates retain the existing edge and target priority. Document,
selection, tool and lock changes cancel the gesture before its captured metadata
can be reused. This index is view state and never enters an IISC file.

`denseTimelineDragDoesNotRescanEveryClip` exercises 480 snapped preview updates
with 10,000 real native layers. It checks unchanged IISC content, no revisions or
projection resets, and a generous 250 ms aggregate budget for the native metadata
updates. Construction, snap-index preparation, painting, rendering and release
storage are measured separately or excluded from that budget.
`dragSnapTargetsStayLive` covers changing playhead/range marks, equal-distance
priority and invalidation after a document edit.

## Native playback ownership (2026-10-21)

The same reference's `ui-egui/src/lib.rs::start_audio` / `advance_playback`
starts the device before using its sample clock; `play_ahead.rs` moves mixing
off the UI/device callback. Dreamscapes adapts this ownership boundary with
`EditorAudioPlayback` and `EditorTimelinePlayback.cpp`. QML delegates the play
toggle to the native model; it no longer advances frames with a JavaScript
wall-clock timer. An audible timeline stays in preparation until its output
actually starts. Device `processedUSecs()` drives frame navigation, while a
timeline without audio uses the native monotonic clock. The final frame remains
visible for its full duration, ending at the exclusive document boundary.

Requests retain an immutable native document snapshot. One preparation worker
runs with at most one latest pending request; cancellation is checked during
clip traversal and every 8,192 output frames. Late results, errors and device
events carry request identities and cannot restart cancelled playback. Seek,
external frame changes, document/canvas replacement, hiding/disabling the panel
and destruction cancel the owning request. Starting a silent timeline also
stops an unrelated asset preview on the shared output. Output failures and a
five-second device-start deadline surface through the existing editor notice.

The device is still a Qt GUI/multimedia adapter. Mixing and retained request
values use standard C++23 and native IISC data; this change does not import
FilmCraft's Rust runtime or its streaming ring implementation. The bounded
complete PCM mix is prepared before playback. Asset preview borrows the
snapshot's asset; little-endian PCM is shared with the output without a second
full byte-buffer copy. Audio state has its own notification and does not trigger
camera/tool-mask invalidation.

`Dreamscapes.AudioPlayback` exercises gated preparation, latest-request
coalescing, input event delivery, cancellation/destruction/reentrancy, worker
exceptions, stale device events, silent-timeline replacement and exact 24 / 
30000:1001 final-frame boundaries. It checks zero revisions/projection rebuilds
and byte-identical native documents for playback/navigation.
`Dreamscapes.AudioPlaybackHost` mounts the real QML editor and uses silent PCM
with the host output. Its log distinguishes an actual device start/playhead
advance from an explicit unavailable-device outcome; both verify seek, hide and
document replacement without late playback. Deterministic clock tests use an
injected output, separately from host-device evidence.

## FilmCraft gesture completion (2026-10-21)

The same FilmCraft reference's clip press handler (`timeline.rs`, the `Hit::Clip`
arm) preserves an already selected group before starting `Drag::Move`; its release
handler emits one edit command. Dreamscapes now preserves the selected native
Layer/Audio group when grabbing one of its members. Control/Command/Shift clicks
still toggle selection. Removing a member does not start a drag on that removed
member. Selection itself remains presentation state.

Content and motion keyframes now use the same stable input owner and native
gesture lifecycle as clips. `beginKeyDrag` captures the key and sorted occupied
frame/snap indexes once. Pointer updates perform bounded metadata work and publish
`keyDragPreview`, without editing a document, invalidating thumbnails, rendering
canvas pixels, writing a file, or rebuilding the timeline projection. Motion
curves and key markers share the viewport's temporary key placement; the original
projection is unchanged, even if the original marker leaves the visible range.
Curve neighbours are found around the temporary position. A release commits one
native key move; Escape, pointer cancellation, document replacement, filter/mode
changes and locking discard it. A click at the original frame creates no history.
Occupied keys cannot be overwritten, and the required content owner at frame zero
cannot be dragged away. Moving a key keeps it selected at its new identity/frame.
Selecting a key in Curves mode keeps that mode rather than collapsing the curve.

`nativeKeyDragPreviewsCommitOnce` covers actual IISC working files for both key
kinds, byte-preserving preview/cancel, no-op, invalid/colliding targets, persistence,
selection and Undo. `denseKeyDragUsesPreparedIndex` covers 480 changing previews
with 10,000 native motion keys and a generous aggregate 250 ms native-update
budget, excluding setup, index preparation, painting and durable release. The
mounted QML tests `pointerDragRetainsLinkedSelection` and
`pointerKeyDragCommitsOnceAndEscapeCancels` exercise real pointer/keyboard events,
multiple selection, visible markers, a single revision and Undo. Viewport tests
cover moving a key into/out of view and retaining curve continuity neighbours.
These focused timings do not establish whole-application frame rate.

Key placement is also separate from delegate membership. A temporary frame change
within the same visible key set retains the existing row/key objects. The native
viewport supplies sorted `curveKeys` only when that visible curve is painted;
marker coordinates bind to the temporary placement. A membership-change spy and
QPointer checks in real pointer tests guard against rebuilding delegates on every
move. The static projection is replaced only for an actual document/view change
or when the visible set changes.

## FilmCraft ruler and preview lifetime (2026-10-21)

FilmCraft's `timeline.rs::Drag::Scrub` retains the initial playhead and restores it
on Escape. Its optional media jobs own their source/cache values separately from
the mounted panel. Dreamscapes applies these boundaries in native C++23 without
changing the existing Figma appearance, IISC layer timing or LVRS controls.

`EditorTimelineNavigation.cpp` owns ruler scrubbing. Press navigates immediately;
pointer moves retain one latest frame and publish at a 16 ms timer interval.
Continuous movement does not restart that timer. Release flushes the actual final
pointer position synchronously, and Escape or pointer/view cancellation restores
the starting frame. Direct seek, external frame navigation, document/canvas
replacement and destruction drop queued input so it cannot write a stale frame
into a new owner. Keyboard/precision seeks stay immediate. The interval bounds
request publication on an available event loop; it is not a frame-rate promise.
Scrubbing does not create IISC revisions, save working files, rebuild the timeline
projection or change undo history.

The thumbnail queue has one running operation and at most 128 pending layers.
Replacing a layer's queued frame retains its FIFO position. A worker retains only
its immutable SDK snapshot and render arguments; completion returns to the GUI
owner and is discarded after cancellation or replacement. Destroying a panel
does not join that optional renderer. The snapshot is shared with the SDK's
existing canvas render snapshot, avoiding a second full document copy in the
timeline. The initial SDK snapshot capture can still copy raster data.

Completion publishes `thumbnailReady(layerId)` only to that layer's image. The
broad thumbnail revision remains for document invalidation. Completed requests
leave the in-flight map, allowing an evicted cached frame to be requested again;
returning to a cached frame retires obsolete queued work for that layer. Hidden
or disabled timelines cancel pending delivery while retaining cached images.

`Dreamscapes.TimelineThumbnails` gates a renderer to verify non-blocking owner
destruction, immutable work lifetime, latest-frame FIFO fairness, bounded backlog,
cancellation, exceptions and completion-observer destruction. The native model
and mounted QML tests in `Dreamscapes.EditorLayerTimeline` verify burst coalescing,
ongoing timer delivery, exact release, Escape/hide rollback, stale-frame rejection,
cache eviction recovery, targeted completion and byte-identical IISC/working files.
These run offscreen without changing foreground application focus.

```sh
cmake --build build --target DreamscapesTimelineThumbnailTests DreamscapesEditorLayerTimelineTests
ctest --test-dir build -R '^Dreamscapes\.(TimelineThumbnails|TimelineViewport|EditorLayerTimeline|EditorTimeline|AudioPlayback)$' --output-on-failure
```
