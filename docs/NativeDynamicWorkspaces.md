# Native editor and workspace view models

## Launch license session

The native app presents its workspace while iiAccountManager checks the
Society license asynchronously. Exactly one automatic check is started for
each app process. The first completed decision is kept as a runtime boolean
until exit; later callbacks cannot revoke a successful session or open a
repeating gate. A failed check displays iiLicenseManager's existing LVUI
Society gate, and opening Society requires an explicit user action. With the
single-check contract, a new check requires an app restart. The check never
edits a canvas or stores a license grant inside an `.iisc` file.
The Account settings category projects the current session's connection,
license result and verified account identifier as read-only fields. Checking,
an unavailable verifier and an explicit denial remain distinct states; no
token or inferred account is displayed. Replacing or disconnecting a session
stops the previous check and removes its projected values.

## Editor actions

The Home sidebar's Tools action opens the same live editor workspace and
document session as the Editor route. It does not mount a static tool catalog
or replace an already open canvas.

The Layers panel's Delete action removes the selected layer and its keyframes
from the in-memory draft, preserves all assets and immutable rendering snapshots,
and selects the closest remaining layer. It does not modify the source file until
the user saves. Invalid selection and reentrant document changes must not corrupt
the active document.

Timeline clips for static and keyframed bitmap/vector layers support transient
move and trim previews. Trimming changes the visible range without deleting
source or motion keys. Moving translates subsequent source keys and all motion
keys with the clip while retaining iiSharedCanvas's required frame-zero source
owner. It clamps the move so even hidden keys remain within the document timeline. A
release commits a single document edit; cancellation or a changed document
leaves the source untouched. Audio and video clip editing have separate timing
semantics and are not provided by this operation.

The Layers panel's new-layer menu creates a selected empty static or dynamic
bitmap or vector layer in the current document. Infinite-canvas bitmap sources
use sparse chunks. The installed iiSharedCanvas requires finite-canvas bitmaps
to have actual pixels, so those start transparent at the canvas extent and are
limited to 16 megapixels for interactive allocation. Vectors retain the
document extent, and dynamic sources include their required frame-zero key.
Creation is an in-memory edit until the existing save action commits it.
Existing assets and renderer snapshots remain unchanged.

The Elements tool projects the selected static vector layer's actual path
geometry. Quadratic and cubic extrema determine the local bounds; stroke
expansion is excluded. Width and height reflect the layer's transform and are
committed with Enter. Resizing changes the selected layer transform around its
existing local origin, preserving the source asset and other layers. The
proportions toggle is session state. Uncommitted input is discarded when the
selection or document changes. A hidden panel does not recalculate bounds.
Layer selection is associated with the document generation so that replacing
an open document cannot temporarily expose a same-named old selection to the
Elements or Layers inspector.
Empty or degenerate paths, singular transforms, bitmap and motion-bearing
layers are unavailable. Shape creation, shape-specific controls and canvas
transform handles remain unconnected; a vector path does not imply a stored
rectangle, ellipse or polygon type.

The Figma Elements Fill field reads the selected static vector asset's actual
solid colors. A common color is displayed as hexadecimal; differing colors
are shown as mixed, and paths without a fill cannot be edited by this field.
Submitting an RGB value preserves each path's existing alpha; an explicit
RGBA value changes alpha too. An invalid input or a changed selection cannot
edit the document. A shared vector asset is copied before changing its fill,
so other layers and mask references keep their original content. An exclusively
referenced asset is edited in place unless an immutable document snapshot is
still borrowed; repeating the same color does not change the revision. The
change marks the document dirty and updates the rendered frame. A color-picker popup
and gradient or image fills are not yet connected.

The Background tool's Mask view manages the selected artwork layer's IISC
mask stack. A new raster identity mask contains one white pixel and white
outside coverage, so adding it initially leaves the rendered layer unchanged
without allocating a full-canvas bitmap. Users can select, duplicate, delete,
enable, invert and combine stack entries and adjust their density and feather;
the stack can also be bypassed. A duplicate owns a separate asset. Removing
an entry preserves its asset because it may still be referenced elsewhere.
Mask edits update the document only when explicitly invoked; row selection is
session state, and retired rows cannot act on a replacement document. The
iiSharedCanvas editor upgrades older in-memory format metadata when an
appearance operation requires it. Existing legacy raster, vector and filter
masks are preserved and reported but are not editable in this panel. Painting
into the identity mask, selection tools, and the Color, Remove and Replace
Background views are not yet connected.

## Society storage provider contract

Local PNG, JPEG, BMP, WebP, TIFF and TGA files selected through Home,
Files or the native Open dialog are decoded with iiVisualCodec on the file
worker into an unsaved iiSharedCanvas bitmap canvas. The codec applies its
orientation and working-color conversion. The still layer spans one second at the document's
default frame rate. Opening an image never changes or associates the source as
an `.iisc` working file: Save requires a new `.iisc` path, and the source is
checked for replacement while decoding. Animated or layered formats are not
advertised as flattened image imports. Existing unsaved canvases remain
protected and stale asynchronous completions cannot replace a newer document.

Image import depends on the JPEG ABI used when the installed iiVisualCodec
archive was built. The codec package must select a matching libjpeg; linking
ABI 80 to a codec built with ABI 62 fails at runtime even if the app links.
The file-model contract exercises JPEG and the other advertised image formats.
Older installed codec packages may not advertise their JPEG ABI and require
the matching JPEG headers and library to be selected at configuration time.

The native browser reads the Society-selected container directly, without a
daemon or inter-application messages. Validate the selected identity and
manifest before and after listing. A redirected root, unready replica, unsafe
section path, changed selection or incomplete search is an error, never an
empty successful listing. Ordinary browsing reads one directory; searches are
bounded, recursive, and return only actual local entries. Work runs on the
runtime worker and only the newest section, directory and search request may
update the owner-thread model. Opening a row delegates to the existing canvas
file model. The request queue accepts replaceable listing adapters for regular
storage, recent files and model metadata. It reads the shared selection file; a
conflicting legacy
environment root override is rejected, and shared-group bootstrap has not been ported. Remote-only
catalog entries and downloads remain separate work.

The Generation History route maintains a review session separate from the
current in-memory generation session. When opened, it lists the selected
Society container's actual local history files on a worker and validates the
container identity before and after the listing. The initial order follows
filesystem modification time; later arrivals append to preserve review
positions. Locally present images and videos are represented
by their real paths; unavailable model, prompt, and generation details are
never inferred. Image previews use the asynchronous native image loader.
Videos without a known preview remain without an image. Selecting, filtering,
or reviewing an item only changes the history session, never its source file.
When one locally available still image is focused or selected, the review
gallery's existing Edit action imports it into an unsaved editor canvas.
Unsupported files, video and multi-selection do not expose this action until
the corresponding editor import is available; no subset is silently edited.
An unsaved editor document is not replaced, and failed imports remain visible
in the review status.
The review context menu can save one locally available original to a new file.
The native picker captures the menu's original target before focus can change;
canceling it changes nothing. The file worker captures a stable source snapshot
and exclusively publishes it at the chosen path without transcoding or
overwriting another file. The destination must retain the original extension.
Unavailable sources, stale revisions, an already existing destination and
worker failures are reported in the review status. Remote-only results and
batch export are not presented as supported by this single-file action.
An unavailable or changing container reports an error instead of an empty
successful history. Refreshes preserve session selection for surviving file
identities, and stale or post-close worker completions are ignored. The current
storage scan is limited to 10,000 entries and reports an error if exceeded;
pagination and remote-only downloads are not connected.
Home's recent-files projection scans the selected Society Files section on a
worker, including nested files, and displays at most twenty of the most
recently modified local entries. Returning Home requests a fresh scan. It does
not follow links or modify originals. The displayed date comes from each
file's filesystem modification time in the local time zone.
The selected-file panel follows the actual selected entry and uses the native
preview provider when supported. Locally available `.iisc` files and supported
still images open through the asynchronous canvas file service; other file types remain
non-openable until a matching editor import service is connected. An unavailable
store is distinguishable from an available, empty store.
If a selection notification causes another Home refresh or selection, the
selected-file projection reconciles to the latest requested file after the
current notification completes.
Home requests only the newest twenty history records on startup and displays
their actual names and available previews. Opening a recent record goes to
history with that record focused; reviewing a selection goes to history with
those records selected. The full history is requested only when its view is
opened. An empty available store and an unavailable store have distinct states.

## Layer inspector contract

The Layers view projects the selected iiSharedCanvas layer, including its real
kind, opacity and blend mode. Selection and an incomplete text edit are view
state: they cannot change the document. A valid opacity commit, slider move or
blend selection edits only the selected layer through DocumentEditor, marks the
document dirty, invalidates the frame renderer, and can be saved through the
existing canvas file model. Invalid percentages, unsupported blend modes and
commands without a selection leave the document untouched. Selecting another
layer discards an uncommitted opacity draft and displays that layer's values.
The opacity control and blend list use LVUI widgets and the panel's Figma
reference; example layers in that reference are never used as document data.
Selection notifications may cause a new selection; the layer list reconciles
to the final selection without modifying the canvas or retaining stale rows.
The Information segment reads the selected layer's actual current-frame asset,
dimensions and representation. Unknown color profile, bit depth or origin is
shown as unavailable; seeking to a frame with another source updates the
display without modifying the document.

## Timeline projection contract

The timeline's Figma `new-layer` action opens the LVUI creation menu. A user
may explicitly add a static or dynamic bitmap or vector layer, or an empty
audio track, to the current document; opening the menu and navigating the view
never add tracks. An audio track is appended after visual layers and existing
audio tracks, receives a unique document identity and a default name, and is
saved in the same `.iisc`. Creation marks the file dirty and refreshes the
timeline without invalidating an unchanged visual frame. A snapshot held by a
worker remains unchanged. Audio tracks cannot yet be played or edited here.

The timeline projects real visual layers, source and motion keyframes within
its visible bounds from the attached iiSharedCanvas document. Layer frame ranges
are inclusive in the document and half-open in the renderer. An absent range
spans the document duration. Scrubbing changes the playhead without changing the
document revision or unsaved state; the preceding rendered frame stays visible
until the next frame arrives. Bitmap and vector layers, including dynamic and
animated layers, can be moved or trimmed under the key-preservation rules above.
Dragging shows a temporary range and translated key markers; cancel discards it,
and release commits through iiSharedCanvas. A document or selection change
during the gesture cancels it. Visible source and motion keys can likewise be
dragged inside their layer's visible range: preview is transient, release moves
only that key, and a conflicting destination is rejected. Source frame zero is
the required ownership anchor and cannot be moved. Real audio tracks and clip
ranges are projected after visual layers, including empty authored tracks;
these rows are read-only and never fabricate audio ticks or sample data. Audio
playback, time-based tick rendering and audio/video clip editing are not yet
connected.

## Canvas size contract

The native Canvas tool displays the open document's real pixel extent. Width
and height are drafts until submitted; accepting them resizes a finite root
canvas around its center without modifying source pixels. Layer motion follows
the shifted base transform, and unlinked masks shift in canvas space while
linked masks remain layer-local. The first operation supports finite documents
without artboards or Dissolve blending; unsupported canvas
structures are rejected without changing the document. The current native
document does not expose persistent print resolution or resampling here, so
those controls must not pretend to save either property. A resize commits one
visual document revision and uses the existing file-save path.

The Mirror segment reflects layer transforms horizontally or vertically within
the finite canvas and applies the same reflection to unlinked masks; linked
masks and motion stay in their local coordinates. It retains the source pixels.
The view-only mirror and text-preservation options remain unavailable until
their respective native view and text semantics are connected.

The Rotate segment applies quarter-turn presets plus a bounded custom angle to
the actual finite canvas, expands its bounds, and transforms layers and unlinked
masks without rewriting pixel assets. The layer-preserving mode is required;
artboards and Dissolve remain unavailable under the same constraints as resize.

## Publishing review settings

The native Thread switch reads and writes the existing common
`publish.destination.<platform>.sequence` IISC setting, using the legacy
`Thread` and `Separate posts` values. Missing settings retain the provider's
automatic policy; opening a target does not write to the document. Metadata
edits update the in-memory publication plan without discarding prepared media;
a visual edit or document replacement invalidates that media. Unknown settings
and other platforms' settings are preserved. A plan is not an account connection
or an upload, and publication remains unavailable until those services exist.
The existing Save draft action delegates to the same canvas file service as
the editor. It is available only for an unsaved document when that service can
save its working `.iisc` or offer Save As. Publication metadata and the canvas
are committed together; choosing Save draft never sends a social post.
Each destination checkbox independently reads and writes the typed boolean
`publish.destination.<platform>.selected` in the same `.iisc` metadata. An
absent setting is unselected; an unrecognized value is reported without
being coerced. Opening a destination review changes only the active panel and
does not silently select or deselect it. A destination selection does not
imply an account connection or enable an upload.
The plan also reads the existing per-target sequence, attachment grouping,
caption, numbering, repeated-introduction and text-splitting settings. Shared
title, description, credits, mentions, tags, link, visibility and content flags
feed the plan, with supported destination overrides taking precedence. Unknown
or incorrectly typed saved review options are reported rather than silently
used as defaults.

## Generation preference provider contract

The About category displays the current CMake project version as read-only
application information. It is generated from the build configuration rather
than copied into a product-specific style or preference value.

The Image, Video, Audio and Agents preferences project the existing native
`generation-models.conf` values. The file is read and saved on a dedicated
worker, using the existing UTF-8 quoted format, including legacy Windows CRLF
files. Edits are serialized and coalesced; a failed write remains an unsaved
draft and reports the error. The provider preserves other categories and
refuses to overwrite a detected external file change. Reloading intentionally
discards the unsaved settings draft; the model exposes that operation, while
the current view instructs the user to restart after a conflict. Image and
Video default-model selectors use the same Society catalogue as generation,
respect each model's eligible media types, and allow clearing the default.
An unavailable saved identifier is displayed without replacing it; the selector
is disabled while the catalogue is unavailable. A successful selection is
saved through the same worker and updates non-overridden session defaults.
A malformed file is never silently replaced. Accepted pending writes
are drained on shutdown independently of canvas and storage work. An edit that
would exceed the format's one-megabyte read limit is rejected before writing.

The Home and advanced generation selectors share one model-catalogue
projection. Image and Video retain separate selected model identities; a
stored default is adopted only when it is present and eligible. Switching a
selection changes the draft only and never begins inference. A missing catalogue
keeps model selection and generation unavailable. If catalogue publication is
rejected during another snapshot notification, both the internal catalogue and
its selections retain the last accepted state.
The catalogue is read from the Society Models section on a worker. It uses
iiLocalDiffusion's bounded model-metadata inspection and honors stored modality
overrides; local Diffusers packages are listed once instead of listing their
internal weight files separately, and the Society VAE category is excluded from
the main model selector. Entries with no recognized modality retain the
existing image fallback. Classification is not a guarantee that an engine can
load a model. Remote-only models and actual inference are not connected to this
catalogue.

Preference field callbacks belong to the currently displayed projection. A
retired field from an earlier category, provider snapshot or model-choice list
must not write a setting after the view has been rebuilt.

Once values have been read or saved, the existing Home generation draft
receives the image and video prompt defaults. Switching media adopts the matching
default only while that input still contains the previous default; a prompt the
user has edited stays unchanged. This does not connect the inference engine.

The editor, file browser, publishing and preferences routes use owner-thread
native C++ presentation models and LVUI controls. File and frame work is
dispatched to a bounded native runtime; service failures are shown explicitly.
The models do not create placeholder files, simulate uploads or present
unknown metadata as fact.

The editor and publishing views share an iiSharedCanvas document session.
Attaching a document validates a detached `.iisc` snapshot. Selection, timeline
seek, active panel and view options are transient presentation state and do not
write or change the document. An explicit layer or publication metadata edit
updates the in-memory document through iiSharedCanvas and marks it unsaved;
it does not silently write the source file. Rejected edits preserve the last
accepted state. Publishing retains unknown shared metadata and does not upload
without an attached publishing and account service.

The editor's File tool has Information, Contents and Storage views backed by
the existing file-panel schema and active document/file session. The complete
schema is visible; fields not supplied by the current native providers remain
explicitly unavailable. See [NativeFileInspector.md](NativeFileInspector.md)
for the current projection and repeated-record limit.

Canvas rendering observes a separate visual revision. Publication title and
description edits still mark the document dirty and increment its document
revision, but do not enqueue a new frame render. Attaching a document and
changing layer appearance, visibility, range or structure do advance the
visual revision; seeking a frame remains transient presentation state.

Render and I/O workers borrow an immutable, shared document snapshot. Frame
seeks and repeated reads do not copy raster data. Single-operation edits detach
when a worker still holds the preceding snapshot; multi-object structural edits
prepare a separate document candidate so they can be committed atomically.
Preparing those candidates can copy large assets on the owner thread. A separate
document generation distinguishes switching files from editing one document.

The editor frame presenter uses iiSharedCanvas' latest-request-wins render
queue. Render work is dispatched off the view thread, and completed pixels
reach the LVUI image on the owner event loop. While another frame of the same
document is rendering, the last completed image remains visible; changing
documents clears it immediately. Stale render completions are discarded.

The editor view derives canvas size, frame range and layer identities from the
attached document, never the example imagery or tracks in Figma. Timeline
playback uses the document's rational frame rate and a monotonic clock; late
ticks skip to the current frame instead of queuing stale frames. Playback ends
on the last frame, pauses on a manual seek, document switch or editor exit, and
does not modify the document. Rendering retains the preceding completed image
while the newest frame is being prepared. Audio playback and the remaining
editing tools are not yet connected. The publishing route derives its platform list and
review limits from iiSocialIntegration; its plan can split a supplied source,
and a document revision invalidates prepared media. The plan does not grant
account access or perform an upload. Its canvas preview uses a separate,
viewport-sized asynchronous iiSharedCanvas frame presenter for the active
document and frame. The presenter is active only while the publishing route is
visible, retains the last completed frame during a render, and is canceled when
the user leaves. This is a canvas preview, not a claim that every artboard or
platform-specific post has been prepared for upload.

The file browser reads locally resident entries from the selected Society
container on demand. Filtering, selection and navigation do not mutate
storage. Locally available `.iisc` and supported still-image rows can be opened in the editor; unsupported
rows do not enable Open. Selecting a row updates its selection state without
recreating the current listing. The Storage preference exposes the verified
selected path read-only. Generation prompt and Image/Video default model values
are backed by the existing preferences file; other settings still need providers. The
account service and publishing transport are also unconnected;
no upload is represented as successful. Remote-only catalog entries and their
downloads are not yet represented by the native browser.
Home's file and generation-history View all actions navigate to their
respective routes. Recent-file and generation-history feeds read locally present
Society files on workers and do not represent remote-only entries.
The review gallery receives the application's asynchronous native file-image
resolver, so a supplied local result can display its preview without reading
or decoding it on the view thread. A missing history provider remains an
unavailable feed; attaching the resolver alone does not populate results.

Canvas file access uses iiSharedCanvas native working-file transactions and
iiFileProvider source identity. Reads preserve both native snapshots and
working files. Saving an existing working file rejects a changed identity or
revision; creating a destination is exclusive and never overwrites another
file. Legacy packed snapshots remain unchanged and require a new destination.
All file access is a worker-thread responsibility.
The file view model serializes open and save requests, then delivers their
results to the owner event loop. An edit made while a save is in flight stays
dirty. A document replaced during a request must never receive that request's
result or file association. The application's native file dialog and editor
Open, Save and Save As actions use this model. The selected Society directory
can use the same open path. Unsaved work cannot be silently replaced.

The New Canvas route uses the existing canvas preset catalogue and validation
rules to create an unsaved in-memory iiSharedCanvas document. Creation must not
overwrite another unsaved document or write a file. Unit conversion and
background selection operate on the draft.
The catalogue is loaded on entry to New Canvas rather than at application
startup; changing a dimension does not rebuild an unchanged preset list.

The visual references are Dreamscapes editor `353:22614`, tool panel
`353:22022`, publishing `1267:4554`, and preferences `353:5587`. The file's
`Storage (Society mirror)` page had no frames when inspected; the existing
product file-browser contract is the available reference for that route.

Model and windowless iiGUI tests cover injected data binding, user commands,
rejected edits, navigation and changes to view state. These tests do not launch
the app, use real account credentials, upload content or prove visual parity or
end-to-end device behavior. The other migrated view stubs remain static.
