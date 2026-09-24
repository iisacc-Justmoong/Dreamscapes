# Dreamscapes 애플리케이션 수명

macOS에서 창 닫기와 앱 종료를 분리한다.

- 마지막 창을 닫아도 프로세스와 런타임은 유지한다. 창의 QML 상태도 유지한다.
- Dock에서 앱을 다시 선택하면 숨겨진 주 창을 다시 표시하고 활성화한다.
- `⌘Q`, 앱 메뉴의 Quit, macOS의 명시적 종료 요청은 기존 종료 경로를 사용한다.
- Windows, Linux, iOS, Android에서는 마지막 창 종료 정책을 변경하지 않는다.

`src/App/ApplicationLifetime.h`를 실제 앱의 QML 엔진 구성 시 적용한다. 엔진을 연결의 수명 소유자로 사용하므로 엔진 해제 후 창을 참조하지 않는다. Qt 6.8.3 Cocoa 플러그인은 Dock 재열기 시 이미 활성 상태인 경우에도 ApplicationActive를 전파한다.

검증: `cmake --build build --target DreamscapesApplicationLifetimeTests` 후 `ctest --test-dir build -R ApplicationLifetime --output-on-failure`. 실제 이벤트 루프에서 마지막 창 종료 후 생존, 재활성화 시 동일 창/상태 복원, 창 없는 상태에서 명시적 종료를 검증한다. 테스트는 `.app`이 아닌 일반 실행 파일이다.
