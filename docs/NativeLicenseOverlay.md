# 라이선스 잠금 화면의 네이티브 연결

Dreamscapes는 iiLicenseManager 프레젠테이션 패키지의 `SocietyLicenseGate.lvui`를
C++23으로 AOT 생성해 iiGUI 앱 창에 올린다. 확인 상태와 결과 코드는
`LaunchLicense`가 소유한다. 실제 진입 권한은 이 판정이 통제하며, 토큰과 자격
증명의 전달 범위는 검증 서비스까지이다. 시트에는 Society 열기와 재시작 안내를 표시한다.

## 상태와 수명

런치 판정으로 잠금이 필요하면 시트 전용 콘텐츠 호스트를 채운다. 기존 작업 화면과
제목 표시줄의 위젯과 문서는 유지되고, 검정 48%의 LVUI 암막 위에서 시트가 입력을
받는다. 작업 화면으로 포인터·키보드·접근성 입력이 전달되는 것을 iiGUI가 차단한다.
편집 도구, 재생, 게시 작업의 허용 여부에는 기존 라이선스 조건을 사용한다.
잠금이 해제되면 시트의 콘텐츠와 모델 구독을 해제한다.

## 빌드와 검사

제품은 iiLicenseManager의 LVUI 선언과 모델, LVUI 컴파일러, iiGUI 및 iiSDKCore를
조합한다. SDK 모듈 사이에 새로운 직접 의존성을 추가하지 않는다. 빌드 시 선택한
패키지들의 Core ABI가 일치해야 한다. 같은 Core 버전 번호만으로는 이를 보장하지
못한다. 오래된 전역 `CPATH`가 다른 SDK 헤더를 앞세우는 환경에서는 명시적으로
선택한 패키지의 헤더가 사용되도록 전역 경로를 제외해야 한다.

`Dreamscapes.NativeViewApp`은 판정 전후에 편집기 위젯과 문서가 유지되는지, 창
전체에 암막이 한 번 그려지는지, 입력 차단과 복원이 작동하는지 검사한다.

이전 외부 표면의 `tests/native-license-bridge`는 상태 투영, 버튼 동작, 확인 중 중복 요청 차단,
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
