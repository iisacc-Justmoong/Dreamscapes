# Dreamscapes 애플리케이션 수명

macOS에서 창 닫기와 앱 종료를 분리한다.

- 마지막 창을 닫아도 프로세스와 런타임은 유지한다. 창의 QML 상태도 유지한다.
- Dock에서 앱을 다시 선택하면 숨겨진 주 창을 다시 표시하고 활성화한다.
- `⌘Q`, 앱 메뉴의 Quit, macOS의 명시적 종료 요청은 기존 종료 경로를 사용한다.
- Windows, Linux, iOS, Android에서는 마지막 창 종료 정책을 변경하지 않는다.

## 모델 메모리 수명

iiLocalDiffusion의 resident 모델 원본은 생성 작업이 아니라 앱의 추론 런타임에
귀속된다. 생성 완료, 대기, 전경/배경 전환, UIKit 메모리 경고는 해제 사유가 아니다.
네이티브 컨트롤러 종료와 명시적 SDK 해제만 원본 풀을 정리한다. 데스크탑 모델
선택 변경도 실행 중인 준비 작업의 워커를 죽이지 않고 다음 준비 경계에 반영한다.
SDK는 오류로 실행 컨텍스트를 폐기해도 완전히 적재된 익명 모델 원본은 재사용한다.

OS의 익명 메모리 압축·스왑은 허용하며 앱이 모델 볼륨에 별도 스왑 파일을 만들지
않는다. 물리 RAM 고정, 스왑 위치 변경, 강제 종료 후 메모리 복원은 제공하지 않는다.
기존 데스크탑 강제 취소/멈춤 감지의 프로세스 종료도 런타임 종료에 해당한다.
이는 백그라운드 GPU 실행 권한과 별개이며, 실행 권한이 없을 때는 기존 연산 경계
정지/재개 정책을 유지한다.

`Dreamscapes.Generation`은 전경 준비 → 생성 → 후면 대기 → 전면 복귀 → 재생성에서
동일 워커 PID와 pipeline load 0회를 확인한다. iOS 메모리 경고 콜백은 로그만 남기며
모델 해제 API를 호출하지 않는다. 실제 iOS OS-kill/스왑 동작은 호스트 테스트로
검증했다고 주장하지 않는다.

`src/App/ApplicationLifetime.h`를 실제 앱의 QML 엔진 구성 시 적용한다. 엔진을 연결의 수명 소유자로 사용하므로 엔진 해제 후 창을 참조하지 않는다. Qt 6.8.3 Cocoa 플러그인은 Dock 재열기 시 이미 활성 상태인 경우에도 ApplicationActive를 전파한다.

검증: `cmake --build build --target DreamscapesApplicationLifetimeTests` 후 `ctest --test-dir build -R ApplicationLifetime --output-on-failure`. 실제 이벤트 루프에서 마지막 창 종료 후 생존, 재활성화 시 동일 창/상태 복원, 창 없는 상태에서 명시적 종료를 검증한다. 테스트는 `.app`이 아닌 일반 실행 파일이다.
