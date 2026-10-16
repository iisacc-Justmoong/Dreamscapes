# Image artboards in one native project

Multiple images selected in Home, generation history or a result gallery become
independent artboards in **one iiSharedCanvas Document and one `.iisc` file**.
Each board keeps the source pixel dimensions, a transparent background, and its
own bitmap fill layer. Sources retain their native editable layers when importing
`.iisc` canvases. IDs are namespaced per attachment, so matching fill IDs never
alias another image's assets. Existing artboards, static/dynamic content,
keyframes and document audio survive native imports; incompatible frame rates
and infinite-canvas attachments are rejected before replacing the current project.

Newly imported image artboards retain their native left-to-right geometry at y=0
with a 64-document-pixel gap. Existing documents may use arbitrary artboard
coordinates. The Editor presents either document as a horizontal sequence of
pages in its persisted artboard order without changing native positions, layer
transforms, source pixels or saved document content.

The selected page is centered and fitted in the available editing viewport.
One live full-resolution native CanvasItem remains the editing owner. The
iiSharedCanvas 0.29.0 `renderArtboardId` presentation focus selects the active
artboard's background and owned artwork, excluding other boards and loose layers
even when their native regions overlap. A page clip frames that focused native
render while keeping the complete workspace origin and document input coordinates
consistent. Focus does not modify the bound document, hide its source layers,
reset selection or undo, or add a field to `.iisc`; the Editor continues to scope
editing to the selected board. See the
[SDK presentation contract](../../../SDK/iiSharedCanvas/docs/ARTBOARD_PRESENTATION.md).
Nearby pages use bounded renders of their actual
artboard content. They share a horizontal center baseline and overlap in depth:
the active page is in front, nearer pages cover farther pages, and distance from
the selection progressively reduces scale, increases blur and darkens content.
Each farther page's width, height and scale are at most 86% of the preceding
page's values while preserving its own aspect ratio. Its outer edge advances
through 62%, 80% and 98% of the available margin beside the active page. This
keeps pages ordered and exposes successive farther edges even with mixed portrait
and landscape artboards. An opaque matte sits behind each preview, and the
distance-based Darken overlay covers both the image and matte.
The deck creates at most seven slots, and only the three nearest pages on each
side render neighboring content.
Blur uses Qt Quick's GPU effect path; darkening and page navigation remain
available with the software renderer.

Click a neighboring page to select it. A compact 76-pixel strip below the editing
viewport shows the real page previews in a horizontal ListView, using 88×68
controls with 72×48 image areas. The list creates visible delegates and keeps the
selected page in view; clicking a preview selects the page and refits it. The deck
requests renders within 512×512 pixels, and the strip within 144×96 pixels. The
preview API limits either requested dimension to 1024 pixels and caches at most
128 results by native revision, frame, page and output size. Duplicate revision
and document-change notifications are coalesced. Presentation preview updates
wait until a live brush stroke ends or is cancelled, then refresh once; the active
native canvas continues displaying the stroke. A hidden preview strip creates no
delegates or preview renders. Edits and frame changes invalidate cached content.
While the editor is hidden, page presentation retains the strip's reserved
76-pixel height. Showing it again restores the ListView's current-index binding
from `project.currentIndex` and scrolls the selected preview into view after
recreating delegates. Dock and toolbar widths, frame-control height, and document
command/navigation/error areas likewise keep their reserved size according to
the actual layout state. Inherited invisibility does not resize the editing
viewport or trigger an automatic Fit; the native zoom and pan remain intact.
Previous/Next, Alt+Left/Right and Page Up/Down provide the same page navigation.
Navigation shortcuts require editor/canvas focus and stay inactive in dialogs.
Horizontal trackpad/wheel input and Shift+wheel pan only a magnified active page;
a fitted page stays centered. Ordinary vertical wheel input retains zoom.
Wheel and middle-button camera gestures also work in the canvas background
outside the clipped page, using the same SDK zoom and pan coordinates.

The Editor command menu (or mobile command bar) also provides **Add Images**.
Its file dialog accepts multiple images and appends new artboards to the current
project as one atomic, undoable edit, preserving existing artboards and its saved
working-file path. A failed attachment leaves the current document intact.

The layer panel, brush, selections, fills, shapes, text, image placement, generation
references and image exports use the active board's dimensions and local coordinates.
New artwork belongs to that board and cannot appear on another board or in a gap.
Resize, rotate and generated outpaint modify only that board and its owned layers.
Resizing repositions following boards in the imported row to retain the gap. Native
files opened directly preserve their persisted artboard geometry. Save stores all
boards, editable layers, assets and timeline; image export renders the selected board.
Undo/redo is document-wide, matching the single persisted editing owner. Board
selection and viewport are editor state and are not serialized into `.iisc`.
A live brush stroke blocks board navigation and saving.

All inputs are validated before project replacement. Identical canonical paths are
only imported once. Attachment limits remain 1000 images/artboards, a cumulative
64×1024×1024 source pixels and SDK serialization/geometry bounds. Image usage first
persists the same-size PNG-to-IISC fill wrapper described in
[ImageCanvasStorage.md](ImageCanvasStorage.md). The combined project embeds assets
and does not need the original image paths after saving.

New projects save as `.iisc` using the SDK's atomic working-file persistence. Changes
to a saved working file commit synchronously. Existing `.iiscp` files are accepted
for migration: their pages are imported as native artboards without flattening their
layers, the original file is preserved, and Save As requests an `.iisc` destination.
An invalid or truncated legacy file leaves the current editor document intact.

## Validation

`Dreamscapes.EditorProject` checks mixed dimensions, alpha, membership, native
save/reopen without originals, brush independence, new layer ownership, placement,
undo/redo, working-file persistence, scoped resize, deduplication, rejected import
atomicity, and legacy migration. `Dreamscapes.MultiCanvasGui` exercises actual gallery
selection, the desktop/mobile page deck, depth/stacking/effect states, compact
preview sizing and refreshed content, page clicks, navigation, active-page panning,
brushing and the `.iisc` save dialog. It also opens a fixture with scattered native
artboard coordinates, including two overlapping boards, and checks that the view
follows page order, displays the selected board's own pixels and leaves serialized
content unchanged. HomeEditorRoutes and EditorCanvasGui cover the
shared entry routes and existing editor interactions.

The multi-canvas GUI contract also hides and shows the editor twice, checking the
last selected page and its visible preview, native viewport size, zoom and pan,
and unchanged serialized document bytes after each restoration.

On a GPU renderer, the same GUI fixture compares a neighboring page with blur
enabled and disabled. It measures squared RGB edge energy in exposed high-contrast
stripes and checks a separate solid-blue region against the configured Darken
amount. The effect toggle leaves serialized document content unchanged. Software
runs check effect state and layout; they do not count as GPU blur pixel proof.
With `DREAMSCAPES_CAPTURE_DIR` set, the native comparison saves `*-blur-on.png`
and `*-blur-off.png` alongside the page-view captures.

EditorCanvas retains the complete iiPaintEngine pixel-parity and working-file
persistence checks. Its CTest timeout is 240 seconds: the earlier 24-case suite
completed successfully in 126 seconds during the row-view verification, exceeding
the former 120-second budget.

```sh
cmake --build build --parallel 4
ctest --test-dir build -R '^Dreamscapes\.(EditorProject|EditorCanvas|EditorTools|MultiCanvasGui|HomeEditorRoutes|EditorCanvasGui)$' --output-on-failure -j2
```

Earlier row-view verification artifacts remain under `build/artboard-verification/`.
Desktop/mobile layout tests on macOS do not constitute iOS/Android device proof.

The earlier 2026-10-08 row-view verification passed the full product build, four scoped core CTest
groups (ImageCanvas, EditorTools, EditorCanvas and EditorProject), and the latest
successful runs of all three GUI groups. A separate Cocoa/Metal desktop run
compared the visible artboard pixels and exercised scrolling, editing, saving and
attachment. The installed `/Applications/Dreamscapes.app` matched the canonical
bundle's file hashes, modes and symlinks, passed strict recursive signature
verification, and loaded its real entry point with isolated Society storage.
The build tree contains one app and no nested apps. A forced Cocoa/software run
failed its captured-pixel comparison; that diagnostic remains recorded and is not
counted as passing native verification. Evidence, including earlier failures and
the final successful results, is indexed by `verification.json` in that directory.

### Horizontal page presentation verification — 2026-10-09

The page-deck implementation passed the full product build and a subsequent
incremental build that required no recompilation. The final scoped CTest run
passed 10/10 groups in 125.60 seconds: EditorProject, EditorCanvas, EditorTools,
EditorGraphicAdjust, MultiCanvasGui, EditorCanvasGui, HomeEditorRoutes,
EditorLayout, EditorPanelResize and EditorRenderQuality.

A separate Cocoa/Metal run on Apple M1 Max passed both desktop (1280×900) and
mobile-layout (390×844) page fixtures, with four QtTest results including setup
and cleanup, no failures or skips, and no QML binding-loop warnings. Actual
neighbor-preview edge energy decreased from 618.556 to 461.492 on desktop and
from 2735.34 to 1375.2 in the mobile layout when blur was enabled. The solid-blue
sample became `#0000cc`, matching 20% Darken. Those fixtures also exercise
scattered and overlapping native artboards, selected-page pixel isolation,
neighbor and bottom-preview clicks, brush/undo/save/reopen, appended pages,
shortcuts, background camera gestures, and two editor hide/show cycles. Both
saved SQLite working documents passed `PRAGMA quick_check` with `ok`.

The canonical `build/bin/Dreamscapes.app` passed strict recursive signature
verification and a fresh single-app audit: one app and no nested apps. Its real
entry point loaded one window and the QML root in 1.700260 seconds using native
Cocoa, isolated temporary Society storage/settings/helper endpoints and disabled
session restoration. Loader output confirmed the embedded iiSharedCanvas 0.29
library, and the executable hash stayed unchanged during that probe. The separate
Metal fixture above supplies actual GPU-effect proof; requesting Metal during the
startup probe alone is not counted as that proof. A parallel QtTest packaged-start
run exceeded its root-load deadline; its diagnostic is retained separately from
the successful isolated entry-point probe.

iiSharedCanvas 0.29.0 passed 59 source tests, 23 staged-package consumer tests and
23 host-installed consumer tests before adoption by Dreamscapes. The previous
0.28 library remains available. The installed `/Applications` app was not replaced,
and no already-running user app was restarted. Software rendering retains
darkening and page navigation; GPU blur and native Windows/iOS/Android device
execution are separate verification boundaries.

Evidence, source checkpoints, screenshots, retained diagnostics and artifact
hashes are indexed by `build/page-document-20261009/final-verification.json`.
The GUI results apply to the recorded compiled source checkpoint; concurrent
changes to unrelated shared GUI test helpers after that compilation are recorded
separately and are not claimed as tested by this run.
