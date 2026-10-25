# Runtime size and UI responsiveness

Layer histogram presentation now uses a view-owned C++ worker and a bounded
native summary cache. Hidden panels and live strokes defer its work, channel
switching reuses the same result, and property reads do not render or scan
pixels. Native compositing retains the full resolution and encoded-byte
statistics. See [LayerHistogram](LayerHistogram.md) for the blocking reproduction,
ownership tests and the remaining initial snapshot-capture boundary.

External image/video Save to File uses a worker-owned media copy and publishes
its completion on the GUI owner. Accepted work no longer constructs a Society
FileSystem, opens the default drive, starts its observer or synchronizes its
mirror. UI actions observe `ImageFileExporter.busy`; the selected source is
captured before the system save dialog opens. Closing the view does not wait for
an accepted copy. The SDK's stateless media-export GUI adapter preserves provider
validation, original bytes and Android document-URL support. Gated worker/input,
destroyed-owner and drive-manifest tests verify these boundaries separately from
whole-application frame rate or physical mobile execution.

Photo-library commands likewise retain their operation before publishing busy
state. First-reply gating and queued GUI delivery prevent an obsolete callback
from changing a newer save. Completion precedes idle notification so observers
can close/reuse the owner safely. These are lifetime and ordering improvements;
the native PhotoKit/MediaStore import adapters and their permission policy are
unchanged. See [PhotoLibraryExport](PhotoLibraryExport.md).

Video duration/FPS edits validate their shot/condition plan before publishing
draft values or undo history. Invalid edits no longer checkpoint a partial draft
and invoke Undo to compensate. Planning uses metadata copies without another
JSON encode/decode of the shot list; the existing accepted-edit history snapshot
remains. This does not enqueue generation or write a native canvas file.

Dreamscapes deploys the runtime dependency closure used by its executable, QML
plugins and plain helpers. It does not ship the complete Qt SDK. The macOS
application selects the Basic Controls style in `main.cpp`; its deployment
selects the same style through `product_deploy_app(COMPACT QUICK_STYLE Basic)`.

Compact deployment excludes unrelated child QML modules, unselected Controls
styles, designer metadata, QML tooling type descriptions and debug symbol
bundles. LVRS fonts, icons and image resources are embedded in `libLVRS`; their
duplicate filesystem copies are excluded while license files and runtime QML
remain. Native libraries outside the executable/plugin/helper dependency closure
are removed. Release binaries retain the application's architectures and public
symbols while local/debug symbols are stripped. Signing happens after all these
changes. Missing libraries or incompatible architectures fail deployment.
Qt framework `Headers`, `PrivateHeaders` and `Modules` directories and their
aliases are also excluded before signing. Runtime resources, binaries and
licenses remain, reducing the number of files that runtime validation must scan.

`iiCSMIDI`, `iiLicenseManager` and `iiUpdateManager` are no longer direct product
dependencies because the application does not call them. A dependency required
transitively by an active SDK still belongs to the runtime closure.

## Apple inference profile

Build the product-owned macOS inference runtime before the release application:

```sh
python3 scripts/build-macos-runtime.py --jobs 8
cmake --build build --target Dreamscapes --parallel 8
```

The script builds the iiLocalDiffusion checkout under
`build/apple-runtime/build`, installs it into
`build/runtime-sdk/iiLocalDiffusion-apple`, runs its compute/native backend tests,
checks the dylib links and configures the application to use that package. All
build outputs remain below `build/`. `--sdk-source` selects another SDK checkout;
`--configure-only` prepares the SDK without building or changing the app cache.
The existing app build's iiFileProvider package selection is retained.

This profile disables the optional LibTorch backend used for AMD ROCm or an
alternative CPU implementation. MLX CPU, Metal, Core ML, native diffusion and
ONNX pose support remain enabled. Windows/Linux backend selections are unchanged.
Fetched native sources that require compatibility patches are copied into the
product build first; shared SDK source trees and global installations are not
modified. The private copies explicitly include their standard library headers
and prefer matching jaccl headers over an unrelated installed include path.

The recipe does not download model weights or generate content. Backend tests
and launch checks do not prove trained-model output quality.

## UI lifecycle and invalidation

View state and document/file state have separate notification contracts.
`EditorCanvas::layerStateChanged` updates selection and inspector values;
`editingGeometryChanged` updates the active work area; `fileViewInformationChanged`
updates the small frame/artboard/presentation context. Those actions do not emit
`documentInfoChanged` or `fileInformationChanged`, advance a document revision,
create history, serialize a canvas, or reopen its storage. File panel rows retain
all parameters and read the view-context map only for the relevant fields.
Lock/alpha protection in the editing session uses layer notifications as well.

`EditorProject::navigationChanged` selects an artboard in the existing canvas.
`currentCanvasChanged` means that the owned canvas object actually changed, so
page navigation no longer mounts the canvas or reconfigures its tool. File Alias
availability only notifies observers when its values change. Inspector reveal
availability uses that Alias snapshot rather than probing the disk on selection.

Native temporary element transforms use iiSharedCanvas `refreshPreview()`.
Pointer movement and cancellation redraw the canvas without notifying document,
file, history, timeline projection, or timeline thumbnail consumers. Release
publishes the accepted change through the normal synchronous durable edit path;
stale-file conflicts still reject the edit. Cancelling an idle SDK stroke is a
no-op. A preview never bypasses a writable DocumentFile transaction.

Window activation and closing a generation-result view do not call model/file
refresh. Native storage monitors and inventory polling continue to discover real
changes, and explicit refresh remains available. Application foreground handling
still owns inference pausing/resuming and transfer lifecycle.

Generation result getters consume the published Alias snapshot instead of
re-probing files, hashing complete IISC payloads or preparing previews. IISC
results use the existing asynchronous image provider; opening a document still
validates its actual file. A removed file leaves the list when the storage owner
publishes its next snapshot, or after explicit refresh. `resultsChanged` only
updates result galleries when the published results actually change; job progress
and search/filter presentation do not invalidate an unchanged gallery.

Static artboard thumbnails keep their cached source while seeking. Only pages
with dynamic content, video, motion, or a partial frame range depend on the
playhead; those pages retain frame-sensitive rendering. Content changes still
invalidate previews, with live brush notifications deferred until completion.
`Dreamscapes.EditorProject`, `Dreamscapes.ElementTransform`, and
`Dreamscapes.ViewStateBoundary` cover these boundaries, including persisted bytes,
Alias versions, signal counts, timeline invalidation and explicit refresh.

File-provider dependencies are declared on `DreamscapesGeneration`, the owner of
file operations. Directory-wide linkage no longer adds native file-provider
libraries to unrelated executables such as the application-lifetime test. The
editor consumes `iiSharedCanvas::Quick` and its Core dependency directly instead
of linking the compatibility facade as an additional shared library.
`Dreamscapes.DependencyBoundary` checks generated target properties and, on
macOS, the actual leaf executable's Mach-O dependencies.
The lifetime regression closes the window immediately before the explicit
Active notification, since a delayed initial native Active event can correctly
restore it between callbacks. Its internal two-second watchdog is unchanged;
the runner allows 30 seconds for cold native loader and plugin setup on the
shared volume.

The Home screen loads `CanvasEditor` on the first operation that needs it: a new
canvas, a native/image file or selected generation results. Cancelling the open
dialog does not instantiate the editor. The loaded editor remains alive when
returning Home so its document, undo history and route behavior are preserved.
Video results do not instantiate the image editor.

The selected-layer query describes only the selected layer in the active
artboard. It no longer constructs the complete QVariant layer list to retrieve
one row. The public row fields, lock/alpha state and empty-selection behavior
are preserved.

## Audio preparation and playback responsiveness

`EditorCanvas::playTimelineAudio` previously mixed the complete remaining
timeline on the GUI thread, and `EditorMedia` copied its PCM again into a byte
buffer before starting the device. The editor now submits an immutable
snapshot to `EditorAudioPlayback`; native mixing and any endian conversion run
on a worker. One active worker plus the latest pending request bounds queued
work. Cancellation does not join that worker on the GUI thread. The output
retains a shared PCM buffer (a borrowed snapshot asset for single-asset preview)
and uses a read-only raw-data view on little-endian hosts, removing the second
complete PCM copy. The existing complete-mix allocation limit remains; this is
not streaming or a whole-application memory/FPS claim.

`EditorTimelineModel` owns preparation, output-start and playback clock state.
It waits for the actual device start and advances only the view frame using the
processed audio time; silent timelines use `QElapsedTimer`. Seek and view
lifecycle cancellation invalidate the owning request. No playback notification
enters camera/tool-mask updates, revisions, edit history, document storage or
timeline projection rebuilds. See `Dreamscapes.AudioPlayback` and
`Dreamscapes.AudioPlaybackHost` for gated-worker and real-host coverage.

## Audio and undo memory

Timeline audio accumulates 8,192 stereo sample frames per block, using 128 KiB
of double-precision scratch storage. The previous temporary buffer grew with
the whole timeline, up to 256 MiB at the playback limit. The returned 16-bit PCM
still contains every requested frame. Track order, resampling, clip offsets,
gain, rounding and final clipping are unchanged; the regression compares every
sample with the full-buffer reference, including block boundaries. Stopping
playback closes and releases the QBuffer's retained PCM data.

Undo entries keep their document, tool state, element state and patterns in one
aggregate. A deque removes expired states without shifting every remaining
document. Each entry's media-buffer cost is calculated once; discarded redo
branches and evictions subtract that cached cost. The 128 MiB logical retained
media budget now includes audio, native conditioning buffers and pattern images,
as well as raster/chunk/video pixels. The existing limit of 32 states and minimum
of two states are preserved. Two individually large states can exceed the media
budget; this policy does not bound every metadata allocation or total process
memory. Implicitly shared pattern images are counted conservatively per state.

`undoHistoryIncludesAudioInItsMemoryBudget` verifies eviction, undo/redo content
and replacing a redo branch with an edit. Existing element, mask, graphic and
timeline tests verify that the aggregated history still restores editing state.

### Targeted measurements, 2026-10-17

The same 8,000-frame, 24-fps audio fixture returns 32,000,000 signed 16-bit
samples before and after the change. Separate process runs with Qt's offscreen
platform and software rendering recorded these `/usr/bin/time -l` values:

| Measurement | Before | After |
| --- | ---: | ---: |
| Maximum resident set | 440,696,832 bytes (420.3 MiB) | 185,024,512 bytes (176.5 MiB) |
| Peak physical footprint | 378,209,408 bytes | 122,586,024 bytes |

Maximum resident memory falls by 58.0%. The sample-parity regression separately
checks overlapping mono/stereo clips, a fractional frame rate, resampling, gain,
source offsets, clipping and block boundaries. These measurements cover the
audio fixture, not a user's full document or resident inference weights.

In the 256-layer selection fixture, 1,000 reads changed from 264.237 ms to
1.57808 ms. Both queries return the same public selected-layer row and leave the
document revision unchanged. This is a method benchmark, not application FPS.
Logs are retained under `build/optimization-20261017/`.

The 2026-10-17 source build and canonical deployment completed. EditorTools,
ElementsSelection, ImageCanvas, passive color selection, QuickGenerate, MCP,
application-lifetime and dependency-boundary checks passed. Sample parity,
history restoration, source PNG preservation and public selected-layer fields
remain asserted. The machine-readable record is
`build/optimization-20261017/verification-summary.json`.

The full regression run is not fully green. Native working-file persistence
still intermittently reports SQLite disk-I/O/locking failures; both a passing
timeline run and later failing runs are retained. Gallery scroll/layout checks,
New Canvas bounds and a preparing-runtime fixture also have failures or timeouts.
The earlier broad Gui and EditorCanvasGui groups have not been established as
passing after the targeted test repairs. These findings are not classified as
harmless performance noise. Investigation did not reproduce the I/O failure
with the standalone database probe, so no speculative file-provider production
change or global SDK replacement was made. The preserved installed user session
has not loaded this build candidate.

GUI regressions wait for asynchronous Home controls and settled dialog bounds
before interacting with them. Passive Elements selection checks its documented
fractional display (`24.5 px`) and retains the exact document, rendered pixels,
revision and undo/redo assertions across focus and tool changes.
Cancelling the New Canvas dialog asserts that the deferred editor has not been
created; the test must not dereference an editor that is intentionally absent.
Standalone Home attachment fixtures use an actual temporary Society drive and
retain it until the view and engine are destroyed. This supplies the file-owner
context required for attachment import without changing no-container tests or
the application's selected storage.
Elements history tests likewise own a temporary drive and settings path before
constructing any editor. The history repair tool restores PNG text from the
native document's stored metadata rather than publishing the presentation-only
preview, retaining the existing pixel and source-byte preservation checks.

The tool panel reads one shared `engineState` binding per tool-state change.
Timeline selection changes update only selection; brush/tool settings update
lock flags only when those flags actually change. Neither operation discards
cached thumbnails or reconstructs the track list. Actual document/revision/artboard changes still invalidate thumbnails, including
pixel edits. Frame navigation is presentation state and retains document and
thumbnail snapshots.

## Verification and measurements

Recurring application-wide storage work is kept out of the input path. Society
Helper publishes presence and reads peer records on one coalescing native C++
worker; the event loop only applies completed value snapshots. Automatic activity
and peer-history commits use a separate lazy worker, so history cannot delay the
heartbeat or focus changes. Normal shutdown drains those commits. Atomic publication,
permissions and directory validation are preserved. Stopping a Helper drains its
active operation and invalidates queued results before removing its own record.

The application requires iiSocietyHelper 0.9.5 or later. A tested package installed
under `build/runtime-sdk/iiSocietyHelper` is supported alongside the normal SDK
installation; `iiSocietyHelper_DIR` selects the exact package. Keep its
iiFileProvider, iiSocietyContainer, iiSharedCanvas Core/Quick and iiSocietySync
dependencies from the same verified package graph when configuring the product.
The iiWebServer dependency must also include the file-provider presentation
filter: preview companions and temporary lease files are not public file entries.
A product-owned installation in `build/runtime-sdk/iiWebServer` is searched for
transitive dependencies without adding an unused direct application link.
Generation-history assertions count content through the provider's control-file
classification, while retaining exact PNG names, dimensions and temporary-file
cleanup checks. Storage-selection fixtures scope both the settings path and the
container override, so an isolated test drive cannot override that selection test.
The complete LocalSociety fixture has a 180-second runner budget for native
process/loader setup on shared volumes. Initial asynchronous replica creation has
a separate 15-second setup budget with both peers' errors reported on failure.
Its per-job progress, stall, cancellation and background-expiration deadlines
are unchanged and remain asserted.

The generation controller's two-second inventory poll reads storage/model
metadata on a background task. An unchanged inventory does not reopen the
FileSystem or reconstruct its complete Files/Published/History mirror. Changed
models update existing selections and bindings on the UI thread. Storage epochs
discard results from superseded selections, and active generation/background
states reject obsolete polling results. Explicit storage selection and Refresh
retain their synchronous completion contract. Background polls preserve an
existing generation error. `backgroundModelInventoryDoesNotReopenTheFileMirror`
covers repeated polls, discovery of a newly added model, selection retention and
error retention. Presence worker tests also deliberately stall an operation
while checking that application timers continue to receive events.

`GuiTests::homeDefersEditorUntilNeeded`, Home/editor route tests and multi-canvas
tests cover delayed creation and retained documents. `mainStartupPerformance`
reports the time spent in `QQmlApplicationEngine::load` and the resulting object
count for three loads of an isolated empty Society fixture. It is a QML load
measurement, not the complete operating-system launch or first-frame latency.

`EditorLayerTimelineTests::interactionPerformance` creates 32 patterned static
layers, warms their thumbnails, then repeats 20 brush-setting changes and
thumbnail queries. It reports elapsed time and model notifications.
`toolSettingsDoNotRebuildTimeline` checks unchanged thumbnails across tool and
selection changes and renewed thumbnails after actual pixel changes. Fixtures
are test-only and do not populate production timelines.

Run relevant native/UI tests with the same renderer and no concurrent compiler
load when comparing timings. Native bundle tests cover real Mach-O pruning,
architecture thinning, exported symbols and execution after stripping. A final
bundle must also pass deep signature verification and an actual application
launch without loader path overrides. Source fixture timings, signed package
size and installed application behavior are separate evidence.

MCP process tests use an actual `.societycontainer` package, with Helper and app
endpoint directories below that package's `.society-runtime`. A bare directory
does not satisfy the current Society storage contract. These fixtures isolate
queue, cancellation and local UI question checks from a running user's store.

## Measured macOS result (2026-10-16)

The installed arm64 application decreased from 1,061,195,842 bytes (1,012.0 MiB)
to 291,063,959 bytes (277.6 MiB), a 72.6% reduction in real-file payload. Allocated
filesystem size can differ because of block allocation, directories and links.
The previous application is retained as a ZIP backup in the verification folder.

With an isolated empty Society fixture and software rendering, Home created
6,551 objects instead of 28,349. The mean of the two warm QML engine loads was
617.3 ms instead of 1,124.5 ms. The before fixture freezes the original Main.qml;
both final Home measurements use the same compact installed runtime. These are
source-QML load measurements and do not include operating-system process launch.

For 32 layers and 20 tool-setting/thumbnail-query iterations, the native model
benchmark decreased from 235.058 ms with 20 model notifications to 0.942 ms with
zero notifications. This is a targeted cache/invalidation benchmark, not an
application-wide FPS measurement. Real trained-model generation speed and output
quality were not benchmarked in this change.

Evidence, command logs, exact payload checksums and installation verification are
under `build/performance-20261016/`. The compact candidate passed the native Cocoa
entry-point and Helper discovery check without child loader-path overrides. The
installed app was also opened normally and checked with an existing IISC canvas,
frame navigation, timeline mode changes and Elements/Brush panel switching.

## Interaction-path follow-up (2026-10-16)

A three-second sample of the long-running production process found 85 of its
237 main-thread samples inside Helper publication's `QSaveFile::commit` and
`fsync`. The periodic generation-model refresh also reopened the complete file
mirror on that thread. These shared services run independently of the selected
screen, making them application-wide input bottlenecks under storage contention.
The changes described above move recurring presence, automatic history and model
inventory work off the input thread while retaining explicit durability contracts.

In a three-second, two-participant presence fixture with 100-ms heartbeats, the
95th-percentile timer interval changed from 15.8922 ms to 12.6121 ms and the
maximum from 36.0853 ms to 27.229 ms. Both runs use the same Qt runtime without
duplicate Qt images. This is an isolated timer benchmark, not overall app FPS,
full application launch time or trained-model generation throughput.

The signed candidate also started using Cocoa and its normal Metal selection
without child loader-path overrides. A separate post-startup sample loaded the
verified Helper binary and contained no main-thread disk commit or model-mirror
reopen stack. This candidate used an isolated storage fixture; its memory usage
and sample counts are not directly comparable to the user's open document.

Evidence, package manifests, regression logs and the reversible installation
record are under `build/interaction-performance-20261016/`. The user's existing
process was launched before this candidate and retains its older executable.
Replacing the installed bundle preserves that session; the new code takes effect
on the next application launch.

## Native timeline interaction path (2026-10-20)

The installed application's main-thread sample showed repeated FileSystem mirror
scans in the two-second polling timer. Its timeline also treated every frame seek
as a document revision: `CanvasItem::setFrame()` triggered synchronous persistence,
undo history, document information updates and timeline notifications. A controlled
16-layer, 512×512 working-file fixture reproduced 24 false document revisions and
24 timeline notifications during 24 seeks (1,422,925 µs before this change).

The product now uses iiSharedCanvas 0.31.2, iiPaintEngine 0.5.1 and
iiSocietyHelper 0.9.5. Seeking invalidates only presentation tiles and stale worker
results; the native immutable document render snapshot remains reusable. Seeking
never adds undo entries or writes the working file. Editing still validates and
commits synchronously; releasing a timeline drag performs one durable edit.

`EditorTimelineModel` owns the filtered track layout, per-track clip/key lists,
sorted motion keys, layer counts, audio presence, timecode and snapping. Projection
and layout notifications are separate from selection and control notifications.
Selecting a row or changing Snap/Linked/Tool does not reset QML Repeaters. Content
key projection indexes all document frame keys once instead of rescanning the
complete frame collection for each layer.

Native drag sessions convert pointer coordinates to frame deltas, snap them and
produce geometry previews for moves, trims, linked selection and audio edit tools.
The update path touches metadata only: it does not copy the native document,
validate pixels, create history, persist files or rebuild the track projection.
Escape, pointer cancellation, document/selection replacement and tool changes
cancel the preview. Release validates and commits once; rejected edits retain the
original native document and file.

Timeline thumbnails requested by QML render and encode on one C++ worker. An
immutable document snapshot is retained across seeks; the pending queue keeps the
latest request per layer, with a 128-entry bound and a 128-image result cache.
Callbacks reject old document/artboard generations. The synchronous `thumbnail()`
API remains available for native callers and deterministic pixel tests. QML uses
`requestThumbnail()` and updates only after a completed result is delivered.

FileSystem periodic and monitor-triggered native mirror scans use a bounded C++
worker, while public explicit refresh/save methods keep their synchronous contracts.
Batch scans pin the drive, validate resident paths and use a set for remote entry
deduplication. Old replies cannot overwrite a newly selected or explicitly refreshed
drive. Android retains its existing ContentProvider request context.

Regression coverage includes read-only scrubbing, byte-identical working files,
independent dynamic frame sampling, stable projections during selection, 240 drag
preview updates followed by a single persisted edit, cancel/reject/undo behavior,
background mirror responsiveness and stale selection replies. Timings describe the
specified fixture, not a guaranteed frame rate or an application-wide speedup.

The clean-package comparison recorded 42 µs for the same 24 seeks (16 layers,
512×512), with zero document revisions and zero model notifications. These are
synchronous command-dispatch timings; frame rendering completes asynchronously.
The native drag fixture recorded 572 µs for 240 metadata-only updates before its
single durable commit. Cocoa/Metal verification also exercised actual mouse
press/move/release and responsive widths 1568, 900, 430 and 320.

macOS CTest runs remove inherited DYLD library/framework overrides. A shell-wide
SDK search path can otherwise silently load an earlier compatible dylib despite
CMake selecting a newer package. Loader logs and native library UUIDs verify the
selected Core/Quick/Helper packages; the installation gate compares those UUIDs
with the transformed, signed bundle before replacement.

Eight targeted application test sets passed. The broader HomeEditorRoutes set
still fails its desktop Home open-file dialog visibility assertion; the same
case fails with the original iiSharedCanvas 0.31.1 / iiSocietyHelper 0.9.4
libraries. This result is recorded separately from the timeline performance
regressions. Verification artifacts are under
`build/verification/interaction-cpp-20261020/`.

Live brush revisions defer timeline projection and thumbnail invalidation until
stroke completion/cancellation. Already cached thumbnails remain available while
painting; uncached UI requests wait for the commit instead of copying a live
document into the thumbnail worker. The primary canvas keeps its existing live
preview. `liveBrushDefersTimelineWorkUntilCommit` first reproduced six thumbnail
invalidations during three stroke points, then verifies zero during the stroke
and the final red pixels after commit.

## Detached editor frame export

Editor frame exports now prepare an immutable frame/artboard/density snapshot
when opening the Save dialog and perform rendering, encoding and atomic saving
on a worker. Completion is separate from accepted work, and duplicate requests
cannot replace the accepted frame. Native IISC copies skip an encode/decode
round trip; ordinary images encode directly to the provider's atomic stream.
Stateless SDK destination/publication adapters avoid activating a drive observer
or refreshing its entire mirror. Gated-worker input and owner-lifetime tests,
alongside the actual File panel dialog regression, are recorded under
`build/verification/editor-frame-export-20261021/`. These are operation-boundary
checks, not whole-application frame-rate measurements.

## FilmCraft viewport adaptation (2026-10-20)

The editor timeline now applies the local FilmCraft reference's separation of
transient pointer state, document commands and visible-range rendering. Native
`EditorTimelineViewport` indexes track/clip metadata once per projection change,
culls offscreen rows/clips/keys, and generates only visible adaptive ruler ticks.
It shares no document/storage owner with the view. A stable pointer area remains
alive when virtualized delegates leave the viewport; only accepted release uses
the existing durable edit path. Cursor-anchored zoom, panning and edge scrolling
change presentation state only. See [EditorLayerTimeline](EditorLayerTimeline.md)
for reference provenance, controls and regression contracts.

The 10,000-track fixture reports the time for 240 viewport-only updates separately
from document construction, rendering and durable editing. Native UI coverage uses
1,500 actual IISC layers and forces culling during an active mouse grab. Test,
Cocoa/Metal capture and installation evidence for this change is stored under
`build/verification/filmcraft-timeline-20261020/`; timings describe those fixtures,
not a guaranteed application-wide response time.

The follow-up gesture fixture identified a separate cost after viewport culling:
`updateDrag` still searched the entire clip list and rebuilt all snap candidates
for each pointer update. It now retains the pressed clip/affected rows and builds
one sorted boundary index at press time, then checks its two nearest boundaries
with the current playhead/range marks. Existing equal-distance priorities and
single-release document commits are preserved. With 10,000 native layers and
snapping enabled, the same 480 metadata updates measured 4,043,860 microseconds
before and 1,031 microseconds after the change in the focused fixture. These are
aggregate native preview times: they exclude document construction, index setup,
rendering, painting and durable release storage. The failed baseline, passing
comparison and live-anchor/invalidation regression are retained under
`build/verification/filmcraft-drag-20261021/`.

The application-wide interaction audit also moves generation-result projection
out of presentation getters. Completed/latest getters return the same published
value snapshots used for result notifications. Job or owner-Alias changes project
and publish before observers run; view reads perform no projection or filesystem
work. Repeated reads, copy isolation, unavailable/restored owner snapshots and
unrelated query changes are covered by the native generation regression. See
[InteractionAudit](InteractionAudit.md) for scope and evidence boundaries.
## Selection state reads

`EditorCanvas::toolState()` returns the selected-pixel count retained when the
selection operand changes. Reading panel state no longer scans every alpha value
of a full-resolution mask. Ordinary selection updates refresh the count before
outline/overlay notifications. Background mask selection, completion, deletion,
temporary removal previews and detached repair previews maintain the same value.
Clearing/replacing a canvas resets it. This is transient interaction metadata;
reads neither edit the document nor schedule storage work.

The `selectionStateReadsDoNotPerformDocumentWork` regression checks repeated reads
of a 2,048 by 2,048 selection, no notifications/revision changes, subtract/clear
and document replacement. `selectionStatisticsFollowMaskOwnership` checks empty,
selected and removed background operands. The fixture logs method-only latency;
it is not a claim about complete frame rendering or overall application speed.

## Working-file reads and preview rendering

Reference images in AdvancedGenerate, the Video monitor and the Video shot/key
cards use the existing CanvasPreview image provider. Their source bindings do
not call GenerationController.imagePreview or prepare a physical runtime PNG.
IISC decoding/compositing runs asynchronously at a bounded requested pixel size;
local ordinary images also use Qt's asynchronous, bounded image loading. Only reference selection
and generation input preparation use the existing import/materialization path.
The URL key includes the provider-normalized native change token so same-size
edits with restored modification times invalidate stale pixels, while a preview's
own attribute publication retains the existing key.

The 2026-10-20 storage reproduction confirmed that raw preview reads/descriptor
closure could overlap SQLite writes within the process. Source access is now
coordinated by native identity through iiFileProvider 0.7.3 / Native 0.8.3 and
iiSharedCanvas 0.31.2 Core. Working-file transactions and preview source copies
exclude each other; idle connections, edit callbacks and detached rendering do
not hold that scope. A changed source invalidates an in-flight derived preview.
Queue publication follows scope release, avoiding queue-backpressure inversion.

The prior uninstrumented 50-save storage fixture failed at iteration 6. The new
candidate completed all 50 in 43.53 seconds with preview work enabled and no
explicit quiescing. Separate negative controls verify transaction exclusion and
editing during a paused renderer. These establish a concurrency repair; they do
not measure end-to-end drag latency, overall FPS or every application operation.
Evidence is under `build/verification/interaction-audit-20261020/`.


Timeline key gestures use native preview state and a press-time occupied-frame
index. Pointer movement does not call `moveKey`, native document commit, canvas
rendering, thumbnail invalidation or storage. The viewport merges only the
visible key range and curve neighbours with that temporary position. Releasing
commits once, while cancellation/no-op leaves the document unchanged. Grabbing
one of several selected clips retains the group; modifier removal cannot start
a drag on a deselected clip. See `EditorLayerTimeline.md` for the FilmCraft
reference, behavioral tests and the limits of the native-update timing budget.


Native IISC result commands retain the document URL instead of submitting the
image-provider display URI to file/Photos export. Full-resolution preparation
uses a read-only detached snapshot on a worker and streams PNG into the provider's
atomic local writer, retaining source bytes and avoiding another complete raster
or encoded byte buffer. Photos preparation likewise moves source/header/MIME I/O
off the GUI. Its private PNG lease survives a closed view until native completion;
Android receives MIME in the prepared payload rather than sniffing a file during
GUI dispatch. Ordinary image/MP4 byte-copy behavior remains.

File, frame and photo export terminal delivery precedes idle notification. An
observer closing/reusing an idle owner cannot discard an accepted result, and
request guards prevent the previous idle transition reaching the next export.
Rejected file-export starts have a separate signal and do not fail accepted work.
These ownership and ordering guarantees are distinct from whole-application FPS.


Selected-image preparation now has a native GUI intent owner, independent of the
generation queue. Pure selection uses stable Alias metadata and starts no backend
operation. An explicit Edit resolves original URLs and serializes absent-original
hydration through the SDK worker. Cached previews never become command inputs.
Transfer completion schedules the next step outside the SDK ready signal stack;
one completed preparation opens the editor. Request epochs guard queued steps,
terminal-before-idle delivery, cancellation, reentrant next commands and closed
views. Retry retains the accepted batch, not just its last transfer. These are
operation-chain and data-identity improvements; they do not establish an
end-to-end import latency or application-wide FPS. Synchronous EditorProject
image decoding and page-preview rendering remain under the ongoing audit.

Result gallery wheel input now moves the viewport directly. Explicit position
capture precedes detail/editor transitions, so intermediate layout passes cannot
replace the user's saved scroll position. The gallery regression retains its
1000-result delegate bound and verifies zero generation-job changes across view
navigation. These are view-state operations and do not edit files or documents.

## Asynchronous artboard previews

`EditorProject::pagePreviewSource` now returns cached presentation data or queues
an optional request and returns empty. `pagePreviewReady(index)` only asks that
page's image binding to read its result; document invalidation keeps the existing
`pagePreviewsChanged` contract. The page strip retains native IISC artboards and
all editing controls.

`EditorPagePreviews` runs the native renderer and PNG encoding on one worker.
There are at most 128 pending slots, with the latest frame per page/output size
replacing its pending predecessor without changing FIFO order. Completed URLs
use a 32 MiB costed cache, including failed optional results so a failed image
binding does not repeatedly launch work. Returning to a cached frame retires its
obsolete pending request. Document replacement/invalidation clears pending work;
an already executing old result is discarded. Destruction does not join that
worker or retain a live canvas/file object.

The project reuses the SDK canvas's existing immutable render snapshot per content
revision and captures the author visibility flags separately. Installed native
raster payloads use owning vectors, so taking an additional full Document copy on
the GUI owner would copy every bitmap again. When the shared snapshot has active
page focus, the worker restores author visibility in its private value before
rendering another page. The worker owns those detached inputs and its typed frame,
artboard ID and requested extent. Initial SDK snapshot capture retains its
existing owner-thread cost; this increment avoids another GUI payload copy.
Navigation, preview completion and optional rendering failure do not revise/save
IISC or overwrite editing errors.

The delayed-renderer negative control reproduced the old synchronous call
returning after 2107 ms on the GUI owner. The replacement is checked with a held
renderer and an independent GUI event heartbeat, not a whole-application FPS
claim. Queue/latest-frame, stale completion, failure caching, lifetime and native
pixel cases are recorded under `build/verification/page-previews-20261021`.

An additional gated regression reproduced asynchronous completion bypassing live
stroke deferral (one or two indexed ready notifications while the stroke was
active). The project now defers that presentation notification until stroke
finish/cancel, while explicit native preview reads can still await current data.
Hiding the page strip retires pending optional work and ignores its running
completion, retaining already completed cache entries for the next reveal. These
view operations publish no project/file change.

Both page-strip images and the neighboring page cards consume indexed completion.
The broader GUI regression caught the card binding still waiting on document
invalidation alone; its two layout cases failed with an empty image before that
second consumer was connected. The FileInfo regression now checks navigation's
existing `fileViewInformationChanged` contract and zero broad file notifications,
while retaining document bytes, revision and precision checks. That directly
constructed EditorCanvas code and fixture were unchanged from the preceding
installed source; no production file notification was added for navigation.

## FilmCraft timeline input and optional work (2026-10-21)

Ruler input now has a native C++23 gesture owner. The initial click and final
release navigate immediately; intermediate pointer events retain one latest frame
for a non-restarting 16 ms timer. Escape/view cancellation restores the initial
frame. External navigation and document/owner replacement discard pending frames.
This reduces repeated canvas-frame notifications during an input burst, without
revising/saving IISC data or changing direct keyboard/precision navigation.

The thumbnail worker no longer joins its render thread when the panel is closed.
Qt's future retains the immutable render operation independently, while the
GUI-owned watcher drops obsolete completion. The queue remains single-worker,
latest-per-layer, FIFO and bounded. Completion targets the changed image instead
of republishing every visible thumbnail binding. Cache eviction no longer leaves
a completed frame permanently marked as in flight. The timeline also reuses the
SDK's existing immutable render snapshot instead of making a second full copy.
Initial SDK snapshot capture and native rendering costs remain separate work.

The held-renderer negative control blocked owner destruction for 2,004,942 us.
The first corrected run released that owner in 19 us while the renderer remained
held, and suppressed its late delivery. This is an owner-lifetime fixture, not
an end-to-end UI frame-rate measurement. Evidence is retained in
`build/verification/filmcraft-timeline-input-20261021-222734/thumbnail-red.xml` and
`thumbnail-green-initial.xml`. See [EditorLayerTimeline](EditorLayerTimeline.md)
for the reference mapping and regression contracts.

## Original import preparation (2026-10-21)

Selected-image and Add Images QML commands now enter a native accepted request.
The request retains only one latest pending batch while a worker reads originals,
assembles native artboards and performs the existing managed wrapper imports.
Temporary per-source EditorCanvas/media/history objects are removed. Asset/audio
payloads move into the aggregate; attachment drafts restore authored visibility
from the shared immutable render snapshot on the worker.

Invalid input, incompatible frame rates and aggregate limits are resolved before
wrapper storage begins. The actual native negative control rejected its second
source yet created one managed IISC file; the regression retains that failure.
Completion updates the project once and maps each actual artboard to its original
source record. Pure navigation does not invalidate an accepted batch, while an
actual document/drive change cancels stale adoption. Closing a view never joins
the worker.

Initial SDK snapshot capture, final native adoption/validation/history and a
durable attachment commit remain separate GUI-owner costs. Existing SDK wrapper
import still decodes its source for its own storage contract; no single-decode or
cross-file transaction claim is made. See [EditorImageImport](EditorImageImport.md)
for component tests, mounted route verification and the remaining first-editor
initialization/snapshot/commit boundaries. The start/destruction timing fixture
holds real preparation behind a gate; it does not measure total app latency.

## Deferred storage for view/data owners (2026-10-21)

The EditorCanvas FileSystem used to discover and scan the default drive in its constructor, including for unsaved/view-only owners. It now uses the SDK deferred initialization path and is an explicit owned QObject child. Detached image readers and original-image preparation avoid the same implicit discovery. Explicit open/save, Alias conflict checks and synchronous persistence retain their contracts. See [StorageInitialization](StorageInitialization.md) for SDK packaging evidence, the activation-boundary regression and constructor measurement scope.

## Working-file preparation and completion (2026-10-21)

Native `.iisc` open and legacy project migration now use a detached read command.
Decoding, mirror scanning and Alias hashing run on a worker; one running/one
latest pending intent prevents a queue of obsolete reads. GUI adoption uses the
SDK prepared-storage values and retains writable Alias/version semantics.
Home/editor navigation and generation metadata update only after completion.
View cancellation is scoped to its own shared download. See
[WorkingFileOpen](WorkingFileOpen.md) for source identity, lifecycle tests,
dispatch measurements and the remaining native adoption/render/commit costs.


## 한 점 색상 표본과 화이트 밸런스 입력

색상 표본의 GUI 호출에서 전체 프레임 렌더링을 제거했다. 불변 프레임·세계 좌표·작성 가시성으로 한 픽셀을 작업 스레드에서 읽고, 최신 수락 요청만 GUI에 전달한다. 원본 리비전 변경은 결과를 폐기하며 순수 탐색은 파일을 쓰지 않는다. 화이트 밸런스의 온도·틴트는 한 번에 처리·저장한다. 읽기 오류와 대기 상태는 파일 상태에서 분리했다. 자세한 계약과 남은 동기 처리 비용은 [EditorColorSampling.md](EditorColorSampling.md)에 명시했다.

패키지 계약은 `iiSharedCanvas 0.31.4`와 `iiSocietyHelper 0.10.2`에 맞춘다. Helper는 컴파일한 Native 최소 버전을 CoreIO보다 먼저 선택하고, 캐시 경로뿐 아니라 실제 가져온 라이브러리 대상의 경로도 확인한다. 전역 SDK를 교체하지 않고 독립 설치 소비자로 검증한다.

파일 저장·읽기·이미지 래핑을 포함하는 도형 변형 검사는 자체 임시 `.societycontainer` 드라이브를 생성한다. 외부 기본 드라이브나 사용자의 선택 상태가 이미 존재한다고 가정하지 않는다. 이 준비는 검사 환경의 격리이며 앱의 파일 접근 정책을 변경하지 않는다.


## 그래픽 패널의 설정 계산

색상 표본·프리셋과 자동 보정 등 8종의 설정 계산을 `EditorGraphicHelper`로 분리한다. 표본·프리셋은 픽셀을 캡처하지 않고, 픽셀이 필요한 분석은 C++ 작업 스레드에서 원본 레이어를 읽는다. 설정 계산 뒤 전체 효과 적용·마스크 혼합·결과 및 원본 PNG 인코딩을 하지 않는다. 버튼 가능 여부 조회에서도 청크 픽셀의 전체 펼치기를 제거한다. [입력·취소·결과 적용과 검증 계약](GraphicParameterHelpers.md)을 참조한다.

마스크 패널의 독립 GUI 검사는 앱과 같은 `registerStorageViewTypes()`로 타입을 등록한다. 공유 패널에 추가된 네이티브 계산 객체를 누락한 검사 전용 타입 목록을 유지하지 않으며, 기존 배경·마스크 패널의 폭별 로딩과 실제 컨트롤 동작을 계속 검사한다.

## 카메라·사진 획득 객체의 초기화 경계

에디터 생성자가 보유하는 `EditorCameraPhoto`의 장치 감시와 임시 폴더 생성은 실제 사용 시점으로 지연한다. 카메라 정보 조회는 장치 감시만 시작하고, 갤러리 목록·검색·선택은 장치 감시나 출력 저장소를 시작하지 않는다. 썸네일·원본 내보내기·캡처 출력은 `iiFileProvider` 네이티브 임시 디렉터리를 공유한다. 변환 없는 로컬 원본 가져오기는 출력 파일을 만들지 않는다. 계약과 검사 범위는 [EditorCameraPhoto.md](EditorCameraPhoto.md)에 기록한다. 이 변경은 초기화 작업의 발생 조건을 줄이는 것이며, 앱 전체 지연 또는 설치본의 응답 시간 개선율을 입증하지 않는다.

## 동적 래스터의 원본과 프레임 이동

앱이 SDK의 동적 래스터 편집을 실제 선택 경로에 연결한다. 원본 캐시와 지우개 복원 데이터는 현재 애셋을 구분하며, 메타데이터 확인만으로 픽셀 펼치기나 파일 작업을 시작하지 않는다. SDK는 애셋을 바꾸기 전에 미완료 페인팅을 취소한다. [동작과 검증 경계](DynamicRasterEditing.md)를 참조한다.

## 그래픽 이미지 명령의 작업 스레드

그래픽 패널의 이미지 미리보기·Apply·마스크는 `EditorGraphicCommand`를 사용한다. `Qt.callLater` 안에서 동기 이미지 API를 실행하는 경로를 제거했다. 원본·청크·마스크 처리와 PNG 인코딩은 작업자가 맡으며, 같은 보정 원본은 공유 소유로 전달한다. 설정 계산과 이미지 처리의 조건은 별도로 조회한다. 상세 계약과 남은 UI 비용은 [GraphicImageCommands.md](GraphicImageCommands.md)를 따른다. 이 변경은 실제 문서 쓰기와 지속 저장을 비동기 자동 저장으로 바꾸지 않는다.

내부 마스크의 픽셀별 `QVariant` 생성과 재파싱은 네이티브 `MaskView` 전달로 교체한다. 소유된 8비트 선택 영역은 한 번만 SDK 커버리지 배열로 변환하고, 기존 공개 맵 입력과 같은 반올림을 유지한다. 새 마스크 미리보기는 사용하지 않는 기존 선택 영역을 복사하거나 레이어 좌표로 변환하지 않는다. 여러 마스크의 보정 배열은 SDK에 그대로 연결하고 별도의 선택 영역이 있으면 결과를 그 영역에 제한한다. 정확성·무변경·저장 계약은 `Dreamscapes.TypedMaskInput`으로 확인한다. 합성 이미지 처리 시간과 프로세스 최대 메모리는 검증 회차의 독립 프로세스 비교 근거에 기록하며, 설치 앱의 FPS나 전경 입력 지연으로 간주하지 않는다.

2026-10-21 비교는 같은 설치 SDK와 2048×2048 합성 `maskWhiteOnBlack` 입력을 사용하고, 각 방식마다 새 프로세스 3회의 중앙값을 기록했다. 기존 바이너리를 보존하여 교대로 실행했으며 모든 출력 픽셀의 SHA-256이 같았다. 어댑터 경계의 시간은 194.65ms에서 87.99ms, 최대 상주 메모리는 350.91MiB에서 222.69MiB로 감소했다. 원본 캡처·선택 좌표 변환·PNG를 포함한 실제 작업 계산 경로는 451.78ms에서 334.41ms, 390.13MiB에서 278.05MiB로 감소했다. 소요 시간과 최대 메모리는 각각 약 26.0%, 28.7% 감소한 값이다. 개별 실행의 변동은 존재하며 전체 앱 FPS·입력 지연 또는 설치 앱 성능의 증거가 아니다. 원자료는 `build/verification/typed-mask-20261021-121348/mask-comparison.json`이다.

## 사진 원본 준비와 비대기 종료

사진 원본 내보내기 완료가 소유 스레드의 동기 가져오기로 이어지던 연결을 제거한다. 대상 문서와 배치 아트보드·저장소를 수락 시점에 캡처하고, 디코딩·래퍼 준비는 별도 작업자가 맡는다. 소유 스레드는 원래 대상에 한 번의 실제 문서 편집·동기 저장만 수행한다. 갤러리 보기·선택·줌·도구 변경은 준비 작업을 다시 실행하거나 파일 상태를 발행하지 않는다.

획득 객체의 전용 QThreadPool 대신 객체별 값형 FIFO 연속 작업을 앱 작업 풀에서 실행한다. 화면 객체를 닫을 때 작업 종료를 기다리지 않으며, 닫힌 객체의 대기 작업은 SDK 호출 전에 버린다. 원본 임시 파일은 획득과 배치 작업이 공유 소유한다. 변경 전에는 600ms 후 해제하는 원본 내보내기 조건에서 객체 종료가 250ms 제한을 초과하는 것을 재현했다. 종료 지연·원본 수명과 입력 이벤트 처리는 `Dreamscapes.PhotoImport`의 실제 네이티브 작업으로 검사한다. 이 결과를 물리 장치 응답이나 전체 앱 종료 시간으로 확대 해석하지 않는다. 자세한 계약은 [PhotoImportCommands.md](PhotoImportCommands.md)를 따른다.

## 기하 마스크의 불필요한 원본 준비 제거

기하 마스크 적용의 수락 단계는 문서 스냅샷과 원본 이미지 대신 레이어 크기·샘플한 변환을 네이티브 값으로 캡처한다. 작업자도 이 요청에 원본 래스터 읽기나 QImage/RGBA 부동소수 변환을 수행하지 않는다. 미리보기에는 원본 비교 이미지가 필요하므로 해당 읽기를 보존하고, 마스크 커널에 전달할 불필요한 이미지 변환만 생략한다. 실제 색·휘도·브러시·정교화 마스크는 픽셀 경로를 유지한다.

개선 전후 성능은 별도 새 프로세스에서 같은 2048 × 2048 선형 마스크의 계산 시간·최대 RSS·결과 해시를 비교한다. 어댑터 호출과 전체 마스크 계산을 구분하며, 수락 단계의 문서 스냅샷 제거는 실제 명령 입력 검사로 확인한다. 이 측정은 애플리케이션 전경의 FPS나 모든 작업의 지연을 대표하지 않는다.

2048 × 2048 선형 마스크를 조건별로 새 프로세스 3회에서 비교한 중앙값은 다음과 같다. QImage 어댑터는 188.54ms에서 178.11ms, 최대 RSS는 138.38MiB에서 74.33MiB로 줄었다. 전체 마스크 계산은 208.75ms에서 198.02ms, 최대 RSS는 194.66MiB에서 130.75MiB로 줄었다. 두 경로 모두 개선 전후 결과 해시가 동일했다. 측정에는 결과 해시 계산을 포함하며, 전체 계산 실험은 기존 불변 문서 입력을 사용하므로 소유 스레드의 스냅샷 제거 효과를 별도로 계량한 수치가 아니다. 원시 측정과 빌드 입력은 `build/verification/mask-demand-20261021-234127/`에 기록했다.

## 원본 좌표와 결과 검증의 실행 위치

청크 원본은 할당된 캔버스 창을 0부터 시작하는 이미지로 펼치므로, 원본의 월드 원점을 변환에 포함해야 한다. `editorRaster::describe`를 값형 캡처와 이미지 계산에 재사용하여 이 매핑을 일치시킨다. 마스크 적용의 문서·RGB 입력 생략은 유지한다.

마스크 결과의 PNG 디코딩, 알파 개수와 윤곽·오버레이 일치 검사는 작업자에서 수행한다. 소유자는 크기와 형식 등 픽셀 순회가 필요 없는 필드만 확인한다. 검증 비용은 기존 계산 시간과 구분하여 측정해야 하며, 데이터 검증을 화면 스레드로 이동하거나 결과를 임의로 신뢰하여 응답 시간을 줄이지 않는다. 이미지 할당·인코딩 오류는 부분 결과를 복제하지 않고 오류 값으로 반환한다.

청크 원점과 결과 수락을 수정한 2026-10-22 비교는 2048 × 2048 선형 마스크를 조건별 새 프로세스 3회에서 실행했다. 수정 전 계산 중앙값은 201.83ms, 현재 계산만의 중앙값은 199.46ms, 결과 검증까지 포함하면 209.35ms였다. 검증 단계 중앙값은 11.17ms이며 작업 스레드에서 PNG·알파·윤곽·선택 개수의 일치를 확인한다. 최대 RSS 중앙값은 각각 130.67MiB, 130.70MiB, 130.80MiB이고 결과 해시는 모두 같았다. 이 변경의 검증 비용을 속도 향상으로 표기하지 않으며, 원본 준비 제거로 확보한 이전 단계의 메모리 감소와 구분한다. 원자료는 `build/verification/mask-coordinates-20261022-004057/validation-comparison.json`이다. 전경 입력·FPS는 측정하지 않았다.

## 갤러리의 오래된 SDK 작업 생략

목록 변경 뒤 결과만 버리던 방식에서, 아직 시작하지 않은 이전 목록·썸네일을 SDK 진입 전에 생략하는 방식으로 바꾼다. 원본 가져오기와 Photos 저장에는 보기 취소 플래그를 전달하지 않아 수락한 파일 작업을 유지한다. 실제 SDK 호출은 기존 객체별 FIFO로 직렬화하며 작업 풀을 추가하지 않는다.

검증은 실제 획득 객체와 제어된 PhotoLibrary로 실행 순서를 기록한다. 최초 스캔을 차단하고 목록을 두 번 더 요청했을 때의 스캔 개수, 첫 미리보기를 차단하고 이전 썸네일·원본·새 목록을 수락했을 때 원본 앞에 실행되는 작업을 비교한다. 이는 제거한 SDK 호출의 근거이며, 물리 라이브러리 지연이나 설치 앱의 전경 성능 측정은 아니다.

동일한 재현 입력을 수정 전후 바이너리에 연결한 결과, 연속 목록 요청의 실제 SDK 스캔은 3회에서 2회로 줄었다. 오래된 썸네일 뒤에 원본을 수락한 사례에서는 미리보기 호출이 3회에서 1회로 줄고, 남은 첫 미리보기 다음에 원본이 한 번 전달됐다. 두 조건 모두 SDK 동시 실행 수는 1이며, 차단 중 UI 이벤트가 처리됐다. 원자료는 `build/verification/mask-coordinates-20261022-004057/next-gallery-observation.json`과 `build/verification/gallery-queue-20261022-012031/repaired-gallery-observation.json`이다. 각 SDK 작업의 실제 iCloud 지연을 측정한 결과는 아니다.

## 갤러리 타일의 수명과 반복 조회

2026-10-22 실제 Camera/Photo 패널의 400행 Gallery를 제어된 PhotoLibrary와 연결해 측정했다. 기존 GridView가 처음 생성한 타일은 6개였으나 썸네일 한 개 완료와 선택 변경 각각에서 그 6개를 모두 파괴·재생성했다. 안정적인 C++ 행 모델로 변경한 뒤 같은 두 동작의 타일 생성·파괴는 새 프로세스 3회 모두 0개였으며, 최초 6개가 유지됐다. 기존 가상화와 크기·간격·아이콘·선택 표시는 유지한다.

같은 실험의 400행 목록 조회 500회 중앙값은 121.296583ms에서 0.008167ms, 선택 개수 조회 500회는 9.520292ms에서 0.000541ms로 줄었다. 새 조회는 정렬·필터·픽셀 접근 없이 캐시된 값을 반환한다. 새 카탈로그·검색·정렬 변경의 실제 재구성 비용은 남아 있다. 출력 체크섬은 모두 200500으로 같고, 작업자를 차단한 제어 조건에서 장치 감시를 시작하지 않았다. 각 단계 새 offscreen 프로세스 3회를 사용했으며 전체 앱 FPS·실제 Photos 지연·설치 앱 입력 지연의 측정값은 아니다. 원자료는 `build/verification/gallery-presentation-20261022-013713/presentation-comparison.json`이다.

## 권한 승인 뒤의 필요한 읽기와 보기의 수명

권한 완료가 목록 세대 변경만으로 버려져 승인 후에도 기다리는 상태가 남는 문제와, 상태 알림 중 다른 라이브러리로 전환해도 이전 권한 요청을 시작하는 문제를 재현했다. 요청 수명을 분리하고 현재 읽기 의도에 연결하여, 실제 승인된 현재 Photos 목록을 한 번 조회한다. 로컬 목록과 파일 작업은 유지한다.

실제 Camera/Photo 패널에서 승인 대기 중 숨김·닫힘을 수행한 재현은 각각 불필요한 SDK 스캔 1회를 관찰했다. 연결 해제는 표시용 읽기만 종료하며, 승인 완료가 이 보기의 스캔을 발동시키지 않도록 한다. 현재 보기와 복귀한 보기는 계속 조회한다. 이 검증은 제거한 SDK 호출의 범위를 나타내며 실제 iCloud·OS 권한 팝업 지연이나 전체 앱 FPS의 측정값은 아니다. 원자료는 `build/verification/gallery-permission-20261022-022207/view-interest-before.json`과 같은 회차의 최종 검증 기록이다.

## 게시 컨트롤러 종료와 콜백 수명

`SocialPublishingController`의 파괴자가 네트워크 작업자를 합류 대기하던 경로를
제거한다. 가짜 전송기가 취소 확인 전에 750ms 동안 반환을 보류하는 재현 조건에서,
기존 구현은 제공자의 반환까지 UI 종료를 기다렸다. 수정 구현은 공유 실행 상태에
전송기와 작업을 보유시키고 UI 소유자와 분리한다. 종료 시 큐를 닫아 다음 작업의
실행을 막고, 현재 작업에 취소를 요청한 뒤 반환한다. 원격 제공자의 처리 시간이나
취소 자체를 강제 단축한다는 의미는 아니다.

작업자는 앱의 GUI 큐에 값형 결과를 보내며, 약한 소유 참조가 유효할 때만 게시
행을 갱신한다. 최종 결과 알림 후 표시 변경 알림을 전달하여, 화면을 닫는 관찰자가
결과 전달을 끊거나 이미 파괴된 객체로 실행을 이어가지 않게 한다. 계정의 취소
콜백도 큐 잠금 밖에서 실행한다. 중복 요청 ID와 계정별 최신 변경 의도는 관찰자가
기록·계정 상태를 바꾸어도 유지한다. 상세 계약과 사례는
[SocialPublishing.md](SocialPublishing.md)를 따른다.

## 생성 완료 이후의 PNG·영상 저장

네이티브 RGB 인코딩, 프로세스 결과의 디코딩·고급 이미지 처리, 영상 검증과
파일 반영을 값형 저장 작업자로 이동한다. 배치마다 작업자에서 한 번 미러를
준비하고 GUI에서는 준비된 값을 채택한다. 저장 중에는 UI 이벤트를 처리하되
다음 생성 작업은 실제 파일 반영과 현재 작업의 완료까지 대기한다.

2048×2048 결과를 실제 PNG로 저장하는 동일 프로브로 엔진 반환 이후 GUI
이벤트 처리의 최대 공백은 749.853250ms에서 5.249208ms로 줄었다. 같은 구간의
타이머 처리 횟수는 1회에서 347회이며, 두 결과 모두 2048×2048 PNG로 저장됐다.
각 한 번의 제어된 관측이며 지연 보장 수치로 사용하지 않는다. 이는 PNG 저장 구간의 측정이며
실제 생성 장치나 전체 앱 FPS의 측정값은 아니다. 취소 경계, 결과 수명과
실행 근거는 [GenerationPublication.md](GenerationPublication.md)에 기록한다.

## 생성 엔진과 캐시 작업 중 표시 소유자 종료

생성·캐시 이행의 작업자가 컨트롤러를 참조하여, 종료 시 반환까지 기다리던 경로를
공유 취소 상태와 값형 입력의 수명으로 분리한다. 네이티브 미리보기 PNG 준비와
캐시 메모리 해제도 작업자에서 수행한다. 영상은 실제 캐시 해제 완료를 기다린 뒤
생성 프로세스를 시작하므로 두 작업을 UI에서 직렬 대기하지 않는다.

엔진 반환을 750ms 보류하는 동일 프로브로 소유자 종료와 공급자 반환의 순서를
확인했다. 소유자 종료는 759.730375ms에서 0.394209ms로 줄었으며, 수정 후에는
공급자가 아직 반환하지 않은 상태에서 소유자 종료가 끝났다. 각 한 번의 제어된 관측이며
실제 GPU 커널의 실행 시간이나 전체 앱 종료 시간의 개선 수치는 아니다.
실행 계약과 잔여 경계는 [NativeGenerationLifetime.md](NativeGenerationLifetime.md)에
기록한다.
## 외부 생성 워커 수명

생성 컨트롤러의 외부 워커 실행·입출력·취소를 iiSDKCore의 네이티브 소유권으로 옮겼다. GUI 종료 시 프로세스 대기를 제거하고 출력의 처리량과 대기량을 제한한다. 순서와 파일 수명·진행 감시의 계약은 [GenerationProcess.md](GenerationProcess.md)에 기록한다.
## 최종 구조 정리

생성 요청 구성, 실행, 진행·프리뷰 처리를 별도 파일로 분리하고 취소·종료의 소유권을 정리한다. 파라미터와 기존 UI는 유지하며 전체 등록 검사로 최종 회귀를 확인한다. [리팩터링 범위](Refactoring.md)에 상세 구성을 기록한다.

## 생성 검토 상태의 분리 (2026-10-24)

`ReviewSelection.h`는 Qt·파일·추론 API를 참조하지 않는 C++23 선택 정책이다.
`GenerationReviewModel`은 결과 스냅샷을 받아 상태·필터·정렬을 투영한다.
후보 선정, 제외, 선택, 포커스, 비교 뷰의 확대·팬과 필터 변경은 생성
컨트롤러 호출, 문서 revision, 저장소 refresh, 원본 파일 저장을 수행하지 않는다.

완료 결과가 추가되면 기존 도착 순서를 유지하면서 tail insert를 사용한다.
그리드는 가시 delegate와 소규모 재사용 풀만 유지하며, 미리보기는 비동기로
읽는다. 스크롤 위치는 결과 ID와 행 오프셋으로 기억하며 필터·정렬별 위치도
구분한다. 큐 진행률 갱신이 검토 모델의 entries 갱신을 유발하지 않는다.

일괄 내보내기는 명시적 Export 명령에서만 별도 작업자로 시작한다. 입력
스냅샷, 원본 참조, 성공·실패 집계를 UI 수명과 분리한다. 비교 100%는 제공된
미리보기 픽셀 기준이며, 고해상도 `.iisc` 원본을 매 프레임 전체 디코딩하지 않는다.

동작 계약과 회귀 검증은 [GenerationReview.md](GenerationReview.md)에 있다.
실제 사용자 선별 시간, GPU 생성 속도 및 p95 프레임 시간은 별도 측정 대상이다.

## 타임라인의 완성 프레임 유지

Dreamscapes의 표시 노드는 다음 프레임의 타일 준비 중에도 마지막 완성 텍스처를 유지한다. 비어 있는 중간 캐시를 화면에 반영하지 않으며, 준비된 프레임을 한 번에 교체한다. 전체 캔버스의 추가 비트맵 복사 없이 기존 장면 노드를 유지하고, 대기 중 팬·확대도 뷰포트 변환으로 처리한다. 실제 투명 프레임과 문서 교체는 각각 새 결과와 새 소유자이므로 이전 그림을 유지하지 않는다.

재생 타이머는 현재 렌더링이 완료된 뒤 최신 벽시계·오디오 시각의 프레임을 요청한다. 이로써 느린 프레임을 매 틱 취소하는 요청 폭주를 방지하며, 문서 리비전·Undo·파일 쓰기는 발생시키지 않는다. 실제 화면 픽셀 및 원본 무변경 검증은 `Dreamscapes.CanvasPresentation`을 사용한다. 상세 계약과 측정 한계는 [CanvasFramePresentation.md](CanvasFramePresentation.md)에 있다.
