# 네이티브 실패 상세 뷰 계약

생성 실패 상세는 `NativeFailureDetails.lvui`와 `FailureDetailsModel`을 사용한다.
모델은 제공된 `details` 또는 `rawError`, 모델 미가용 사유, 에이전트 오류, 저장 오류,
보고서 경로를 표시한다. 없는 필드를 추측하지 않으며 표시와 복사는 생성 작업이나
보고서 파일을 변경하지 않는다.

`NativeFailurePresenter`와 `NativeFailureDetailsBridge`는 Qt 객체가 아닌 C++ 객체다.
실제 네이티브 생성 큐의 실패와 복구 정보가 있는 취소를 소유 스레드에서 뷰모델로
전달한다. 실패는 iiMultimodalAgent `FailureDiagnostics`의 근거 보고서를 사용하고,
취소는 별도 제목과 복구 정보로 표시한다. 작업 상태나 원본 오류는 변경하지 않는다.
생성 큐의 완료 이벤트보다 먼저 분리된 워커 오류, 로그와 확인된 종료 정보를 게시한다.
해당 정보가 없는 공급자의 종료 코드와 시간 초과 여부는 확인되지 않았다고 표시한다.

실패 뷰는 네이티브 검토 작업 공간에 장착되며 LVUI 모달과 읽기 전용 텍스트 뷰를
사용한다. `FailureDetailsModel`은 원문과 문서 리비전, 열림과 오류 상태를 소유한다.
텍스트 선택은 원문을 수정하지 않으며 전체 보고서 복사는 기존 선택을 바꾸지 않는다.
기본 클립보드는 iiGUI를 사용하고, 쓰기와 텍스트 갱신 실패는 화면 상태로 표시한다.
보고서와 위젯 상태는 소유 스레드에서 갱신하며 갱신 알림 중의 재진입은 거절한다.
복사할 때 보고서와 클립보드 함수의 스냅샷을 사용한다.

네이티브 문서와 iiGUI 기본 클립보드의 한도는 UTF-8 기준 64 MiB이며 NUL은 거절한다.
텍스트와 문단 한도 안에서는 실제 폭과 글꼴로 자동 줄바꿈을 시도한다. 배치가 줄바꿈
한도를 넘으면 같은 모달의 문서 기반 `CodeEditor`로 전환하며 원문을 자르지 않는다.
그 경로는 아직 자동 줄바꿈하지 않는다. 선택 범위 복사에는 호환되는 LVUI와 iiGUI가
필요하다.

현재 연결된 진단은 결정론적인 근거 분석이다. 구 Qt 에이전트의 로컬 언어 모델을 통한
추가 해석, 비동기 완료 알림 및 보고서 영속화는 이 네이티브 경로에 연결되지 않았다.
나머지 옛 Qt 호환 소스와 다른 화면의 기능 이전 여부는 별도로 확인해야 한다.

`Dreamscapes.NativeFailureDiagnostics`는 분류, 종료 상태, 긴 UTF-8 원문과 취소를
검사한다. `Dreamscapes.NativeFailureBridge`는 네이티브 모델 연결, 복사, 스레드 경계와
취소를 검사한다. `Dreamscapes.NativeGenerationShell`은 실제 셸의 생성 큐에서 실패
상세까지의 연결을 합성 공급자로 검사한다. 이들은 실제 모델 추론을 수행하지 않는다.
`Dreamscapes.NativeFailureDetails`와 `Dreamscapes.NativeFailureRendererBoundary`는 뷰
계약과 링크 경계를 검사하며, macOS 클립보드 검사는 별도의 페이스트보드를 사용한다.
