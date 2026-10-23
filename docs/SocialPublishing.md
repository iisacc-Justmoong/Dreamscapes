# Social publishing backend

Dreamscapes owns a `SocialPublishingController` backend at
`mainWindow.socialPublishing` / object name `socialPublishingController`. It is
created on every application route, including Home and Editor, without starting
network traffic or a worker at startup. The editor now opens the separate
[publishing workspace](PublishingWorkspace.md); entering that view does not
submit a backend job or automatically post on generation completion.

Provider protocols live in the Qt-free C++23 SDK
[`iiSocialPublish`](../../../SDK/iiSocialPublish/README.md). The product's QObject
adapter only translates drafts/status, owns native account grants and delivers
worker completion to the GUI thread. It never uses Society app-to-app IPC or
stores a second copy of generated media.

## Native account setup and invocation

An authenticated account/session service configures each destination through
the **C++-only** `configureAccount(accountId, platform, Account)` method. Supply
the OAuth user token, granted scopes, expiry, explicit board/Page/author or
instance, and a supported API version where required. Tokens are not exposed
through QML properties, arguments, jobs or signals. Calling `disconnectAccount`
cancels that account's queued/active work and removes its in-memory grant.

The existing iisacc.com account-linking ledger is not a publishing token API.
Linking an account/profile does not establish publishing scopes, app approval
or a valid grant. This implementation does not extract encrypted server tokens,
embed OAuth client secrets in the app, or pretend the current app is already
authorized to publish. Account OAuth registration/consent and encrypted grant
custody belong to the account service; its trusted native integration can inject
grants through the method above. No real provider account is configured by
default.

After a native grant is configured, the callable backend surface is:

```qml
const jobId = window.socialPublishing.submit("my-mastodon-account", {
    text: "Created with Dreamscapes",
    idempotencyKey: "an-intent-id-retained-by-the-caller",
    media: [{
        kind: "image",
        localFile: "/absolute/export.png",
        mimeType: "image/png",
        altText: "Description of the artwork"
    }]
})
```

Supported draft keys are `text`, `title`, `link`, `tags`, `media`,
`coverImageUrl`, `visibility`, `contentWarning`, `sensitive`, `madeWithAi` and
`idempotencyKey`. Media entries accept `kind` (`image`/`video`), `localFile`
(absolute path or `file:` URL), `publicUrl`, `mimeType`, and `altText`. Unknown
keys and incorrect field types are rejected. Instagram and Threads require
public HTTPS media; a local path is never advertised as a public URL. Export
IISC to image/video first. The SDK's
[provider table](../../../SDK/iiSocialPublish/docs/Providers.md) lists exact
supported shapes and permission requirements.

`providers` exposes eleven capability records, including explicit unsupported
Pixiv/ArtStation records. `jobs` contains only job/account/provider IDs, state
and credential-free result details (`postId`, `url`, error, HTTP status,
numeric retry delay and `requiresReconciliation`). No token, draft or local
media path is included. `jobFinished(jobId, result)` runs on the GUI thread.

## Queue and failure policy

대상별 검토는 [PublishDestinationReview](PublishDestinationReview.md)에 기술한다.
`submitPlan`은 공통 원고에서 준비한 게시물 목록과 응답 의존성을 작업자에 전달한다.
토큰 없는 `accounts` 목록으로 대상과 계정 ID를 선택한다. 완료 결과에는 확인된
게시물별 `completed`와 다음 인덱스 `next`가 포함된다. 부분 성공 뒤 실패/취소는
`needs-reconciliation`으로 보존하며 원격 게시물을 자동 재생성하지 않는다.

지연 생성하는 C++23 작업자가 최대 여덟 개의 대기 작업을 직렬 실행하며,
기록은 256개로 제한한다. 업로드·폴링은 UI 스레드에서 실행하지 않는다.
`cancel(jobId)`는 진행 중인 HTTP·폴링에 취소를 요청하거나 아직 시작하지
않은 작업을 큐에서 제거한다. 컨트롤러 파괴는 큐를 닫고 취소를 요청한 뒤
즉시 반환한다. 이미 실행 중인 제공자 호출의 반환을 GUI에서 기다리지 않는다.
제공자가 이미 요청을 수락했을 수 있는 결과는 기존과 같이 확인이 필요한
상태로 유지하며 자동으로 재게시하지 않는다.

A repeated account + idempotency key + unchanged draft returns the existing job
ID. Reusing the key with different content is rejected. The key remains reserved
for that in-memory job, including ambiguous publication. `clearFinished()` removes
finished ordinary rows and their deduplication entries, but retains
`needs-reconciliation` rows. X is disabled until a trusted native grant explicitly
allows billable requests; this code does not purchase access or enroll in a plan.

The queue is in-memory. Closing the app cancels unfinished jobs; jobs and
deduplication are not persisted across restarts. It is not an unattended server
scheduler. A future account/backend persistence feature must preserve intent
and outcome separately from credentials before offering crash-safe scheduled
publishing.

## Build and verification

Build/install iiSocialPublish into `SDK/iiSocialPublish/build/install` (or an
installed SDK prefix). Dreamscapes resolves that package through
`IISOCIALPUBLISH_PREFIX`, the installed `~/.local/SDK/iiSocialPublish`, or the
workspace package fallback. Only the CMake package is linked; SDK sources are
not added directly to the product.
The installed SDK's dependency manifest and nlohmann/json MIT notice are embedded
as `:/licenses/iiSocialPublish/...` resources in the product and verified at GUI
startup.

```sh
cmake -S . -B build
cmake --build build --target DreamscapesSocialPublishingTests Dreamscapes DreamscapesGuiTests --parallel 4
ctest --test-dir build -R SocialPublishing --output-on-failure
```

The backend test injects a transport and verifies lazy startup, all ten
capabilities, worker-thread execution, GUI-thread signals, token-free snapshots,
idempotency conflicts, cancellation, disconnect, ambiguous outcomes, history
retention and safe shutdown. The SDK separately verifies provider payloads and
real loopback TLS/multipart streaming. These are local tests; no live social post,
OAuth consent, app-review approval or new paid access is claimed.

On 2026-10-18 the macOS Release app, GUI tests, backend test and QML lint target
built successfully. The four selected CTest groups (`SocialPublishing`,
`DependencyBoundary`, `EditorSidebar`, `EditorApplicationToolbar`) passed. Three
additional GUI scenarios (`mainStartupPerformance`, `homeDefersEditorUntilNeeded`,
`desktopHomeSearchesSocietyAndUpdates`) passed, including backend creation and
the embedded license check; the Qt harness reports five passes including setup
and cleanup. The canonical `build/bin/Dreamscapes.app` passed deep/strict code
signature verification. Existing unrelated QML warnings remain; this is not a
claim that the entire product regression suite is clean. This verification did
not replace or restart the user's running `/Applications` app.

Queued cancellation does not wait for an unrelated active upload. The native
controller removes an unstarted job under its queue mutex, then reports its
cancelled outcome and updates pending counts on the GUI thread outside that
mutex. Disconnecting an account retires all of its queued work the same way.
Active requests retain cooperative cancellation and provider/uncertain-response
reconciliation semantics. Completion observers may call the queue API reentrantly;
the transport test holds another account's request to verify this ordering without
making a real platform request.

## 작업 수명과 완료 알림 순서

작업자는 QObject를 소유하거나 참조해 실행하지 않는다. 전송기·큐·실행 중인
작업·계정 스냅샷의 공유 상태를 값으로 보유하며, 컨트롤러 파괴 후에도 실행 중인
전송이 반환할 때까지 이 수명을 유지한다. 닫힌 큐의 다음 작업은 실행하지 않는다.
피드백은 앱의 GUI 큐로 전달하고, 컨트롤러의 약한 참조가 유효할 때만 반영한다.
대기 상태의 작업자도 큐 종료 알림을 받아 빠져나온다. 네트워크 내부의 취소는
제공자·전송기의 협력적 취소 경계를 따른다. 이 계약은 화면 종료의 대기 제거이며,
프로세스 전체 종료나 원격 게시의 철회를 보장하는 계약이 아니다.

완료 시 행에 최종 결과를 먼저 반영한 다음 `jobFinished`를 발행하고, 소유자가
살아 있을 때 `changed`를 발행한다. 완료 관찰자가 새 요청을 시작하거나,
기록을 지우거나, 컨트롤러를 삭제할 수 있다. 이미 끝난 행에 늦은 완료를 다시
발행하지 않는다. 중복 요청의 기존 ID는 알림 전에 복사하여, 알림 중 기록이
교체되더라도 수락했던 ID를 반환한다.

계정 교체·연결 해제는 해당 계정의 아직 시작하지 않은 작업을 먼저 제거하고,
진행 중 작업에 취소를 요청한 다음 결과를 전달한다. 취소 콜백은 큐 뮤텍스 밖에서
실행하므로 콜백이 큐 API를 다시 조회할 수 있다. 결과 알림 후 소유자의 수명을
확인하고, 계정별 변경 번호를 대조하여 새 설정이나 연결 해제를 오래된 호출이
덮어쓰지 않게 한다. 변경 번호는 진행 중인 호출을 구별할 때만 유지하며,
관련 없는 다른 계정의 변경은 현재 계정 설정을 취소하지 않는다.

`Dreamscapes.SocialPublishing`은 기존 계약과 별도로 전송 정체 중 종료,
완료 알림 순서, 기록 교체 중 중복 ID, 완료·연결 해제·계정 교체 중 소유자 삭제,
최신 계정 설정·연결 해제·독립 계정 변경, 취소·해제·종료 콜백의 큐 재조회 사례를
검사한다. `--case <이름>`으로 각 사례를 독립 실행할 수 있다. 전송을 보류하는
검사는 GUI 종료가 끝난 뒤 전송을 풀며, 750ms의 탈출 장치를 사용해 기존의
종료 대기를 실패로 검출한다. 모든 제공자는 가짜 전송기이며 외부 게시를 수행하지
않는다. 힙 접근 계측 검사는 제품의 게시 어댑터와 해당 검사 실행 파일에 한정하며,
전체 앱의 메모리·스레드·프로세스 종료 검증과 구분한다.
