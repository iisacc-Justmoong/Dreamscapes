# Canvas tool feedback

Dreamscapes shows Photoshop-inspired feedback while using tools on the canvas.
The product interprets its existing native tool settings; it does not add sample
content, change the IISC schema, or implement Adobe's private renderer.

`EditorCanvasFeedback` is an input-transparent native painted surface above the
document, existing mask tint and transform controls. It has no accepted mouse
buttons, touch input or hover handlers. `EditorCanvas` remains the sole input
owner. The feedback artwork and selection/shape drafts do not modify document pixels, history,
authorship or a working file. Completed paint and mask operations retain their
existing native transactions and Undo behavior.

- Select Rectangle, Triangle and Lasso show a black-and-white dashed contour
  while dragging. The preview shares the selection operation's geometry builder,
  including smoothing, ratio, rotation, from-center and triangle rounding.
- Accepted selections retain marching ants around their actual mask at 50%
  coverage, including disconnected islands and subtraction holes. The contour
  is cached when selection coverage changes; pointer moves do not scan the mask.
- Elements show their actual rectangle, ellipse, polygon or line outline while
  creating, alongside the existing free-transform frame. The creation preview
  reuses the shape path builder used by the native element operation.
- Retouch and non-pixel eraser strokes show their affected brush-width trail.
  Local Brush masks show the same stroke-width contour; Linear/Radial masks show
  their guide geometry. Background Mask adds dashed Rectangle/Ellipse/Lasso and
  Gradient guides and a Polygon rubber-band line to its existing coverage tint.
  Linear guides follow the native gradient angle and span. Semantic and range
  selection modes do not display a rectangle that their native operation ignores.
- Brush, Eraser, Retouch and both mask brushes display a contrasting brush tip
  cursor. Round, Square and Diamond brush tips follow the configured shape.
  Diameter follows zoom, screen-size locking and the selected raster layer's
  sampled affine transform. Small tips and retouch tools add a center crosshair.
  The system pointer is hidden only while a tip is actually displayed. Leaving
  the canvas, hiding Editor or losing focus restores the system pointer.

Escape, capture loss, a tool change, frame change or document replacement clears
uncommitted selection, shape and mask guides. SDK brush cancellation restores
its live stroke. Heal/Clone already apply their pixels while dragging; ending
their capture retains those pixels in one existing Undo step. Unavailable Face
Refine does not display a painting cursor or reuse another retouch mode.

The painted child surface follows the visible feedback bounds, rounded to a
32-pixel grid. A cursor-only frame therefore uses a small texture instead of a
viewport-sized backing image. Nearby pointer moves reuse that allocation.
The 100 ms ants timer runs only while a visible canvas has a selection or guide;
it stops when Editor is hidden or its window is minimized.
It repaints the separate overlay, without rendering document tiles, emitting
document revisions or writing PNG selection images on each tick. Cursor-only
feedback updates on pointer or viewport changes and does not keep a timer alive.
Stroke widths and dash lengths stay fixed in viewport pixels across zoom.

The local Photoshop reference is
`Engineering/Projects/Adobe/Photoshop/docs/BrushInterface.md`, especially the
brush diameter/tip inventory and painting cursor identifiers at lines 195–199,
with its linked `Photoshop.sdef` declarations. These recovered interfaces
establish the reference controls, not Adobe's private painting algorithms.

`Dreamscapes.CanvasFeedback` verifies preview geometry, actual outline pixels,
subtraction holes, cursor shape/size/zoom modes, cancellation with Redo, and
working-file immutability. `Dreamscapes.CanvasFeedbackGui` drives the real editor
toolbar, native pointer routing, Rectangle/Lasso previews, Escape, brush cursor
rendering and hover exit. Captures use `DREAMSCAPES_CAPTURE_DIR`.
