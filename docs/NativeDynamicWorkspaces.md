# Native editor and workspace view models

## Launch license session

License verification is temporarily disabled in the native app by default.
With `DREAMSCAPES_ENABLE_LICENSE_VERIFICATION=OFF`, startup creates no Society
license probe or gate and does not claim that a license or account was verified.
To restore verification, configure `build/` with
`-DDREAMSCAPES_ENABLE_LICENSE_VERIFICATION=ON`, rebuild and reinstall the app.
An already running process keeps its original launch configuration.

When verification is enabled, the native app presents its workspace while iiAccountManager checks the
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

Timeline playback projects the current document's unmuted audio tracks to a
native float PCM output while the visual playhead advances. Audio is mixed in
bounded blocks from the immutable document snapshot, with clip timing, source
offsets, track/clip gain and mono/stereo sample rates respected. The audio
worker starts at the current visual playback time and is canceled on pause,
seek, document switch or editor exit. A missing output device reports a
timeline message without changing the document or preventing visual playback.
The output device's buffering latency is not measured, so this is clock-based
preview synchronization rather than sample-accurate device-clock sync. Audio
edits made during playback take effect on the next play from a fresh snapshot.

The Brush tool connects its LVUI panel and pointer input to iiSharedCanvas
bitmap editing. Color, size, hardness, opacity and flow affect the brush;
pointer pressure and tilt are passed to the paint engine. A gesture
uses a captured document revision and a newly allocated raster asset for the
selected bitmap layer. Static layers retain one source; a dynamic layer writes
an exact-frame source key based on the frame's held image. Other layers, earlier
frames, existing assets and files remain unchanged. The in-progress gesture is
private to the editor; cancel, an invalid input or a changed document, selection
or frame must discard it. Stroke processing and composited preview run away
from the view thread, and only a completed, still-current stroke may commit.
The source file changes only when the user saves the document. Other brush
presets and advanced controls are not yet connected.

The Light adjustment panel uses iiGraphicAdjust's rendered-sRGB light controls:
exposure from -5 to 5 stops and contrast, highlights, shadows, whites and
blacks from -100 to 100. Its target is the selected static bitmap layer. A
preview is prepared from an immutable document snapshot on a worker and never
edits the document; Apply commits only that preview to the same selected layer
and frame if the document revision is still current. The changed raster gets a
new asset identity, preserving the original and any other layer sharing it.
Selection, frame, viewport or document changes invalidate pending work. Auto
analyzes the selected bitmap on the worker and fills an uncommitted slider draft;
it does not apply or save that draft. Transparent-only images and unavailable
workers report an error. The controls support static bitmap layers; other
adjustment categories and unsupported layer types remain unavailable.

The Home sidebar's Tools action opens the same live editor workspace and
document session as the Editor route. It does not mount a static tool catalog
or replace an already open canvas.

The File, Layers, Elements, Background Mask, Color and Replace panel paths
exposed through the native `--view` command open their corresponding panel
inside that same editor session. They do not create a disconnected empty
document. Color and Replace require their worker-backed provider; if it is
unavailable the route reports that condition. Other migrated component paths
can still resolve to their simplified placeholder views.

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
layers are unavailable. A vector path does not imply a stored rectangle,
ellipse or polygon type, so shape-specific parameters for existing artwork
remain unavailable. Shape creation and transform handles are described below.

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

The Background Color view uses iiColorManager for its nine picker modes and
color-space conversions. Picker edits are a session draft. Applying a solid,
linear or radial multi-stop fill prepares a new finite-canvas background on a
worker, and commits it only if the same document revision is still current.
The background is the `canvas.background.layer` layer, never the selected
artwork layer. Existing background layer properties and other shared assets
are preserved. Opening a different document, an intervening edit, invalid
colors, unsupported artboards or an infinite canvas cannot commit a stale or
partial background. The original file changes only through Save.
The picker provides its current mode's channel controls as well as RGB, HSB,
Lab and generic CMYK fields; alpha and opacity are separate multipliers.
Gradient stops, angle, interpolation, catalog swatches and presets are draft
settings. Recent colors are kept only for this app session after an apply.
An intervening document edit cancels worker-side pixel generation where
possible. The panel labels these inputs as a new fill: an existing raster
background has no stored editable gradient parameters, so the view does not
pretend its initial draft reproduces those parameters.

Background Replace accepts a locally selected image or IISC file as a new
background asset in the current IISC draft. An IISC source is rendered at its
first frame; its layers and metadata are never copied into or edited by the
destination. Fill, Fit and Stretch describe the resulting image's placement on
the finite canvas. Decoding, source rendering and document preparation run on
a worker and commit only against the captured destination revision. Cancelled
file selection, a source that changes while being read, an intervening edit,
an over-limit source and an unsupported canvas cannot partially change the
document. Neither source nor destination file is written by Replace; the
destination file changes only on Save. Generative replacement, subject
matching and lighting remain unavailable until their providers are connected.

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
masks are preserved and reported but are not editable in this panel. Lasso
selection can add vector masks as described below. Painting into the identity
mask and the ML Remove Background view remain unconnected.

The layer Histogram view reads the actual displayed composite preview and
computes RGB channel distributions and encoded-RGB luma statistics on a worker. The
readout is labeled as preview-sampled: it is not a claim about full-resolution
pixels. Fully transparent pixels are excluded; other pixels each contribute one
sample regardless of alpha. RGB, red, green and blue are
session-only display choices. Requests coalesce to the newest rendered frame,
and hiding the view or changing documents retires old results without editing
the canvas or blocking playback on the view thread.

The Audio Track panel selects actual IISC audio tracks by stable track ID.
Creating a track, changing its mute or gain, and importing an audio clip are
document edits; changing the selected track is session state. PCM16 WAV input
is read and decoded on a worker from the explicitly selected local file. Its
source sample rate, channels and samples are preserved without implicit
resampling. Import places the entire clip at the current playhead and extends
the document timeline if necessary. Since IISC clip durations use whole
timeline frames, the source is padded with at most one frame of silence to
retain a sub-frame tail. An overlapping clip, changed source file,
invalid audio, a changed document revision or an unavailable file picker is
reported without partially changing the canvas. The imported sound remains in
the unsaved document until Save. Timeline playback resamples its unmuted clips
to the output format. Fades, pan, ducking and sync analysis remain separate
capabilities and are not provided by this panel.

The Trim segment chooses an existing clip on the selected audio track. Its
range is entered in timeline seconds and is rounded to the nearest timeline
frame. Applying a valid subrange adjusts the clip's start, duration, and source
sample offset without modifying the audio asset. Sample offsets are rounded
down to the nearest source sample; the fractional difference is less than one
sample. The operation cannot expand a clip, cross a neighboring clip, or commit
after the source document changes. The selected track and clip and an uncommitted
range remain view state. Waveform display and fades are not yet connected.

The Elements shape choices select a session-only drawing tool. A drag is mapped
from the displayed, aspect-fitted canvas preview to document coordinates and
shows a temporary outline. Only release creates a new static vector layer;
canceling the gesture, changing the document or leaving the tool creates no
layer. Rectangle, ellipse, polygon and line/arrow use the existing Elements
catalog's initial shape, solid fill, stroke color and width, rectangle radius,
polygon sides and star setting, and line end arrow. The vector renderer applies
a centered stroke; the former inside/outside and cap options are not yet
represented. The created vector is selected and can be resized and recolored by
the existing inspector and transformed using the selection handles. Editing
shape-specific parameters on an existing vector remains separate work.

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

On desktop, Dreamscapes uses the Society daemon's same-user broker for Files,
Assets, Generation History and Models. `SOCIETY_LOCAL_BROKER_DIR` can select a
broker. `SOCIETY_CONTAINER_PATH` can constrain the drive that broker announces.
The daemon supplies revision-pinned directory pages and prepares selected files.
Dreamscapes validates the selected manifest and the returned path before
opening a file. Ordinary browsing reads one folder. Recursive searches have
depth and entry bounds. The broker budget is 20 seconds for a listing and five
minutes to prepare a file. An unavailable daemon or a drive change is surfaced
as an error. Only the newest worker request publishes its view model snapshot.
Mobile reads through its on-device storage provider.

The Generation History route maintains a review session separate from the
current in-memory generation session. A worker reads the selected history
directory from the storage provider. The initial order follows modification
time and later arrivals append to preserve review positions. Remote entries
carry a logical reference. Resident files supply their real paths and image
previews use the asynchronous native image loader.
Videos without a known preview remain without an image. Selecting, filtering,
or reviewing an item only changes the history session, never its source file.
When one supported still image is focused or selected, the review gallery's
Edit action prepares its referenced file through Society and imports it into
an unsaved editor canvas.
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
storage request is limited to 10,000 entries and reports an error if exceeded.
The desktop provider reads those entries in revision-checked daemon pages.

The review workspace compares two to four selected still-image results in a
retained overlay, leaving the gallery's selection, focus and scroll state in
place. Pan, zoom, fit and 100% operate on supplied preview pixels only; the
view never changes the originals. Synchronized comparison uses one pixel scale
and shared relative center, while unsynchronized panes can be adjusted
independently. An unavailable preview is reported per pane, and late image
results from a closed or replaced comparison are ignored.
Home's recent-files projection reads the selected Society Files catalogue on a
worker, including nested files, and shows up to twenty recently modified
resident entries. Returning Home refreshes the catalogue. The displayed date
comes from the modification timestamp carried by the storage provider.
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
worker remains unchanged. The timeline plays audio clips with the visual
playhead and allows moving or trimming a clip on its existing track. The Audio
Track panel provides import, trim, mute and gain editing.

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
ranges are projected after visual layers, including empty authored tracks.
Audio dragging displays a transient preview, is bounded by the adjacent clips
and timeline, and queues a document edit only on release. Trimming cannot
extend the source; its start adjusts the source sample offset. The worker
preserves the immutable snapshot and rejects an edit if the document changes
before it commits. The same controls work when the Audio Track panel is hidden.
Time-based tick rendering and direct video clip editing are not yet connected.

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

The native publishing action accepts user OAuth grants supplied for the
current process and sends each checked destination's independently prepared
plan through iiSocialIntegration on a worker. Grants remain session-only and
are never placed in the IISC document, view properties or result messages.
Before sending any destination, validate every selected plan and grant; then
retain each destination's individual outcome, including partial or unknown
results, without automatically retrying. Retain the staged media for the
duration of the worker request even if the document or visible view changes.
The Publish button is enabled only when media and grants for all selected
supported destinations are available. No network request occurs until the
user invokes Publish.

The current native host can receive an already-authorized user grant through
process environment variables. For each platform use the uppercase platform ID
in `DREAMSCAPES_PUBLISH_<ID>_ACCESS_TOKEN` and provide its actual granted scopes
in `DREAMSCAPES_PUBLISH_<ID>_SCOPES` (space or comma separated). Optional
`TARGET_ID`, `INSTANCE_URL`, `API_VERSION`, `ALLOW_BILLABLE` and
`ALLOW_THREAD_REPLIES` values use the same prefix. X billable access and thread
reply entitlement are enabled only by an explicit `1`, `true` or `yes` value.
The host consumes and removes the access-token variables before creating its
workers. The provider validates scopes and destination requirements at
submission. This interface does not acquire, refresh or persist OAuth grants;
interactive account linking remains unconnected. Providers requiring publicly
reachable media cannot use the current local PNG staging by itself.

While the publishing view is visible, the current IISC frame is prepared as
temporary PNG media on a worker for the platform review plan. Publication
metadata orders canvases and supplies their captions and alternative text;
when no explicit canvas list exists, its visible artboards or its root
canvas are used in document order. The staging files are temporary provider
inputs: the IISC document and its shared metadata remain the source of truth.
The current frame is a still image, not a video export. Document replacement,
visual edits, frame changes, a changed canvas list or hiding the view retire
the prepared result. Older worker completions cannot replace newer state.
Changing titles or platform options does not rerender pixels. Preparation
does not authenticate or send any network request, and an unavailable
renderer, invalid canvas reference or exceeded limit is reported explicitly.
Each image is checked against the codec's dimension and pixel-byte limits
before rendering; configurable canvas-count, combined-pixel and encoded-byte
limits bound a preparation batch. Retiring the final lease removes its private
staging directory. Switching or detaching the media model during an observable
update leaves the latest view state authoritative.

The native Thread switch reads and writes the existing common
`publish.destination.<platform>.sequence` IISC setting, using the legacy
`Thread` and `Separate posts` values. Missing settings retain the provider's
automatic policy; opening a target does not write to the document. Metadata
edits update the in-memory publication plan without discarding prepared media;
a visual edit, frame change or document replacement invalidates that media. Unknown settings
and other platforms' settings are preserved. A plan is not an account connection
or an upload; transmission requires a configured grant and an explicit Publish action.
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
The destination review fields use the actual document's shared title,
description, tags and link as inherited values and expose explicit per-target
overrides. Sequence, attachment grouping and the caption, numbering,
introduction and text-splitting options are written in their existing typed
IISC settings. Reset removes only the selected override and restores the
inherited or automatic value. Opening or changing a target is view state;
retired field callbacks cannot edit a different target or replacement document.
The available fields also follow the destination: X and Threads expose their
reply rules, X exposes AI disclosure, Mastodon exposes sensitivity, content
warning and language, Pinterest exposes its video cover URL, and YouTube
exposes video category, audience, synthetic-media and notification settings.
They affect the local plan only; credentials and account configuration are
not stored in the canvas.
These controls prepare a plan only and cannot connect an account or publish.

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
The desktop catalogue receives its Models inventory from the Society daemon.
An app worker uses iiLocalDiffusion's bounded metadata inspection for locally
resident models and honors stored modality
overrides; local Diffusers packages are listed once instead of listing their
internal weight files separately, and the Society VAE category is excluded from
the main model selector. Entries with no recognized modality retain the
existing image fallback. Classification is not a guarantee that an engine can
load a model. Remote-only models are not connected to this catalogue. The
native image generation provider uses the captured local model identity.

Preference field callbacks belong to the currently displayed projection. A
retired field from an earlier category, provider snapshot or model-choice list
must not write a setting after the view has been rebuilt.

Once values have been read or saved, the existing Home generation draft
receives the image and video prompt defaults. Switching media adopts the matching
default only while that input still contains the previous default; a prompt the
user has edited stays unchanged. Applying a default does not start inference.

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

The Select tool uses the active canvas frame and the selected static vector's
actual geometric bounds to present eight transform handles. Pointer movement
previews the outline of a move or resize without editing the document; the
artwork frame updates after release commits one layer transform. Cancel,
selection change, document replacement or
leaving the tool discards the preview. Selection uses the uppermost visible
vector whose transformed bounds contain the pointer. Layers with motion,
artboard clipping, dynamic sources or unsupported geometry remain unchanged. Clicking empty
canvas space clears the selection without changing the file.
Completing a new shape activates Select; choosing a shape in Elements or
returning to the Elements tool resumes drawing with the last selected shape.

The Background / Mask lasso creates a new vector mask on the selected static
artwork layer. The temporary pointer path is presentation state; the closed
path is committed as one mask-stack edit after release. New masks use the
existing Add / union mode, and users may change their combination afterwards.
Cancel, document replacement or leaving the tool discards the path. The first
implementation rejects animated, artboard-clipped or singularly transformed
layers instead of applying a mask in the wrong coordinate space.

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
while the newest frame is being prepared. Canvas pointer tools temporarily
decline input while a replacement frame is pending, so their coordinates never
refer to retained pixels from an older canvas revision. Some remaining editing
tools are not yet connected. The publishing route derives its platform list and
review limits from iiSocialIntegration; its plan can split a supplied source,
and a document revision invalidates prepared media. The plan does not grant
account access; the explicit Publish action invokes the native provider with
configured session grants. Its canvas preview uses a separate,
viewport-sized asynchronous iiSharedCanvas frame presenter for the active
document and frame. The presenter is active only while the publishing route is
visible, retains the last completed frame during a render, and is canceled when
the user leaves. Its preview is separate from the frame media prepared for the
platform review plan and does not itself upload a post.

The desktop browser reads the selected drive through the Society daemon. Open
asks it to prepare a supported `.iisc` or still-image file and hands the
verified local path to the editor. The Storage preference shows the
daemon-selected path. Home's View all actions open Files or Generation History,
which use the same provider. Generation prompt and Image/Video defaults use the
local preferences file. Interactive social account authorization is outside
this storage connection.

The native generation migration must keep a session-owned queue separate from
the views and from the Society files that completed jobs produce. A submission
captures its parameters and model identity before it enters the queue. Jobs run
in order on a worker, report only actual engine progress, and deliver completed
outputs to the owner event loop without erasing earlier outputs if a later job
fails or is cancelled. Cancelling a waiting job prevents it from starting;
cancelling a running job requests cooperative cancellation. Late worker events
cannot change a retired or replaced job. The queue must not invent a backend,
success, progress percentage, or output path when a provider is unavailable.
Cancellation exposes both an atomic flag for in-process SDK operations and a
stop token for iiSDK process communication, so either backend receives the same
request without polling the view thread.
The desktop SDK-worker adapter must pass one bounded JSON request without a
shell and accept completion only from a matching `iild-worker-result-v1`
message after `iild-worker-v1` readiness and a successful process exit.
It must parse chunked stdout and stderr without unbounded buffering, forward
only validated SDK progress events, retain a bounded diagnostic tail, and
terminate the owned worker process on cancellation. A missing or malformed
result is a failure, not a completed generation. The protocol adapter does not
resolve model identities, prepare outputs, or publish files; those remain
separate provider responsibilities. The worker runner invokes preparation,
transport and publication in that order on a worker. Only a successfully
committed output is passed to the review session. If a later publication fails
or the job is canceled, earlier committed outputs remain visible. Invalid UTF-8
and embedded zero bytes in diagnostic output are replaced for the selectable
failure report; log retention is bounded to the latest 16 KiB. The queue also
preserves the beginning and end of unusually long provider failures within its
64 KiB limit, without cutting a UTF-8 sequence or dropping the last log event.
The native application discovers the installed iiLocalDiffusion worker in the
background and connects image and video requests to it. Only parameters
implemented by these adapters are forwarded; other edited settings return an
explicit error. Social-media account connections and social publication still
require providers. The worker and publication contracts are covered with
synthetic fixtures; those tests do not establish that a particular installed
model or hardware can successfully generate an image or video.
The model catalogue captures the daemon-selected drive and local source file
identities on its worker. An image checkpoint or directory package resolves
against the same selection before execution and result publication. A changed,
removed, redirected or newly added package member invalidates the captured
reference; the application must not silently run a replacement model. These
filesystem identities detect ordinary replacement and edits, not deliberate
forgery of filesystem metadata.
An image worker request is prepared on a worker from that captured reference.
It preserves the prompt, negative prompt, dimensions, batch count and seed as
individual arguments, and lets the SDK choose the model route. Quick generation
retains SDK model defaults; an advanced request supplies its explicit sampling
values. Any edited setting for which this adapter has no implementation must be
rejected with its name before the worker starts. A private temporary directory
is owned by the request and must not replace the user's model or existing output.
Before an image enters Society history, its worker record, path containment,
SHA-256, decodability, dimensions and expected batch count are checked. The
captured model and selected history section are revalidated before publication.
Each destination is exclusive. The PNG preserves its existing chunks and gains
the generation recipe in `Dreamscapes.Parameters`; a seed is called resolved
only if the worker actually reported it. Once inference has produced files,
failure or cancellation retains the private output for recovery; a successful
publication releases that staging directory. If the staging directory cannot
be inspected, its contents are conservatively retained and its location is
included in the session report.
If cancellation leaves uncommitted generated files during a live session, the
canceled job exposes a Details action containing the recovery location. It
remains a canceled job; already committed outputs remain visible independently.
The in-memory queue and its recovery report do not survive application exit.
Each committed output also schedules a refresh of the Society history model,
so the Home and history projections see the saved file without changing the
review session's selected results. In the current review session, the captured
model label, prompt, validated dimensions and submitted settings accompany each
result. A seed is displayed only when the worker record supplies a valid
resolved seed; device and elapsed time remain unavailable unless reported.
An image worker's nonnegative 64-bit reported seed remains valid even if a
random batch crosses the 32-bit input limit.
Worker discovery checks `IILD_GENERATOR_EXECUTABLE` first, then the explicit
`DREAMSCAPES_DIFFUSION_EXECUTABLE` build setting. If neither is set, it prefers
the launcher in the build's iiLocalDiffusion package and uses the per-user SDK
when that launcher is unavailable. A missing explicit launcher is reported.
Windows script launchers require an absolute `IILD_PYTHON_EXECUTABLE`. The
selected image worker must accept `--hires-fix` and `--no-hires-fix`. Discovery
runs off the UI thread. The first generation request starts the worker.
Video requests must use the same captured Society model identity and worker
protocol. Each requested video is generated and published independently in
order; cancellation or a later failure must retain earlier committed videos.
With an explicit seed, successive videos use that seed plus their zero-based
batch index, matching the existing generation contract. A batch that would
overflow the unsigned 32-bit seed range is rejected before starting a worker;
random-seed batches let the SDK resolve each video's seed separately.
Home rejects an explicit seed that cannot cover its selected batch before
queueing a job, and clears the explanation when the user corrects the draft.
The Video viewmodel prevents submission while camera motions conflict, an
explicit seed cannot cover the requested batch, or sequential offload conflicts
with CPU text encoding or VAE tiling. It explains the constraint in the existing
generation status area. Correcting the draft restores the
Generate action without altering the accepted settings of other fields.
The adapter must preserve every supported video setting or report an unsupported
setting before inference. It must validate the SDK's completion record, output
dimensions, frame rate, frame count, file size and SHA-256 before an exclusive
Society history commit. The SDK's verified-decode field is evidence supplied by
the worker, not an independent playback test by Dreamscapes.
Sequential video offload explicitly requests the SDK's `--low-vram` mode and
requires CPU text encoding and VAE tiling, which that mode enforces. An older
installed SDK without the flag reports a worker error instead of silently
claiming low-VRAM execution. Video review currently has no native playback or
poster provider, and the history projection does not yet restore the video
worker's detailed sidecar metadata.
The queue-to-review projection supplies the existing LVUI queue, Home queue,
and result-selection models with actual session jobs and committed outputs.
Progress-only updates do not replace the result list. Filtering a job changes
the review projection, not the queue or the files.
The generation review route uses the existing Figma queue, results, and
inspector composition. Its controls must use the same session as the submitted
job; the Society history route remains a separate result source.
When a provider reports failure, the queue can open the existing selectable,
copyable failure view with the submitted settings, observed stage, and original
provider error. A provider error is evidence, not an inferred root cause or an
agent analysis.

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

The editor Text tool uses the catalogue defaults for content, font, size and
color. Enter text, choose the typeface and color, then click the canvas to
create a selected, saveable static vector layer. The canvas coordinates are
mapped through the same fitted preview as the other editor tools. A document
replacement, view change, or intervening edit cancels an unfinished placement.
On macOS, Core Text shapes Unicode and supplies vector glyph outlines; missing
outlines, a text box that does not fit, and more than 128 visible glyphs are
reported instead of silently omitting content. Text is stored as vector paths,
not editable text metadata. Other platforms report that outlining is unavailable
until a platform provider is connected. Text-role presets and the additional
layout and appearance options from the original panel are not yet implemented.

The visual references are Dreamscapes editor `353:22614`, tool panel
`353:22022`, publishing `1267:4554`, and preferences `353:5587`. The file's
`Storage (Society mirror)` page had no frames when inspected; the existing
product file-browser contract is the available reference for that route.

Model and windowless iiGUI tests cover injected data binding, user commands,
rejected edits, navigation and changes to view state. These tests do not launch
the app, use real account credentials, upload content or prove visual parity or
end-to-end device behavior. The other migrated view stubs remain static.

시작 라이선스 검사는 기기 식별과 Society 로컬 채널을 포함해 900ms에 판정
기한을 둔다. 지연 시 허가하지 않고, 이미 전달받은 결과와 종료된 세션의 늦은
응답은 섞지 않는다. 정상 이벤트 루프와 OS 스케줄링이 전제이며, 별도 프로세스의
합성 서명 토큰 검사는 설치된 데몬이나 실제 계정의 검증을 대신하지 않는다.
