# Working-file open command

`EditorProject.openDocumentSource(source, asCopy)` is an accepted asynchronous
command. A true return means accepted, not that the current canvas has changed.
`opening`, `documentOpened`, `documentOpenFailed` and `documentOpenCancelled`
describe its lifecycle. `cancelDocumentOpen` retires the intent immediately.
CanvasEditor forwards completion; Main changes its visible editor and generation
metadata only after successful native adoption. A failed/cancelled read preserves
the old document and its generation metadata.
Main supplies an explicit create/open intent and disables CanvasEditor's unused
initial 1024-square placeholder. The standalone component retains its default
blank initialization. This avoids allocating/binding an extra bitmap/canvas
before opening the requested file; blank-canvas commands still create explicitly.
The Home route regression checks this empty initial state explicitly: rejected
imports and failed asynchronous reads retain the same uninitialized canvas,
without dereferencing or manufacturing a placeholder document.
GUI open assertions wait for completion and then retrieve the current canvas;
the canvas captured before an asynchronous replacement is no longer an owner.

## Work and ownership

EditorDocumentOpen prepares detached values on a worker. Native `.iisc` reading,
legacy `.iiscp` decoding/artboard assembly, storage selection/mirror scanning and
the existing full-content Alias version calculation occur there. Legacy payload
I/O uses iiFileProvider's native C++ API. Native IISC reads and storage/Alias
values use the existing SDK FileSystem gateway; parsed documents remain native
value objects. Bitmap inputs
reuse original-image preparation, including its existing managed-wrapper storage
contract. A source access token coordinates nested SDK reads and version capture;
no QObject, timer or writable file binding crosses threads.

EditorDocumentOpenRequest is the GUI command owner. It keeps one running read and
one latest pending intent, owns cancellation and checks the captured project,
canvas, revision, file path and selected drive before adoption. Editing,
replacement, live strokes and changed drive identity retire a pending intent;
pure frame/zoom/selection navigation does not. The worker closure owns only input
values and its cancellation flag. Destroying the command/project does not join
the read. Cancelled or obsolete results cannot replace the current document.

Source metadata uses iiFileProvider's native change token, which includes native
identity/change time while normalizing its own presentation attributes. The data
fork and SQLite WAL/journal are checked before/after preparation and before
adoption. Reader shared-memory lock tables are excluded. A replaced source is
rejected even when its modification time is restored. This is bounded identity
I/O on the GUI owner, not another payload hash or directory scan.

PreparedStorage carries the already discovered mirror to the new canvas. Native
working documents retain their Alias and expected-version save checks. Copies
retain no writable source binding; legacy imports remain unsaved native projects
and never overwrite their `.iiscp`. A legacy active page maps to its first actual
artboard even when preceding pages contain several artboards. Whole-document
values are moved into the canvas, preserving the native document schema.

Synchronous EditorCanvas open/import helpers and EditorProject image-import
helpers are C++ utilities, absent from the QML metaobject API. GUI entry points
use this open command or EditorImageImportRequest. The direct QML wrapper, File
menu, file dialog, drag/drop and Home routes share the accepted/completed contract.

## Cancellation and shared transfers

Main retains the container ID of its accepted download, in addition to file key
and section. Cancelling a view may call FileDownload.cancel only while the
download is active and both key/container ID match that owned intent. An unrelated
shared transfer is preserved. A completion from a different drive cannot open a
same-key file from the newly selected drive. ImageOpenRequest continues to own its
own hydration requests separately.

## Verification boundaries

The source-access gate regression reproduced a 2024473-microsecond blocking open
before the change. The first native replay accepted the same command in 44
microseconds and delivered a timer event while preparation was pending. This
measures dispatch responsiveness, not total decode/open/render time.

An initial combined test run exceeded its integration deadlines. A live sample
of the isolated queue case placed the main thread in fixture creation, at
`DocumentFile::create` / SQLite transaction sync / `fcntl`, before the request
started. That isolated case subsequently passed. Lifecycle/queue fixtures use the
supported native IISC snapshot representation; the source-access and writable
binding regressions retain real SQLite working files. Product durability is
unchanged and the dispatch-time bound remains 250 milliseconds.

Tests cover current working-file binding and optimistic conflicts, copy
isolation, prepared mirror adoption without rescanning, bounded/latest requests,
cancellation/edit/replacement/drive invalidation, replaced-file identity,
reentrant intent ordering, non-joining destruction and legacy active-page
mapping. Mounted offscreen routes separately verify Home metadata and editor
adoption. Full build, QML checks and packaged startup remain separate gates.

Initial editor/QML construction, final native validation/binding/history,
implicit-still migration writes, initial render-snapshot copying and durable edits
still have GUI-owner costs. There is no zero-I/O or whole-application FPS claim.
The existing bitmap wrapper contract may publish individual originals before a
later cancellation; it is not a cross-file transaction. Native foreground input
testing remains separate while another UI automation is active.

## Home 이미지 열기와 저장소 등록 잠금

원본 접근 토큰은 최초 버전 캡처에서만 짧게 소유한다. 실제 페이로드 읽기는
SDK 디코더의 자체 접근 토큰을 사용하고, 준비 완료 전과 채택 전의 버전 검사는
계속 수행한다. 이미지의 편집용 iisc를 Society에 등록하는 동안에는 원본 토큰을
유지하지 않는다. 파일 수집 작업이 등록 임대를 가진 채 같은 원본의 프리뷰를
읽는 경우에도 서로 기다리지 않아야 한다.

`bitmapPublicationDoesNotHoldTheOriginalReadLock` 검사는 등록 임대를 잠시 점유한
상태에서 이미지 열기를 시작하고, 등록을 기다리는 동안 다른 읽기 작업이 원본에
접근할 수 있는지 확인한다. 임대를 해제한 뒤 문서 열기 완료와 원본 바이트 보존도
검사한다. Home의 실제 선택·Open in editor 경로는 GUI 회귀 검사로 별도 검증한다.
