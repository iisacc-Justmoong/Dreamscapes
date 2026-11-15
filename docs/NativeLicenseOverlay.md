# 라이선스 잠금 화면의 네이티브 연결

Dreamscapes macOS 데스크톱은 iiLicenseManager 네이티브 패키지의
`SocietyLicenseGate.lvui`를 C++23으로 AOT 생성해 iiGUI 외부 표면에 결합한다.
기존 `SocietyAccount`가 소유한 확인 상태와 결과 코드가 화면으로 전달되며, 실제
진입 권한은 계속 기존 라이선스 판정이 통제한다. 토큰과 자격 증명은 화면 모델에
전달하지 않는다. Society 열기와 재확인 버튼도 기존 계정 동작을 호출한다.

## 상태와 수명

잠금이 필요하고 창이 보일 때만 표면을 생성한다. 창 표시와 라이선스 속성의 변경,
계정의 상태 알림, 언어 변경, 사용자 동작에만 반응한다. 확인 중 재확인은 차단하고,
Society를 열지 못하면 기존 안내 오류를 보여준다. 이미 열린 표면이 닫히기 전에
새 표면이 시작되지 않으며, 앱의 정상 종료는 호스트가 표면을 정리할 때까지 기다린다.

현재 Qt 앱의 격리된 QML 잠금 화면은 네이티브 화면 아래에 유지한다. 네이티브 화면
연결이 불가능하거나 표면 마운트가 실패하면 기존 잠금 화면이 그대로 작동한다.
다른 플랫폼은 아직 이 레거시 경로를 사용한다. 이는 각 플랫폼에서 새 호스트가
동작한다거나 사용자 앱에서 실제 화면 표시가 검증됐다는 뜻은 아니다.

## 빌드와 검사

제품은 iiLicenseManager의 LVUI 선언과 모델, LVUI 컴파일러, iiGUI 및 iiSDKCore를
조합한다. SDK 모듈 사이에 새로운 직접 의존성을 추가하지 않는다. 빌드 시 선택한
패키지들의 Core ABI가 일치해야 한다. 같은 Core 버전 번호만으로는 이를 보장하지
못한다. 오래된 전역 `CPATH`가 다른 SDK 헤더를 앞세우는 환경에서는 명시적으로
선택한 패키지의 헤더가 사용되도록 전역 경로를 제외해야 한다.

`tests/native-license-bridge`는 상태 투영, 버튼 동작, 확인 중 중복 요청 차단,
오류와 재진입·수명 종료를 검사한다. `tests/native-license-presenter`는 실제
LVUI 생성 코드와 계정 연결을 빌드하며, 표시하지 않는 offscreen 창에서 진입·종료를
검사한다. 별도의 `PaintView` 검사에서는 생성된 뷰의 버튼과 상태를 모델·가짜 페인트
장치에 연결한다. 이들은 앱 창이나 실제 GPU 렌더링을 실행하는 검사가 아니다.

각 디렉터리를 호환되는 SDK 패키지 경로로 CMake 구성한 뒤 다음과 같이 다시 실행한다.

```sh
cmake --build build/native-license-bridge
ctest --test-dir build/native-license-bridge --output-on-failure
cmake --build build/native-license-presenter
ctest --test-dir build/native-license-presenter --output-on-failure
```

2026-11-14의 제품 검사는 동일한 Core를 사용하는 계정·네트워크 패키지와 제품의
iiJson 타입을 조합해 빌드한 iiLicenseManager를 `build/` 아래에 별도 스테이징했다.
다른 iiJson 값 타입으로 빌드된 동적 네트워크 패키지를 제품의 기존 JSON API와
섞지 않았다. 이 스테이징은 시스템 설치를 갱신하지 않는다.
같은 날 Dreamscapes 앱 빌드와 번들 패키징이 통과했고, 번들의 라이선스 모듈과
Core UUID가 이 스테이징과 일치했다. 실제 앱 창은 띄우지 않았다.
