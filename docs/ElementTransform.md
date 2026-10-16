# Element selection and free transform

Elements uses the document's selected layer as the geometry source. Clicking a
visible shape selects the topmost painted shape under the pointer; an ellipse's
empty bounding-box corner does not hide another shape underneath it. Shift-click
adds or removes a selection. The active shape has a blue transform outline, a
thirds grid, eight square resize handles, a center reference and a round rotation
handle. Other selected shapes have outlines; manipulation applies to the active
shape. All handles stay nine screen pixels wide through zoom and pan, including
artboards and mirrored editor presentation.

Drag inside a shape to move it, drag an edge or corner to resize, and drag the
rotation handle to rotate. Shift constrains movement to one axis, corner resize
to the original aspect ratio, or rotation to 15-degree increments. The existing
Constrain proportions setting also constrains corner resize. Alt resizes around
the center. Crossing the opposite edge flips that axis without flattening the
shape to a singular transform. Width and height remain between 1 and 65536 pixels.
The width and height of a rotated shape are lengths along its own transformed
axes, rather than the larger axis-aligned enclosing rectangle.

Clicking empty space clears selection; dragging empty space draws a new shape
and shows its creation bounds. Control/Command-drag can draw over an existing
shape. Arrow keys move the selected shape one canvas pixel, or ten with Shift.
Escape cancels a live transform, then clears selection when no gesture is active.
Enter accepts the current transform preview. Losing capture or focus, switching
tools, changing frame or selection, replacing the document, and Undo/Redo cancel
an unfinished transform before applying the new action. Locked shapes remain
outlined and inspectable with read-only dimensions; hidden or out-of-frame shapes
do not present handles or intercept shape selection.

`EditorCanvas.elementTransform` exposes viewport points, handles, active layer
identity, dimensions, rotation and interaction availability. The QML overlay only
draws that state. Hit testing, pointer manipulation, keyboard input and commits
use the same native coordinates. It samples the actual IISC layer's asset and
motion at the current frame, combines its affine transform and artboard origin,
then maps to viewport zoom, canvas origin and pan. Motion keys and source assets
remain intact: manipulation changes the layer's base affine transform.

`selectedElementSettings["field-0"]` reports those same actual dimensions. Width
and height edits on session-created shapes regenerate local geometry after
accounting for existing scale and rotation. Other shape, fill, stroke, opacity
and blend values come from that shape's accepted settings and layer properties.
Selecting a different known shape changes the Shape view to match its type while
Fill, Stroke and Inspect remain in their chosen views. Shape conversion followed
by Undo/Redo also restores the matching Shape view when the layer ID is unchanged. The Elements presenter
reads selected native values directly and preserves fractional dimensions to two
decimal places; it does not display the 1080-pixel creation draft over a selection.

A vector reopened from IISC without session generation metadata still exposes its
actual path bounds, size, opacity and blend mode. It supports transform and size
edits without guessing the original shape, corner radius or fill-generation
parameters. The Inspect view labels it Vector and disables unavailable generation
controls. Ordinary bitmap images are not Elements candidates; gradient and image
fills created as Elements in this session keep the existing session metadata.
This change does not add a file format or claim to reconstruct absent metadata.

Preview changes redraw in memory without recording authorship, writing the file,
or creating history entries. Release/Enter validates and persists one final edit
using the existing document commit path; one Undo restores the entire gesture.
Cancel and a click without movement preserve encoded document bytes and any Redo
branch. Failed persistence restores the original transform. The interface and
history remain shared by desktop and mobile editor presentations.

## Verification

`Dreamscapes.ElementTransform` exercises every resize handle, rendered pixels,
single-entry Undo/Redo, true-path hit testing, movement, rotation followed by
resizing, scale-aware numeric dimensions, modifiers, flips, cancellation, Enter,
arrow keys, locks, hidden/multiple selection, IISC reopening, artboard and motion
coordinates, and gradient/image asset preservation, disk persistence only at acceptance, and
conflicting-save rejection without losing Redo. `Dreamscapes.ElementsSelection`
retains the existing selection, fill-source and shape-generation regressions.
`editorElementsTransformAndInspectorStayInSync` opens the actual editor QML,
drags a native handle through the overlay, checks live width/height, Undo/Redo,
fractional numeric editing, per-shape panel switching, constant-size zoomed
handles and actual enlarged pixels from the asynchronous canvas renderer. Captures are optional through `DREAMSCAPES_CAPTURE_DIR`.

The combined `Dreamscapes.EditorElements` GUI group allows 600 seconds for its
desktop/mobile cases and the new transform checks, including cold imports while
other workspace builds share the external volume. This is a test harness limit,
not an application interaction latency target.
