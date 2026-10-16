# Elements selection

The Elements panel reads actual layers on the current artboard through
`EditorCanvas.elementCandidates`. Each entry provides `id`, `name`, `shape`,
`selected`, `active` and `editable`. Session-created elements include solid
vector shapes and the bitmap layers used for gradient or image fills. Other
vector layers are listed as `Vector`; their original generation parameters are
not inferred from names or rendered pixels. Ordinary bitmap images, paint
layers, known text elements, other artboards and the editor's background layer
are excluded.

`selectElement(id)` selects one candidate. `setElementSelected(id, checked,
multiple)` updates a multiple selection, `selectAllElements()` selects the
current candidates, and `invertElementSelection()` selects their complement.
`clearElementSelection()` clears selection and does not delete layers. Only
one selected element remains the native active layer for editing; the other
IDs are transient editor presentation state. These operations do not change
document bytes, revision, or undo history.

Ordinary layer selection replaces the multiple selection with the chosen
candidate, or clears it for a non-element layer. Leaving the Elements tool,
changing artboards, removing layers, and replacing a document discard IDs that
no longer apply. Selection changes are rejected during a live brush stroke.
Locked elements remain selectable for inspection, but both pixel and position
locks disable geometry editing.

For an element created in the current editing session,
`selectedElementSettings` returns its own generation settings with the actual
transformed width and height, opacity and blend mode. Selecting it restores the native
Elements draft, avoiding recreation with the previous element's shape settings.
An imported vector has no reconstructed generation parameters, but exposes its
actual transformed path dimensions, opacity and blend mode. Its size and affine
transform can be edited; unsupported generation controls remain disabled. Generation metadata
is currently session-local and is not restored when reopening a saved document.
No SDK or document serialization format is added by this selection API.

Session-local generation bounds and settings are saved alongside each existing
document undo snapshot. Undo and Redo restore Inspector values with the actual
geometry, including vector-to-gradient/image bitmap changes. Creating a shape
completes the metadata of the same committed history entry after its layer ID
exists; rejected edits do not add an entry or discard a Redo branch. All parallel
snapshots follow the existing branch truncation, 32-entry limit and 128 MiB
raster-storage trimming. Selection actions themselves still create no history
entry. Restored active-element settings are synchronized before the existing
`historyRestored` presenter notification.

Image fill sources are retained as implicitly shared session-local `QImage`
values per element. Selecting an element restores its own image source before
later geometry or stroke regeneration. Pattern maps follow the same Undo/Redo
metadata snapshots and are cleared on document replacement. Accepting an image
picker applies the captured Image-fill draft after decoding succeeds; it does
not depend on a global source left by another selected element. Original input
images are not added to the serialized document: existing rendered bitmap fills
remain the stored document representation.
An existing element without its own image source rejects an Image-fill update
until its picker successfully imports one. A failed decode or locked edit leaves
the prior per-element source intact.

The existing shape renderer retains its current behavior: fixed two-stop
diagonal gradients, tiled image fills, approximate corner smoothing and one
active element for geometry updates. Multiple selection provides selection and
inspection navigation; it does not apply edits to all selected layers.
Polygon Star mode uses an effective inner radius of 50% when the stored inner
radius is zero, so its toggle works without an extra Polygon view control. An
explicit positive inner radius continues to use that value.
Editing the active element preserves the other transient selected IDs. Native
creation and geometry transactions suppress intermediate selection notifications
until their new generation settings are available, so a presenter synchronizing
the active draft cannot restore stale parameters over the edit. Width and height
must be finite values from 1 to 65536 pixels; rejected geometry edits restore the
last accepted active-element draft.

Configuring the current Elements tool with its identical accepted draft is
idempotent. Deferred panel synchronization therefore preserves an in-progress
mouse drag through release, producing one shape and its normal undo entry.
Changing the draft or choosing another tool retains the existing gesture
cancellation behavior.

`tests/tst_elementsselection.cpp` independently checks real candidate filtering,
solid/gradient/image element support, multiple selection and inversion, byte and
revision preservation, unchanged undo availability, known draft restoration,
foreign-vector protection, stale IDs, lock protection and live-stroke rejection.
It also simulates the presenter's signal-driven draft synchronization while
changing geometry, converting a fill to bitmap, resizing and creating another
shape, and verifies that rejected dimensions leave both pixels and draft intact.
Working-file Undo/Redo tests compare actual rendered pixels, Inspector values,
layer types and encoded bytes for corners, dimensions and gradient/image fills.
Creation, branch replacement and the 32-entry history cap check that removed IDs
do not retain editable metadata and restored IDs regain their original settings.
Two independent image-fill elements are checked against red and blue reference
pixels through size/stroke edits, Undo/Redo, source replacement and a new branch
after Undo. Document replacement also checks that a previous image source cannot
silently fill a newly created element without a new import.
Mouse creation is also checked through press, repeated identical configuration,
move and release, including rendered pixels and one-step Undo/Redo restoration.
Changed configurations and another tool continue to cancel that gesture.
The test target links `DreamscapesGeneration` and `Qt6::Test`, defines
`DREAMSCAPES_TEST_DIRECTORY` as the shared `build/` directory, and runs with
`QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software`.

Canvas hit testing, the free-transform grid, resize/rotation handles, gesture
commit/cancel behavior and scale-aware dimensions are documented in
[ElementTransform.md](ElementTransform.md).
