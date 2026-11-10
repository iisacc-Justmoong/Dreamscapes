# Dreamscapes account and licensing

A signed, device-bound grant is checked in the background after the workspace becomes visible and remains
valid for the lifetime of that application run. A later daemon disconnect or
token expiry does not relock an already authorized workspace; each new launch
must obtain and verify a new decision. The local broker is hosted by the Society
background service and does not require the Society window to be open.
If the default broker is unavailable, the native client starts the installed
license-only daemon without a window and retries the local connection. On macOS
it looks in the user's Applications folder and then `/Applications`; Linux uses
the standard binary locations. `SOCIETY_DAEMON_EXECUTABLE` selects an absolute
alternative. Starting a daemon never substitutes for verifying its signed reply.

Society owns the iisacc.com account, browser sign-in and online license lookup.
Dreamscapes requests its license decision from Society through iiAccountManager
Native `SocietySession` over the same-user local channel. Dreamscapes does not
receive passwords, online cookies or bearer credentials. It verifies the signed
response, device, product and expiry before granting access; signed denial,
invalid responses and an unavailable Society fail closed. The account page
opens Society or requests a fresh local license check. Sign-out is performed in
Society and affects its account rather than a separate Dreamscapes session.

시작 시 작업 화면을 먼저 표시하고 이벤트 루프에 Society 라이선스의 비동기 확인을 예약한다.
확인 중이라는 사실은 유효한 라이선스의 증거가 아니며, 생성 요청과 비시각적
호출의 기존 권한 검사는 계속 유효하다. 검증 결과가 명시적 거부 또는 확인
불가이면 iiLicenseManager의 안내 화면에서 Society 열기와 재시도를 제공한다.
재확인이나 라이선스 상태 변화는 이미 구성된 작업 화면과 초안 상태를 보존한다.
승인 후에는 현재 실행 동안 해당 결정을 유지한다.

The QML view is embedded from `DREAMSCAPES_LICENSEMANAGER_VIEWS_DIR` at build
time. It defaults to the workspace iiLicenseManager source or an installed SDK
view. `Dreamscapes.LicenseEntry` covers the view's checking, denial, retry and
grant states. `Dreamscapes.LicenseEntrySociety`
checks the actual account-to-window binding when Society is unavailable and
rejects a tampered response before a signed grant through a local fixture broker,
then verifies that the authorized workspace remains open for the current run.
The GUI entry fixtures use temporary containers rather than the user's shared
storage. A separate signed-token fixture also checks the account policy.

The native channel currently supports macOS and Linux; Windows and mobile need
platform-specific local transport before this flow can work there. A previously
saved direct-browser Dreamscapes credential is no longer read by this account
model, but is not automatically deleted by this change. Network revocation and
durable removal of Society's own session remain Society's responsibility.

The `Dreamscapes.SocietyAccount` fixture checks a signed local grant, denial,
tampered token, unavailable service and retention after token expiry. A registered URL-handler fixture
checks the Society-open command without launching the user's application. This
is not evidence of a production account, installed app, successful paid
activation, shutdown responsiveness or cross-platform operation.
