# 에이전트 질문의 네이티브 화면

Dreamscapes macOS 데스크톱은 iiMultimodalAgent 패키지의
`IIMULTIMODALAGENT_VIEWS_DIR/UserQuestionsSheet.lvui`를 빌드 시 C++23으로
생성해 iiGUI 표면에 결합한다. 화면과 문자열, 질문 초안은 모듈의 LVUI 선언과
C++ 모델을 사용한다. 앱은 기존 `QuestionInbox`에 모듈의 `NativeQuestionBridge`를
연결하고 모델을 생성된 뷰에 제공한다. 앱이 이 모듈들을 조합하며 SDK 모듈끼리의
새 의존성은 추가하지 않는다.

## 표시와 수명

표면은 질문이 있고 창이 보이며 `licenseWorkspaceVisible`이 참일 때만 요청한다.
라이선스 결과의 판정은 기존 계정·라이선스 경로가 담당한다. 닫힘과 다시 열림이
연속으로 요청되면 이전 표면이 실제로 정리된 뒤에 다음 표면을 만든다. 오래된
표면의 완료 이벤트는 새 표면에 영향을 주지 않는다. 사용자 선택, 취소, 창 가시성,
라이선스 상태 변경과 앱 종료에 반응하며 대기 중 폴링이나 상시 프레임 구동은
추가하지 않는다.

앱 종료 조정기는 iiGUI 호스트의 종료 완료까지 기다린다. 창이나 수신함이 없어지면
관찰과 화면 수명도 종료된다. 네이티브 호스트 생성·마운트가 실패하거나 적절한
창·라이선스 알림을 연결할 수 없으면, 앱은 모듈에 격리된 기존 QML 화면을 사용한다.
실패 시 자동 재시도를 반복하지 않는다. macOS 외 플랫폼도 아직 기존 QML 화면을
사용한다. 실제 플랫폼 호스트와 앱 내 표시가 검증되기 전에는 해당 플랫폼의 이전이
끝난 것으로 간주하지 않는다.

## 빌드와 검증

선택한 iiMultimodalAgent 패키지에는 LVUI 뷰와 C++ 브리지가 있어야 하고 LVUI,
iiGUI 및 iiSDKCore는 서로 호환되는 ABI로 빌드되어 있어야 한다. 같은 버전 번호의
Core 빌드라도 ABI가 다를 수 있다. 현재 Qt 6.8의 macOS 링크 구성은 최신 SDK에서
사라진 AGL 항목을 앱 로컬 CMake 호환 처리로 제거한다.

`tests/native-question-coordinator`는 이벤트 결합, 요청 교체·재진입, 오래된 종료
콜백, 호스트 실패와 비동기 종료를 가짜 표면으로 검사한다.
`tests/native-question-presenter`는 LVUI 생성 코드와 실제 브리지·iiGUI 링크를 빌드하고
표시하지 않는 offscreen 창에서 호스트 진입 조건과 종료 조정을 검사한다. 이 검사는
실제 창을 띄우거나 GPU 렌더링 결과를 확인하지 않는다. 모듈 자체의 입력·선택·전송
검사와 설치 패키지 소비자 검사는 iiMultimodalAgent의 `docs/NativeQuestionView.md`에
기록한다.
