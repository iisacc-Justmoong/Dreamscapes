# Dreamscapes account and licensing

The native Dreamscapes app temporarily defaults to
`DREAMSCAPES_ENABLE_LICENSE_VERIFICATION=OFF`. In this mode it does not start
a Society license check, display the license gate, or claim a verified grant.
Configure `build/` with `-DDREAMSCAPES_ENABLE_LICENSE_VERIFICATION=ON` and
rebuild and reinstall to restore the policy described below. Running processes
must be restarted to use a changed build.

When enabled, a signed, device-bound grant is checked in the background after the workspace becomes visible and remains
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
invalid responses and an unavailable Society fail closed. Each application run
makes one license decision. After a denial or unavailable result, restart the
application to request a new decision; opening Society does not alter the result
of an already completed run. Sign-out is performed in
Society and affects its account rather than a separate Dreamscapes session.

시작 시 작업 화면을 먼저 표시하고 이벤트 루프에 Society 라이선스의 비동기 확인을 예약한다.
확인 중이라는 사실은 유효한 라이선스의 증거가 아니며, 생성 요청과 비시각적
호출의 기존 권한 검사는 계속 유효하다. 검증 결과가 명시적 거부 또는 확인
불가이면 iiLicenseManager의 안내 화면을 표시한다. 이 실행의 첫 확인이
끝나면 성공 여부와 상태를 메모리에 고정하고 자동 확인, 포커스 변경 및
명령으로 인한 재확인을 중단한다. Society에 로그인한 뒤 다시 확인하려면
앱을 재시작한다. 성공한 결정은 대몬 연결 해제나 토큰 만료 이후에도 앱을
종료할 때까지 유지되며 디스크에 영속화하지 않는다.

The macOS and Windows source configurations select iiLicenseManager's LVUI license gate
and its independent `iiLicenseManager::Presentation` model. Dreamscapes
provides the existing account decision and the Society-open command. It does
not provide a retry command because a completed decision is fixed until the
next launch; the LVUI retry control is therefore disabled and explains that
the app must be restarted. If the external native host cannot be mounted, the
isolated legacy QML view remains as a fallback. That view is embedded from
`DREAMSCAPES_LICENSEMANAGER_VIEWS_DIR` at build time, first looking under
`src/Legacy/qml` or the installed `share/iiLicenseManager/legacy/qml` directory.
The Windows presenter is source-level integration; its product build and
window/input behavior have not been verified on Windows. `Dreamscapes.LicenseEntry` covers checking, denial and
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

The independent presentation package, generated LVUI view and Dreamscapes
bridge were also checked with a fake paint device and an offscreen presenter.
Those tests do not start the installed application or establish production
license service or visible-window behavior.

On 2026-11-15 the macOS Dreamscapes target and its app bundle built with the
independent presentation package. The global `CPATH` selected an older
iiLocalDiffusion header ahead of the configured package, so the build was run
with global C/C++ include-path variables removed and the affected application
object was recompiled. The final executable refers to the presentation library
and no longer directly refers to the old license-client library. This was a
build-tree artifact, not an installation or an application launch.
A second work session also built the same app bundle during the final rebuild;
after that build finished, the executable and bundled library were inspected:
the executable refers to the presentation library, the C API symbols are
present, and a strict deep code-signature verification passed. This inspection
does not establish visible-window or license-service behavior. The subsequently
added C model-lease API was verified in a separate SDK staging package; this
bundle has not been rebuilt to include that later C API revision.
