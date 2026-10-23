# Original image intake and artboard adoption

Selected Files, History and generated-image records retain their original command
URLs and opaque generation parameters. `ImageOpenRequest` owns original hydration
through the existing SDK downloader. Its ready event starts
`EditorImageImportRequest`; hydration alone no longer runs a synchronous native
image import from Main.

## Operation boundaries

`EditorImageImportRequest` captures the ordered sources, records, target project,
canvas revision/path and native Alias versions. It admits at most one running
preparation and one latest pending intent. Replacing an intent cancels obsolete
work and preserves the newest batch. Workers receive standard C++23 path/document
values and an atomic cancellation token. They do not receive a canvas, container,
GUI callback or mutable file binding.

`prepareImageImport` reads originals through the existing SDK `FileSystem` media
bridge, normalizes the existing implicit-still timing rule, deduplicates canonical
paths and assembles one native document. Assets, layers, frame keys and audio are
moved into the aggregate. Layer masks, vector/filter masks and ordered mask
operands receive the same namespace remapping as their referenced assets.
Unlinked masks on loose world-space layers are localized with those layers.
All other native layer appearance, motion, visibility, conditioning and audio
parameters remain in their typed document values.

All originals, aggregate geometry, artboard count, pixel budget and compatible
rational frame rates are checked before managed wrapper imports begin. The old
path created a temporary EditorCanvas per source, including its media/history
owners, and could save a wrapper before discovering a later invalid source.
The negative control under `build/verification/original-import-20261021-224304/`
records a failed batch increasing Files from zero documents to one.

The worker reuses one SDK FileSystem for wrapper storage. Native IISC originals
are read as snapshots; bitmap wrappers retain the existing SDK naming, hashing,
managed Files placement and synchronization contract. Source canonical path,
size and modification checks detect ordinary replacement during preparation;
bound Alias versions and drive identity are checked on the GUI owner as well.

Each wrapper remains an individually durable SDK operation. This is not a
cross-file storage transaction: a later storage failure or cancellation after an
earlier valid wrapper was published retains that valid managed file. No existing
file is deleted as a rollback. Cancellation is checked between SDK calls, and a
codec/storage call already executing finishes with its detached inputs.

## GUI completion and ownership

An accepted worker result returns to the GUI owner. The target canvas, revision,
working-file path, selected drive and original Alias versions must still match.
A real document edit or replacement cancels the pending adoption; frame seeking,
zoom, pan and artboard navigation retain the intent. Closing the view or changing
commands cancels both hydration and import. Destruction drops delivery without
joining the optional worker. Terminal callbacks precede the idle notification;
reentrant newer intents cannot receive an older idle/error update.
Once the final native adoption has started, it remains one atomic GUI command.
Cancellation from an adoption observer suppresses view reopening/delivery and
finishes the busy state after that command returns; it does not undo an edit
that has already committed. A new request cannot enter halfway through that
adoption. Rejection messages remain presentation state and are not replaced by
an unrelated, unchanged document error.

`EditorProject::adoptImageImport` performs one final native adoption. Opening
replaces the project only after preparation succeeds. Attaching uses the current
canvas's existing validated commit/history/working-file synchronization path and
selects the first new artboard. That durable edit and initial immutable SDK base
snapshot capture still have GUI-owner costs. The import worker does not change
their durability or conflict policy.
The first `ensureEditor` also constructs QML/editor owners and its initial blank
canvas synchronously. The request-start benchmark uses an existing project and
does not include that first-use initialization.

The synchronous `EditorProject::openImages` and `attachImages` remain C++ utilities
using the same preparation/adoption functions; they are absent from the QML
metaobject API. Production image-selection and Add Images paths use
`EditorImageImportRequest`. Ordinary working-file Open and legacy project
migration now use the separate [accepted open command](WorkingFileOpen.md), with
native file ownership retained after asynchronous preparation.

## Source records and presentation

Preparation returns one original-source index for every new artboard. A selected
native document with multiple artboards repeats its own source record; a
deduplicated input does not shift later records. Main receives these mapped
records only after successful adoption. CanvasEditor retains generation metadata
by stable artboard ID, so navigation/attachment/undo does not assign another
source's model or parameters to that artboard.

Preparing, cancellation and failures have their own request state. They do not
revise IISC or enter generation/inference. Existing LVRS controls display the
accepted operation and expose cancellation in the collection, editor or current
result/Home view. Asynchronous preparation failure does not replace the old
project or mutate its document error/history merely to refresh presentation.

## Verification scope

`Dreamscapes.ImageImport` covers rejected/oversized/incompatible batches without
managed writes, source-index mapping, masks/motion/audio preservation, moved
payload ownership, author visibility restoration, ID collisions and early
cancellation. `Dreamscapes.ImageImportRequest` gates actual preparation to check
GUI input delivery, latest-only intent ownership, cancellation, stale document
rejection, destruction, callback reentrancy and the shared production QML type
registration. Existing EditorProject tests retain native file/history round trips.
Mounted Main/CanvasEditor route tests check rejection visibility, complete opaque
metadata after artboard navigation, downloaded originals and asynchronous
attachment followed by a durable-file reopen.

The shared LVRS macOS CMake package was unavailable early in this increment.
Standalone native consumers compile the actual preparation/project/request
sources against the preexisting application SDK runtime. Those component checks
do not substitute for the full LVRS/Main application build, rendered panels,
packaged startup or installed-application verification. After the shared package
became available, the canonical build and offscreen route gates were resumed;
their final status is recorded separately in the increment's verification receipt.
Native foreground checks remain deferred while another UI automation is running.
Canonical output and installation are separate evidence boundaries.
# Binding and callback ordering

An image-import command installs its new project or container before retiring
the previous intent. Direct cancellation observers therefore read the new target
and may choose an even newer binding or start another command. No stale binding
write follows those callbacks. Independent project/container binding generations
suppress superseded notifications and callbacks without changing document or
operation revisions.

Destruction of an idle bound target still publishes `bindingsChanged`. If a
cancellation observer destroys the proposed target, its null binding is notified
once; no later raw pointer assignment revives that target. Both setters tolerate
the command being deleted by cancellation observers. A project canvas change
rebinds its listeners before publishing failure so a retry already watches the
current canvas. These are presentation/command ownership changes, with no file
write or document adoption.

