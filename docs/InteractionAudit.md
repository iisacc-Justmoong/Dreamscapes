# Application interaction and data-flow audit

The Layer histogram path now separates view-owned analysis from document/file
commands. Its exact statistics, latest-pending work, visibility/live-stroke
deferral and optional-error ownership are documented in
[LayerHistogram](LayerHistogram.md). This scoped operation review does not claim
that every remaining method in the application is complete.

The active objective covers every application object and method, including
command dispatch, native work, ordered completion/error delivery, cancellation,
view/document separation and the information returned to presentation. It is not
limited to the editor timeline or a single passing test group.

The initial source snapshot includes 173 non-test C++/header/Objective-C++/QML/JS/
Java units (40,388 lines). The provisional lexical navigation index contains
122 class declarations, 2,575 QML object templates and 2,883 member/handler entries.
These are source-index counts, not verified semantic coverage. Compiler/QML
comparison, review of every indexed member and matching operation evidence remain
required before the whole audit can be considered complete. The initial hashes,
index and evidence are under `build/verification/interaction-audit-20261020/`.

## Review contract

For each object/method, trace its callers, input validation, actual operation,
completion/error response and output notification. Distinguish view-only changes
from document/storage commands. Review early returns, asynchronous ownership,
stale replies, cancellation, reentrancy, repeated conversions and broad refreshes.
An intentional unsupported capability must report that outcome; it must not
silently accept a command. Fake transports/local fixtures may verify contracts
without publishing real posts or starting a paid/external generation request.

## Confirmed first-pass findings

- The mobile Board command had no matching dispatch branch. It now shares the
  existing unsupported-action response with desktop/Home media actions and
  preserves the prompt. This does not add an audio/board generation engine.
- Opening a stored video changed the screen but returned `undefined`. The route
  now returns its accepted outcome consistently with image/native-canvas opens.
- `presentLatestResult()` could replace a manually opened file with an empty
  submission snapshot. Submission presentation now requires an owning job list.
- An unavailable file without drive identity returned `false` without an error;
  rejected download starts also retained their pending open intent. Both paths
  now report a failure and rejected starts clear that intent.
- Cancelling a queued publishing request only set a token, leaving the row and
  pending count queued until an unrelated active request finished. Cancellation
  and account disconnection now remove unstarted jobs and publish their terminal
  outcome immediately. Active uploads retain their provider-confirmed outcome
  and reconciliation contract. Completion signals are emitted outside the queue
  lock so observers can safely call back into the controller.

The route regression exercises desktop and mobile dispatch, accepted video opens,
manual-result ownership and invalid download metadata independently. The native
publishing fixture holds an unrelated active request, checks immediate queued
cancellation/disconnection and reentrant completion, and verifies that cancelled
jobs never reach its fake transport. Native unavailable-history presentation is
also checked to retain its complete Alias identity without a filesystem refresh.

## Remaining coverage

The file-export review found that ImageFileExporter constructed a FileSystem for
every external save. Its constructor opens the selected drive and starts mirror
observation, and successful export synchronizes that drive. File copying also ran
on the GUI thread. The SDK now provides a stateless media-export GUI adapter;
ImageFileExporter schedules the same validation/byte copy on a worker and delivers
one saved/failed outcome on the live GUI owner. Its boolean return accepts work;
it is not a completed-save result. Busy rejects duplicate requests without
replacing the accepted inputs. The worker owns its source/destination by value,
so closing the view does not block or cancel an accepted copy.

The Video workspace also read its current selection when the Save dialog was
accepted. It now captures the chosen video when opening the dialog, matching the
result gallery's existing contract. Both views show pending/completed/error
feedback and disable repeated file-save actions while busy. Tests cover queued
completion, GUI input during a gated copy, duplicate rejection, owner destruction,
byte preservation, validation errors and changing selection with an open dialog.
The SDK fixture hashes the selected drive before/after stateless export and
checks that unrelated drive metadata and files remain unchanged. Android URI
handling retains the existing provider route; physical Android execution is a
separate verification requirement.
The manifest fixture stops the disposable drive factory's existing presentation
watcher and drains its work before capture, then drains export publication before
comparison. A first unquiesced attempt observed that factory watcher's unrelated
text-preview creation; it is retained separately from operation evidence.
The deterministic video fixture embeds the requested output identity as MP4
comment metadata. Its decoded blue frames remain unchanged; distinct file bytes
make the selection-change export assertion sensitive to copying the wrong result.

The reference-display audit found four QML image bindings calling the synchronous
GenerationController.imagePreview accessor. That accessor uses its retained
FileSystem (it does not construct one), resolves the Alias, ensures the runtime
preview directory and may render/write a physical PNG during source evaluation.
The views now use the existing CanvasPreview bridge and asynchronous IISC image
provider. The separate imageCanvasPreview helper remains an imperative generation
input operation. A UI regression injects an observable backend into the actual
four views: display must make zero storage-preview calls and still load native
pixels. A separate cache-key case preserves mtime/size across an edit and checks
invalidation, while retaining the key across provider-owned metadata publication.

Verification distinguishes decoded image readiness from painted frame readiness:
the Home card regression waits for the actual artwork pixels. The flat-history
fixture includes a real provider-owned preview companion and excludes only the
provider's reserved control files from the user-asset inventory, while preserving
the original asset-byte assertion. The multi-window Home route group has a larger
aggregate runner budget; individual action watchdogs retain their short limits.
The Generation suite runs serially within its CTest pool because its CPU-progress
fixtures deliberately use a 1.5-second stall deadline. The test and production
watchdog limits are unchanged; UI/ffmpeg fixture contention must not substitute
for a stalled worker in that assertion.

The worker-deadline audit also found that a late GUI timer could fail a job before
intaking stdout already waiting in the process pipe. The deadline callback now
polls available output without waiting, consumes it, then rechecks the owning
request and its deadline. Accepted progress or completion prevents the obsolete
timeout; heartbeat-only output still fails. A barrier-controlled fixture flushes
an event before exposing its ready marker while the test withholds GUI dispatch.
The original callback fails its progress row and passes its heartbeat row; both
rows must pass with ordered intake. Production timeout limits are unchanged.

The application-wide audit is still in progress. Native editor tools and panel
parameter dispatch, generation/worker queue and persistence, video recipes,
preferences, exports/capture, all platform adapters and MCP command paths still
require complete per-member review and evidence. The shared Alias lookup hotspot was measured and repaired in the SDK; package
consumption and existing-binary compatibility are verified separately from the
remaining application-wide member review.

Hypotheses are not treated as defects without inspecting the owner contract.
For example, `FileDownload::ready` follows native storage synchronization, and
unavailable native Aliases intentionally have an empty local path. Their existing
metadata/open identity is preserved; no speculative SDK downloader change is part
of the first patch.

```sh
cmake --build build --target DreamscapesGuiTests DreamscapesGenerationTests DreamscapesSocialPublishingTests
ctest --test-dir build -R '^Dreamscapes\.(InteractionRoutes|SocialPublishing|StorageBrowser|ViewStateBoundary)$' --output-on-failure
```

A further confirmed route defect was the desktop Recent files row: it connected
`openFileRequested` but left the reusable row's `openFileAction` at its hidden
default. The existing Home editor route test reproduced the missing dialog from
an actual click. The desktop row now explicitly enables that action.

The Alias fixture measured 10,000 accepted rows and 2,000 lookups over three
passes: median 522,700 microseconds before and 621 microseconds after indexing.
The exported header/layout remains unchanged (`sizeof(Container) == 152` on this
arm64 build). Snapshot validation, ordering, replacement and notifications retain
their existing contracts. SDK alias/dashboard/preview/file-action/drive/shared-
storage/files-view regressions passed 8/8. These are isolated lookup measurements;
they do not represent whole-application response time.

Result presentation also retained completed/latest snapshots for notification
comparison while recomputing them in every public getter. Those getters now
return the published value snapshots directly. Projection occurs on producer job
or Alias-revision changes before notification; unrelated Home queries do not
invalidate it. The regression repeatedly reads completed/latest outputs, checks
no queue/storage work or notifications, tests caller-copy isolation, and verifies
invalidation/restoration when the owner publishes availability changes.

The retained-result change also covers rejected storage reconnection. Clearing a
session publishes the empty projection before legacy-cleanup error callbacks, and
both success and rejection notify the replaced job list. A fixture first generated
a result, occupied a disposable legacy path, and reproduced stale results on the
new cached-getter implementation. The guard fixes that failure path while keeping
the occupied file intact; no installed release with that intermediate behavior
was deployed.

Selection panel reads also scanned every alpha value even though the count only
changes with a selection operand. The count now updates at selection mutations
before presentation notifications, including background masks and temporary or
detached previews. Repeated getter, subtract/clear/replacement and background-mask
ownership regressions cover this boundary separately from document editing.

The final integration attempt retained an intermittent working-file save failure.
A diagnostic-only SQLite interposer identified `SQLITE_IOERR_LOCK` (3850), system
error 9, at `BEGIN IMMEDIATE`. The same full timeline failure reproduces with the
previous installed Container Core (UUID `2AD9CE6C-2854-3EE9-A512-AB6D67F6660F`);
the isolated save test passes. No SQLite locking or durability policy was changed
to hide it.

The SDK-level reproduction subsequently isolated concurrent presentation reads
and working-file writes. Before coordination, an uninstrumented 50-save loop
failed on save 6; explicitly draining presentation first passed all 50. The
production repair instead coordinates actual reads/closes, document transactions
and preview source copies through iiFileProvider Native source identity. Idle
file bindings, user edit callbacks and detached rendering do not retain access.
Queued presentation starts after a changed commit releases its scope. SQLite's
cross-process conflict and durability rules remain unchanged.

A SQLite VFS observer verifies transaction entry: the previous Core began a write
while another source owner held access; the repaired Core waits, while still
running the edit callback outside that scope. Another negative control pauses a
domain renderer: the old provider blocks an editor until rendering resumes; the
new provider permits the scoped write and rejects the obsolete preview. Existing
Core exports and the Quick adapter remain unchanged. The private candidate uses
iiFileProvider 0.7.3, Native 0.8.3 and iiSharedCanvas 0.31.2 Core. The unquiesced
application-storage loop then completed 50/50 saves in 43.53 seconds with one
verified Native image. This is storage fixture evidence, not whole-app latency
or completion of the application-wide member audit.

A separate two-renderer barrier also exposed concurrent preview publication:
two requests could render the same source, then the second treated the first
renderer's own attribute update as an external change. Source intake now compares
normalized provider change tokens, reuses an already valid projection and retains
external-change rejection. Early application runs also used a nonexistent test
temporary directory and are retained as harness failures; corrected runs create
and verify that directory before starting any process.

Native-window tests also encountered activation failures, including an isolated
Home dialog-return attempt. Its unchanged isolated rerun passed; the failed
attempt is retained. The final current integration has 13 passing groups and
20 passing cases across independent Cocoa/Metal/startup processes, including
Home routing, downloaded-file opening, video playback/export and view transitions.
This is the selected increment's verification, not a clean whole-application run.

Unchanged generation preferences previously entered the atomic file writer and
republished `modelPreferencesChanged` on every repeated setter. The persistence
boundary now returns immediately for an equal value with no outstanding error.
An invalid previous load still writes safe defaults and clears its error, and
changed values retain atomic-write failure handling. Applying an existing default
still restores an explicitly overridden model selection; only redundant storage
and preference publication are skipped. The regression blocks the writer's
temporary path after a valid save, repeats the same model/prompt forty times,
checks byte preservation and zero preference/job notifications, then verifies
selection restoration and malformed-file repair separately.

The dependency audit also reproduced a ready WAL delivery database waiting behind
another process's initialization lease. iiSocietyHelper now prepares the SQL
driver before that lease and opens a supported, initialized WAL store without
waiting for it. First schema creation remains serialized, and future schema
versions remain rejected. A held-lease negative control and an existing-snapshot
read exercise the distinction. This path belongs to `Helper::start`; the current
Dreamscapes `Client`/FileSystem routes have not established a call to that method.
It is an SDK regression repair, not evidence of a Dreamscapes-wide latency cause.

## Clipping presentation reads

The editor's `toolState()` getter used to composite the full native frame, scan
its pixels and PNG/base64-encode the clipping overlay on a cache miss. A hidden
Image source binding still evaluated that getter. The negative control measured
966,617 microseconds for the first read on a 1,024 by 1,024 fixture.

`toolState().clipping` now contains only support/enable flags, highlight/shadow
flags and the existing white/black thresholds. `EditorClippingOverlay` owns the
display operation and its `busy`, `ready`, `available`, `error` and failure
notification. It accepts no pointer input. The painted surface is clipped to the
viewport and preserves the original native-pixel threshold colors, alpha rules,
frame and active-artboard composition without a PNG/data-URL round trip.

The visible overlay captures a detached immutable SDK rendering snapshot on the
GUI owner and performs composition/mask generation on a worker. Producer changes
coalesce before capture; at most one operation runs with one latest pending
request. Hidden/disabled/minimized views do not start work. A changed request
clears old pixels immediately; obsolete results/errors cannot publish. Closing
the view never waits for or grants a file binding to the worker. Panning changes
only painting coordinates, not the requested native image.

The SDK snapshot boundary was specified and tested independently before product
adoption: all 64 SDK tests passed, followed by an installed-package consumer.
Quick retained its 307 previous exports and added one protected capture method;
Core retained all 642 exports. This increment updates the product-private SDK
prefix only.
`toolStateDoesNotMaterializeClippingPixels` checks the pure metadata contract and
byte/revision preservation. `Dreamscapes.ClippingOverlay` covers native pixel,
alpha, frame and artboard output, hidden reads, GUI input during a gated worker,
latest-only navigation, shared snapshots, stale failure suppression and owner
destruction. Runtime/package evidence is kept separately under
`build/verification/clipping-preview-20261020/`.

## Detached editor frame export

The separate editor `exportImage` path still rendered, encoded and saved on the
GUI thread. It constructed a FileSystem for destination checking and another for
saving, activating drive observation and broad mirror synchronization for an
explicit frame export. The IISC branch also encoded and immediately decoded the
same flattened document before creating its authoring file.

The File panel now captures the immutable frame/artboard snapshot and pixel
density before opening its Save dialog. `EditorFrameExporter` retains that draft;
canceling the dialog discards it without work. Accepting the dialog consumes the
draft and starts one value-owned worker. Later frame, artboard, document or
selection changes cannot change the accepted frame. Direct C++ `exportImage`
callers without a prepared dialog capture their current frame at acceptance.
Each EditorProject shares one export command owner across its replacement canvas
objects. A negative control reproduced destruction of the prepared request when
opening a new canvas while the Save dialog was open. The retained project owner
now keeps that chosen frame independently of the current document; standalone
canvases retain their own export owner. No export draft becomes document state.
The boolean return accepts work; `saved(destination)` or
`failed(destination, message)` is its terminal outcome. `rejected(message)` is a
separate unaccepted request, including duplicate starts while busy.

Native composition, image encoding and file publication run on the worker.
Ordinary images encode into the provider's atomic stream without a second full
encoded buffer. The SDK's stateless `mediaExportDestination` GUI adapter preserves
Society section/redirect rules; `exportCanvasCopy` creates a new authoring file
from detached data, preserves occupied targets and native Files-only placement,
and requests only the target's provider preview. Neither creates a FileSystem or
refreshes a mirror. Existing save/versioned-document operations keep their owning
FileSystem contract. The now-unused application `canvasDestination` wrapper was
removed rather than retaining an alternate drive-activation route.

`Dreamscapes.FrameExporter` verifies queued completion, event-loop input while
the worker is gated, duplicate rejection, a real export after owner destruction,
reentrant completion/request identities, exception delivery, PPI/content and
occupied-file preservation. `Dreamscapes.FrameExporterHost` drives the actual File
panel handler and Save acceptance after changing frames, then checks the chosen
pixels, terminal status and unchanged IISC. SDK fixtures compare the selected
drive's complete file manifest before/after external export and independently
check native and bitmap destination restrictions. These checks do not complete
the application-wide object/method audit.

## Timeline and asset audio preparation (2026-10-21)

Reviewed the concrete chain `EditorCanvas::playAudio/playTimelineAudio` →
`EditorMedia::playAsset/playTimeline` → `EditorAudioPlayback::play` →
`prepareAudio` / `dreamscapes::mixTimelineAudio` → output start → native timeline
clock. The prior chain ran full mixing and PCM copying on the GUI thread while
QML advanced a separate wall clock immediately. A synthetic negative control
also reproduced an asset preview continuing under a newly started silent
timeline (`ownership-red.xml`).

The native controller keeps accepted request IDs, pure status getters,
value-owned worker data, one running/latest pending job, cooperative cancellation
and separate rejected/started/finished/failed/cancelled outcomes. Device events
are queued out of the device's own start/state stack. Exceptions become terminal
errors, preparation has a device-start deadline, and stale completions cannot
revive a stopped or replaced request. Destruction does not wait for preparation.
The single-asset path borrows its snapshot asset and the output shares native
little-endian PCM rather than copying a full byte buffer on the UI thread.

`EditorTimelineModel::setPlaying/bindPlayback/beginPlaybackClock/
updatePlaybackPosition/endPlayback` now own the transport. Preparation must
reach output-start before its clock advances, and the exclusive end includes the
last frame's duration. Seek, external frame changes, document/canvas replacement
and QML visibility/enabled changes cancel playback. Navigation still uses
`CanvasItem::setFrame`, without revision, file writes, history or projection
publication. Audio notifications no longer enter the camera tool-state/mask
update chain. Mixing mathematics remains covered by the existing sample-level
EditorTools regressions, with added block cancellation and finite-gain checks.

Evidence is kept under `build/verification/audio-playback-20261021/`:
`Dreamscapes.AudioPlayback` checks gated input responsiveness, coalescing,
ownership, stale notifications, exceptions and 24 / 30000:1001 boundaries;
`Dreamscapes.AudioPlaybackHost` exercises actual QML/host output and cancellation.
Host logs explicitly identify whether an audio device really started. This
increment reviews this chain only; it does not certify all application members,
physical mobile audio or whole-application performance.

## Photo library command lifetime (2026-10-21)

Revisited all `PhotoLibraryExporter` members and the Apple/Android/unsupported
adapters. The prior source-review entry did not prove reentrant or duplicate
completion behavior. A negative control now shows that an old duplicate native
reply clears a later save's busy state, and a thrown operation escapes the
command instead of producing a terminal failure (`photo-library-20261021/red.xml`).

The command retains its operation/input before busy notification, accepts only
the first reply per request and queues delivery to its GUI owner. Native
exceptions become failures; rejected starts have a separate reason/outcome and
cannot fail active work. Terminal delivery precedes idle observation. Deleting
or reusing the owner from a busy/terminal observer is covered explicitly. The
result view handles rejected requests through its existing feedback. See
[PhotoLibraryExport](PhotoLibraryExport.md) for the complete signal contract and
the boundary between injected adapter tests and physical platform verification.

The refreshed lexical navigation index at this stage contains 183 non-test
source units and 2,961 provisional member/handler entries. It is still not a
semantic audit count; per-source hashes, compiler declarations and verified
operation evidence are tracked separately. New/changed members and the remaining
video draft, platform and application command chains keep the whole goal active.

## Video composition command validation (2026-10-21)

Read the Video workspace/parameter/timeline-state command chain. Five negative
controls show that duration/rate edits can publish invalid parameter or timeline
state before validation: too many minimum-length shots for the requested extent,
rounded image-condition collisions, NaN frame count/rate and zero FPS. The prior
shrink path checkpointed the invalid candidate and called Undo, leaving it in
Redo; the parameter panel ignored the command's outcome.

`resizeComposition` now plans and checks all shot durations and distinct image
anchors before checkpointing once. It returns an explicit accepted/rejected
outcome. The parameter panel updates values only after acceptance and marks a
rejected field invalid without dropping any other parameter. Rounding reserves
the minimum duration of every later shot, permitting feasible small edits.
The native GUI fixture checks rejected draft/history preservation, valid retry,
minimum allocation and Undo/Redo with zero generation jobs. Native recipe/output
validation and queued generation remain separate downstream contracts.

Other VideoTimelineState commands have been located and source-read, but their
individual invalid-input, undo and runtime contracts are not all proven by this
increment. That distinction is retained in the member review rather than marking
the whole video object complete from the resize regression.


## Native result save and export completion audit (2026-10-21)

A source-confirmed route mismatch passed `image://iisc-preview` to APIs requiring
an original local file. Production-result UI negative controls reproduce this
for native snapshots and working documents. Display and command identities are
now separate: source rendering stays asynchronous, while commands retain the
native document URL. SDK contracts were specified and frozen sequentially before
app adoption: iiSharedCanvas 0.31.3 detached read-only full snapshots, then
Helper 0.10.1 stateless full-size PNG export and read-only native readCanvas.

The photo operation chain now has worker preparation, typed path/MIME dispatch,
request-owned temporary resources and first-reply cleanup. Tests retain all prior
callback/exception/reentrancy cases and add closed-view preparation, original-size
native content, MIME and resource-lifetime cases. The platform operation remains
GUI-dispatched; source I/O and rendering do not run there. No real OS photo asset
is published by the injected tests.

A separate negative control reproduced file/frame exporters losing terminal
signals when idle observers deleted their owner. They now deliver saved/failed
before idle, guard reentrant next jobs and distinguish rejected file starts from
accepted failures. This increment reviews the indicated operation chains; it does
not certify every application member or complete the full audit objective.


The next original-source trace reproduced two distinct selected-image failures
with a real replica StorageMap Alias and local cached JPEG: StorageBrowser
excluded the unavailable image from multi-selection, and Main.openImages
rejected a direct selected-image command without starting hydration. The negative
control did not demonstrate an actual reduced-preview import; that earlier
source hypothesis is explicitly not claimed as observed behavior.

ImageOpenRequest now owns the native ordered preparation and lifetime boundary.
ImageSelection retains Alias identity across availability changes; Main hands
the editor original URLs separately from presentation/result metadata. The
existing SDK FileDownload owns filesystem, transport and verification work.
Batch retry, navigation cancellation, drive/root replacement and shared transfer
ownership are part of this chain. Application tests model original arrival and
exercise the real native Alias/download handoff; authenticated network transfer
is covered separately by the existing SDK Client tests. The current increment's
evidence is build/verification/image-open-20261021.

This is another scoped semantic review, not completion of every source member.
The following trace entered EditorProject::openImages / attachImages and
pagePreviewSource. The then-current implementation created temporary EditorCanvas
objects and performed source I/O/decoding or page rendering synchronously. The
subsequent preview and original-import increments below separate those paths.
The full application audit remains active.


The existing advanced-generation regression caught a metadata-shape regression
in the first native preparer: generic Alias fields were inserted into generation
results. Generated results now retain their schema and parameters; command URLs
remain a separate signal argument. A dedicated native test also retains a result
with a nested Alias and a generation-owned version field.

The broader gallery replay reproduced accepted wheel events with no scrolling
and a lost scroll position after an editor round trip, including 1000 results.
A viewport WheelHandler now applies bounded wheel deltas without invoking the
backend. The result view captures its position before detail/editor transitions
and keeps pending restoration separate from user wheel/touch/scrollbar movement.
The existing gallery cases also assert that navigation does not change the
generation queue. The Sidebar fixture now compares the installed LVRS semantic
accentBlueMuted token rather than a stale hardcoded palette value; product
Sidebar colors are unchanged. Initial failures and isolated reproductions remain
in build/verification/image-open-20261021.


The first installation attempt found an actual packaged-entry gap that the source
GUI fixtures did not cover: ImageOpenRequest was registered only in the fixture
entry point. The installer restored the previous bundle after the failed root
load. The existing packagedApplicationStarts case independently reproduced the
missing production type before the fix. Production and source fixtures now share
StorageViewTypes for GUI type registration; only their explicit generation/draft
factory policies differ. Dreamscapes.PackagedApplication runs the canonical real
entry point before installation, including its actual embedded resources and
helper discovery. This separates root-window startup evidence from the small
ApplicationLifetime lifecycle executable. Failed installation/startup evidence
is retained alongside the subsequent repair and rerun.


A subsequent Cocoa gallery check exposed an overstrict fixture condition: its
retained anchor was intact, but the native composer finished changing gallery
height from 766.88 to 772 pixels. The correct bounded viewport was at 668 while
the requested anchor remained 673.12. The gallery tests now distinguish retained
anchor from valid end clamping, assert unchanged window size, finish touch
movement before programmatic navigation, and use a physical-pixel allowance for
native clip rounding. The reachability and queue-preservation checks remain.
Diagnostics and the initial native failures are retained; these fixture
corrections are not presented as an additional product scroll repair.

The next native run passed the retained-anchor checks but attempted its final
Cancel click at x=1289 after resizing the window to 640 pixels. The grid column
binding had updated before the toolbar's native layout was polished. The fixture
now waits for the actual Cancel button bounds inside the resized viewport, then
performs the same real click and verifies the Home transition. The out-of-window
input diagnostic is retained with the other native evidence.

The next page-preview audit reproduced GUI-owner rendering with a controlled
renderer: the existing synchronous source call returned only after the 2107 ms
gate. `EditorPagePreviews` now owns bounded optional rendering work; the project
shares the SDK's immutable snapshot plus author visibility and the page strip listens for its
indexed completion. This preserves the existing coalesced document invalidation
signal separately from asynchronous image readiness. Tests cover current-page
focus versus other-page pixels, editable/file state, stale documents, pending
frame replacement, queue bounds and owner destruction. Image import/attachment
still needs its own original-source I/O/decode and storage-side-effect review;
that remaining operation chain is not certified by the preview changes.

The pending-completion stroke regression failed for both finish/cancel before the
forwarding correction. Indexed completion now follows the same live-stroke
presentation deferral as document invalidation. Page-strip hiding also retires
pending optional work without joining the running renderer or clearing completed
images. The next scope still includes synchronous image import/attachment and
the SDK's initial GUI snapshot copy; no copy-on-write raster payload is claimed.

The integration replay found the second preview consumer in EditorPageDeck: the
page strip had its readiness connection, but neighboring cards did not. Both now
listen for indexed completion. The same broader replay found a stale FileInfo
fixture expecting fileInformationChanged for frame/page presentation changes.
Its directly constructed EditorCanvas sources and fixture match the previous
installed source hashes; the fixture now asserts dedicated view notifications,
zero broad file notifications and unchanged encoded document/revision.

## Original decoding and native adoption (2026-10-21)

A rejected image batch reproduced an unintended managed-storage write: the first
bitmap's wrapper was published before a later missing source rejected the batch.
The native preparation boundary now validates all originals and the aggregate
before wrapper imports. It moves native payloads instead of copying them through
temporary EditorCanvas instances, and remaps appearance-mask asset references
that were omitted from the previous artboard namespace conversion.

The GUI command is owned by EditorImageImportRequest with detached worker inputs,
one running/one latest pending batch, original/drive/target identity checks and
non-blocking cancellation/destruction. Main waits for successful native adoption;
record-to-artboard indices preserve opaque generation metadata across canonical
deduplication and multi-artboard sources. These changes are documented in
[EditorImageImport](EditorImageImport.md), including individually durable wrapper
storage and remaining synchronous native commit/first-editor initialization costs.

An independent native consumer preserved component verification while the shared
LVRS macOS package configuration was temporarily missing. The restored package
allows the canonical build and mounted offscreen routes to be checked separately.
Verification receipts distinguish those checks from installed-runtime evidence;
whole-application semantic completion is not claimed by this increment.

## View-owned storage activation (2026-10-21)

The constructor audit found a hidden backend boundary: each EditorCanvas created a FileSystem whose default constructor immediately opened/scanned storage and started monitoring. The consumer now chooses deferred initialization; unsaved editing and pure navigation remain unselected until a real storage command. The SDK contract was built/tested/staged and consumed independently before adoption. [StorageInitialization](StorageInitialization.md) records the unchanged persistence rules and links the subsequent working-file preparation review.

## Accepted working-file open (2026-10-21)

The next native source-access regression held a file token for two seconds. The
previous Project open waited 2024473 microseconds on its caller. The accepted open
command now prepares native/legacy contents, storage mirrors and Alias versions
off the GUI owner. Its first replay returned in 44 microseconds while retaining
the old document and delivering input-loop events. Final native adoption remains
a separate cost. [WorkingFileOpen](WorkingFileOpen.md) describes the source/drive
identity guards, request lifetime, preserved file ownership and completion-based
Home/editor routing.

The same route review found an unconditional shared-download cancellation in
Main. It now checks the active key and the captured container ID. A read-only
view change cannot cancel a transfer it does not own. Legacy page selection also
maps to the correct artboard offset instead of treating a page index as an
artboard index. These are scoped operation-chain changes; whole-application
semantic completion remains unproven.


## 진행 중: 한 점 색상 읽기와 가져오기 바인딩

색상 한 점을 읽기 위해 전체 프레임을 동기 렌더링하던 Layer 표본과 화이트 밸런스 입력을 C++ 작업 소유자로 분리했다. 호출 수락과 결과 적용을 구분하고, 처리 중 하나·최신 대기 하나·오래된 결과 폐기를 검사한다. 일회성 표본은 캡처한 프레임과 좌표를 유지하고, 실제 편집으로 이어지는 화이트 밸런스에는 선택 대상과 프레임 가드를 추가한다. 조회·취소·실패는 파일 상태를 발행하지 않는다. [상세 계약](EditorColorSampling.md)을 참조한다.

ImageImportRequest는 프로젝트·컨테이너 바인딩을 취소 알림보다 먼저 설치한다. 취소 관찰자의 재시도·재바인딩·소유자 삭제 이후 이전 호출이 새 요청을 덮지 않도록 바인딩 세대와 생존성을 확인한다. 유휴 바인딩의 대상 소멸도 알리며, 캔버스 교체 중 재시도는 처음부터 새 캔버스를 감시한다.

이 항목은 전체 객체·메서드 감사의 일부이다. 지속적 생성·편집·저장 호출, 최초 스냅샷·초기화, 아직 의미를 직접 검사하지 않은 멤버가 남아 있다. 소스 목록이나 어휘적 멤버 수는 동작 검증 완료 수가 아니다. 설치와 실행 앱 검증은 빌드·패키지 후보 검증과 별도로 기록한다.


## 진행 중: 그래픽 설정 보조 계산

8종의 보조 계산이 전체 이미지 미리보기 API를 호출하여 사용하지 않는 픽셀 결과와 PNG를 만들던 경로를 분리한다. 표준 C++ 입력·결과, 패널별 작업 소유자, 최신 요청 하나와 실행 중 하나, 프레임·대상·설정 컨텍스트 가드로 설정 반영을 연결한다. 순수 설정 계산은 파일 상태를 발행하지 않는다. 큐에 넣은 실제 이미지 명령도 실행 전에 원래 대상을 확인한다. `GraphicHelperValues`, `GraphicHelper`, `GraphicHelperGui`의 검증 범위는 [상세 문서](GraphicParameterHelpers.md)에 기록한다. 전체 객체·메서드 검증 완료를 의미하지 않는다.

## 진행 중: 카메라·사진 객체의 수동 상태와 획득 작업

`EditorCameraPhoto`는 에디터마다 생성되므로 생성자의 `QMediaDevices`와 임시 폴더 할당을 실제 장치 조회·출력 시점으로 분리한다. 변경 전 검사에서 생성 시 감시 생성 함수가 호출되고, 갤러리 목록 조회와 변환 없는 로컬 이미지 가져오기도 출력 폴더를 만든다는 세 가지 실패를 재현했다. 수정 후 검사는 수동 상태·갤러리 조작, 장치 감시의 단일 초기화·신호 연결, 출력 파일의 수명과 오류 재시도를 포함한다. 이 객체의 파일 시스템 작업은 설치된 `iiFileProvider` 네이티브 API를 사용한다. 상세 계약은 [EditorCameraPhoto.md](EditorCameraPhoto.md)에 있다.

전체 앱 객체·메서드 감사는 진행 중이다. 앞선 그래픽 보조 계산 검증에서 `EditorCanvasGui` 네 메서드 그룹이 기존 180초 제한을 초과한 기록은 보존한다. 이후 같은 조건의 검증 결과는 회차별 근거에 별도로 기록한다. 카메라 경계의 개별 검사 성공으로 기존 회차의 실패 결과나 전체 완료 상태를 대체하지 않는다.

## 진행 중: 동적 래스터 선택과 원본 일치

일반 선택 경로가 동적 비트맵의 픽셀 편집 연결을 생략하던 문제와, 프레임 변경 후 보정·Reset·지우개가 이전 원본을 참조하던 문제를 네이티브 검사로 재현한다. 문서·레이어·실제 애셋을 기준으로 캐시를 구분한다. SDK의 미완료 페인팅 취소 순서도 모듈 자체 검사와 앱 소비자 검사로 분리한다. 상세 계약은 [DynamicRasterEditing.md](DynamicRasterEditing.md)에 있다. 전체 객체·메서드 감사는 계속 진행 중이다.

## 그래픽 이미지 명령 경로

패널의 미리보기·Apply가 UI 이벤트 루프에서 동기 처리를 실행하던 경로를 작업자 소유 명령으로 교체한다. 이미지 없는 설정 계산과 실제 픽셀 명령의 허용 조건을 구분하며, 큰 청크에서 null 픽셀을 사용하는 경로를 차단한다. 원본 해석은 표준 C++의 `editorRaster::read`를 공유하고 Qt GUI 객체를 작업 입력에서 제외한다. 전체 객체·메서드 감사와 설치·실제 입력 성능은 계속 별도 범위이다. 구체적인 계약과 검사는 [GraphicImageCommands.md](GraphicImageCommands.md)에 기록한다.

## 진행 중: 내부 마스크 입력과 보정 배열

작업 스레드 안에서도 내부 마스크를 픽셀 수만큼의 범용 값으로 확장한 뒤 SDK 배열로 재해석하던 중복 변환을 제거한다. 네이티브 마스크 뷰는 동기 호출 동안만 빌리며, 공개 맵 입력과 검증 규칙은 유지한다. 새 마스크 미리보기에서는 사용하지 않는 선택 영역 복사를 생략한다. SDK가 지원하는 독립적인 `adjustments` 배열을 앱의 선택 영역 사전 검사와 강제 `mask` 삽입이 차단하던 문제도 수정한다. 변경 전 세 가지 실패를 재현하고 SDK 직접 호출, 파일 무변경, 적용·재열기와 자원 사용을 별도로 검사한다. 이 항목의 완료는 전체 객체·메서드 감사나 설치 앱의 성능 검증 완료를 뜻하지 않는다.

## 진행 중: 사진 원본 준비와 원래 문서의 배치

원본 준비 도중 문서를 교체하면 이전 결과가 새 문서에 삽입되는 결함과, 획득 객체 종료가 진행 중인 원본 내보내기를 기다리는 결함을 실제 검사로 재현했다. 수락 토큰·대상 문맥·임시 파일 공유 수명·별도 준비 작업자를 연결하고 한 번의 문서 트랜잭션으로 배치한다. 보기 탐색은 원래 대상과 별개로 유지하며 실제 문서 변경과 명시적 취소는 오래된 결과를 거부한다. 상세 계약은 [PhotoImportCommands.md](PhotoImportCommands.md)를 따른다. 전체 객체·메서드 감사, 물리 장치와 설치 앱의 실제 입력 지연은 계속 별도 범위이다.

## 기하 마스크 수요와 동적 변환 경로

`graphicImageInput → graphicImage::calculate → EditorGraphicAdjust::makeMask`는 기하 마스크에도 문서·원본 픽셀·전체 RGB 변환을 전달하던 경로였다. 기하 적용에는 크기와 샘플한 변환만 전달하고, 원본 비교 미리보기와 픽셀 의존 연산은 실제 원본 경로를 유지한다. 기존 대상 검증·선택 피연산자·파일 상태 분리를 생략하지 않는다.

같은 추적에서 마스크의 좌표 매핑이 레이어 기본 변환을 사용하여 현재 프레임의 모션을 놓치는 문제도 재현했다. 캡처와 작업자 경로에서 SDK의 `sampleLayerAt` 결과를 사용한다. 실제 수락 명령과 직접 계산을 모두 검사하며, 전체 객체·메서드의 의미 검토가 완료되었다는 근거로 확대하지 않는다.

## 청크 원점과 마스크 완료 상태 검증

유효한 8 × 8 청크 캔버스의 원점이 (-1, -1)일 때 기본 선형 마스크가 64개 대신 49개 픽셀로 밀리던 문제를 재현했다. 원본 이미지의 픽셀 원점을 샘플한 레이어 변환에 포함하고, 캡처와 작업자에서 공통 원본 좌표 정보를 사용한다. 양수·음수 원점, 이동·회전·모션과 픽셀 의존 마스크를 함께 검사한다.

또한 마스크 데이터와 윤곽·오버레이가 없는 작업자 결과도 `ready`로 수락되어 선택이 존재한다고 표시되는 문제를 재현했다. 실제 선택을 수정하기 전에 결과 계약을 검증하고, 실패 시 기존 상태와 파일을 보존한다. 내용 검사는 작업자에 두며, 빈 정상 선택·재시도·원본 비교 미리보기 경로를 유지한다. 이 수정은 실제 메모리 고갈이나 모든 코덱 장애의 완료 증명이 아니다.

## 진행 중: 갤러리 보기와 수락된 파일 작업의 대기 순서

연속 목록 변경이 중간 요청까지 모두 스캔하고, 오래된 썸네일 두 개가 이미 수락한 원본보다 먼저 SDK에 진입하는 것을 제어된 실제 획득 큐에서 재현했다. 목록 세대의 값형 플래그를 대기 작업에 전달해 SDK 시작 전에 생략하고, 원본과 Photos 저장은 기존 파일 작업의 수명·순서·취소 규칙을 유지한다. 실행 중 SDK의 즉시 취소나 모든 갤러리 메서드의 의미 검토 완료는 주장하지 않는다. 검사는 [PhotoImportCommands.md](PhotoImportCommands.md)에 명시한다.

## 진행 중: 갤러리의 증분 표시와 조회 경로

실제 가상화된 Gallery에서 썸네일 완료·선택 변경이 표시 중인 타일 전체를 교체하는 것을 재현했다. 획득 객체가 전체 값 목록을 매번 재정렬하여 발행하던 표시 경로를 별도 GUI 행 모델과 조회 캐시로 교체한다. 모델은 카탈로그 교체·필터와 개별 상태 갱신을 구분하고 변경 행만 알린다. 원본 가져오기·Photos 저장·문서 배치는 기존 작업 소유자와 큐를 유지한다.

새 알림 경계에서 필터 변경·객체 삭제와 원본 목록의 일관성을 검사한다. 오류 초기화 중 새로운 목록 요청이 발생하면 기존 호출이 덮지 않으며, 모델을 게시하기 전 상태를 반영해 재진입한 썸네일 오류를 보존한다. 목록 행 모델의 검증은 전체 카메라·사진 객체나 전체 앱의 의미 검토 완료를 뜻하지 않는다. 상세 계약과 측정 범위는 [EditorCameraPhoto.md](EditorCameraPhoto.md)와 [Performance.md](Performance.md)에 기록한다.

## 진행 중: 권한 승인과 갤러리 읽기의 연속성

승인 대기 중 보기 변경으로 완료 처리가 목록 읽기를 건너뛰는 경우와, 알림 관찰자가 다른 라이브러리로 이동해도 이전 권한 호출을 시작하는 경우를 재현했다. 독립적인 권한 요청 번호·소유자 검사·현재 읽기 의도를 사용하여 실제 작업을 연결한다. 승인·거부·미지원과 실패 후 재시도를 구분하고 오래된 응답이 새 요청을 끝내지 못하게 한다.

실제 패널을 숨기거나 닫은 뒤에도 SDK 읽기가 발생하는 점을 추가로 확인하여, 표시 객체의 소유권과 종료를 연결한다. 대기 중 보기 작업은 생략하되 수락한 원본 배치는 유지한다. 재연결 시 취소된 작업을 성공처럼 발행하는 경로도 막고 실제 썸네일 파일을 검사한다. 이 항목은 권한·읽기·표시 수명에 한정한 검증이며 전체 앱과 모든 객체의 검토 완료를 뜻하지 않는다.

## 게시 객체의 작업 연결과 화면 종료

게시 백엔드의 공급자 조회·작업 조회·오류 조회·대기 개수 조회는 네트워크나 작업자를
시작하지 않는다. 명시적인 게시 수락만 값형 작업을 직렬 큐에 넣는다. 기존의 여덟
작업 제한·256개 기록 제한·중복 키·확인이 필요한 게시 결과의 보존 계약을 유지한다.

네트워크 작업자의 실행 상태는 QObject 수명과 분리한다. 화면 종료는 닫힌 큐와
취소 요청만 반영하고, 전송의 반환을 기다리지 않는다. 작업자가 늦게 보내는 상태는
GUI 큐의 약한 소유 참조로 걸러낸다. 게시 완료를 반영한 뒤 결과 알림을 전달하고,
표시 알림이나 다음 계정 취소 결과로 이어지기 전에 소유자의 생존성을 확인한다.
중복 ID는 알림 전에 고정하고, 계정 변경은 계정별 호출 번호로 최신 의도를 유지한다.

검사는 가짜 전송기만 사용한다. 전송 정체 중 화면 종료, 알림 중 소유자 삭제,
연결 해제 중 재설정, 오래된 설정에 의한 연결 복원, 취소 콜백의 큐 재조회 등을
실행하여 수명·순서 계약을 확인한다. 게시 객체의 전체 메서드별 판정과 미검증
경계는 해당 회차의 `member-review.json`에 기록한다. 이는 다른 생성·캔버스·
플랫폼 객체의 전수 검토나 실제 OAuth·외부 플랫폼 게시 완료를 대신하지 않는다.

문서 열기 완료 시 프로젝트는 실제 캔버스 객체를 교체한다. 상단바 GUI 검사는
수락 직후 얻은 이전 포인터를 대기 중 계속 조회하지 않고, `documentOpened`를
확인한 뒤 현재 캔버스를 다시 얻는다. 이전 객체의 약한 참조가 해제되고 새 객체가
32×32 원본을 표시하는지 함께 검사한다. 기존 레이아웃·검색·선택·문서 상태의
검증과 대기 시간은 유지한다. 이는 비동기 완료와 교체된 객체의 수명을 따르는
검사 수정이며, 실제 캔버스 동작을 우회하거나 실패 조건을 제거하는 변경이 아니다.

## 생성 결과 저장과 다음 작업의 연결

엔진 반환 후 GUI에서 PNG 인코딩·이미지 읽기·영상 검사·파일 미러 스캔을
직접 수행하여 이벤트 처리가 지연되는 경로를 실제 저장 프로브로 재현했다.
네이티브·프로세스·원격 결과를 동일한 값형 저장 경계에 연결하고, 수락한 결과의
임시 원본 수명과 다음 작업의 시작 순서를 유지한다. 보기 숨김과 반복 조회는
파일 저장 취소나 문서 변경을 뜻하지 않는다.

저장 준비 취소와 실제 반영 이후의 결과를 구분하며, 오래된 진행·프로세스 종료
알림이 완료를 선점하지 못하게 한다. 드라이브와 작업 ID를 확인한 뒤 결과 필드만
병합한다. 알림 중 소유자 삭제로 발생하던 충돌은 별도의 실패 재현과 약한 소유
검사로 교정한다. JPEG 입력의 고급 처리가 PNG 목적지에 JPEG 바이트를 쓰던
코덱 문제도 실제 파일 헤더 검사로 재현하여 수정한다.

이 회차는 결과 저장·완료·수명에 대한 검증이다. 생성 컨트롤러의 모든 메서드,
캐시 이행·엔진 자체의 종료 대기와 앱 전체 의미 검토는 별도 잔여 범위이다.
세부 계약과 검사는 [GenerationPublication.md](GenerationPublication.md)를 따른다.

## 생성·캐시 작업자의 수명과 알림 관찰자

현재 생성·캐시 이행 작업자가 컨트롤러 자체를 참조하고, 소유자 종료가 작업 완료를
합류 대기하는 경로를 재현했다. 요청과 취소 상태·원본 수명을 값으로 유지하고
네이티브 실행을 별도 작업 경계로 연결한다. 미리보기 PNG는 작업자가 준비하며,
GUI에는 살아 있는 소유자의 현재 작업에 해당하는 표시 정보만 반영한다.

준비 완료 알림의 관찰자가 취소해도 파일 저장을 시작하는 오류와, 준비·전경 상태
알림 중 소유자를 삭제하면 실행을 계속하여 충돌하는 오류도 재현했다. 알림 이후의
소유자·현재 작업·취소 상태를 검사하며, 출력 파서와 프로세스 호출부까지 연결한다.
캐시 해제는 종료와 영상 시작 모두 같은 작업자 호출을 사용하고 완료·실패·취소를
구분한다. 실제 모델을 이용하지 않는 제어된 검사와 기존 생성·Society 검사를
함께 실행한다. 더 넓은 메서드와 전체 앱 종료 경계는 진행 중이며, 이 회차의 계약은
[NativeGenerationLifetime.md](NativeGenerationLifetime.md)에 기록한다.
## 외부 추론 프로세스 경계

GenerationProcess와 GenerationController의 프로세스 분기는 직접 소유한 네이티브 작업자와 순서가 보존된 GUI 전달로 연결한다. 어댑터·컨트롤러·파일 회수 검사를 별도로 수행한다. 전체 객체·메서드의 의미 검토가 끝났다는 뜻은 아니며, 나머지 미검토 경로는 계속 추적한다. [구현 경계](GenerationProcess.md)를 따른다.
## 최종 리팩터링

전체 소스 인벤토리와 등록된 전체 회귀 검사를 기준으로 누적 변경을 정리한다. 마지막 구조 변경과 보존 계약은 [Refactoring.md](Refactoring.md)에 기록한다. 물리 장치와 외부 서비스의 실제 운영 검증은 코드 리팩터링 완료와 구분한다.
