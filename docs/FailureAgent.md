# 작업 실패 설명 에이전트

Dreamscapes는 실패한 이미지·비디오 생성과 결과 게시 단계에 iiMultimodalAgent를
연결한다. 원래 작업의 `state=failed`와 `error`는 변경하지 않는다. 실패 직전의
모델, 제출 설정, 마지막 유효 진행 상태, 원문 오류, 종료 코드, 작업 로그와
진단 경로를 캡처한다. 중간 파일은 진단 작업이 끝날 때까지 수명을 유지한다.

캔버스 열기, 미디어 파일·사진 보관함 저장, 소셜 게시의 비동기 작업 실패도
`explainOperationFailure`를 통해 같은 보고서를 제공한다. 일반적인 잘못된 입력과
사용자의 취소는 자동 분석 대상으로 삼지 않는다. 이 보고서는 생성 작업 목록에
추가하지 않으며 생성 상태나 오류를 변경하지 않는다. 게시가 일부만 성공했다면
완료된 단계 수와 조정 필요 여부를 근거에 포함하며 자동으로 재게시하지 않는다.

`FailureDiagnostics`의 C++23 근거 보고서는 즉시 반환한다. 일반 실행 파일인
`DreamscapesFailureAgent`는 별도 프로세스에서 `agent::Engine`을 실행한다.
미실행 상태에서는 언어 모델을 로드하지 않는다. 작업당 분석 상한은 기본 90초,
동시 분석은 1개, 대기열은 8개이다. 모델의 추론 단계는 60초로 제한하며 프로세스
전체가 응답하지 않으면 부모가 종료한다. 생성 대기열은 분석 완료를 기다리지 않는다.

외부 프로세스를 제공하지 않는 iOS·Android·WASM 빌드는 순수 C++ 진단 컴포넌트만
참조하고 근거 보고서를 반환한다. 데스크톱 전용 모델 런타임을 강제하지 않으며,
로컬 언어 모델 에이전트의 실제 실행 검증은 현재 macOS에 한정한다.

기본 모드는 `native-evidence`이다. 이는 실제 오류 근거를 분류하는 결정론적 분석이며
언어 모델 추론으로 표시하지 않는다. 로컬 채팅 모델 카탈로그가 있으면
`ServiceModel`을 통해 추가 설명을 생성한다. 기본 카탈로그는 선택된 Society
컨테이너의 `Models/.society-runtime/iiLocalLLM/Models`이다.
`IIMULTIMODALAGENT_MODELS_DIR`로 카탈로그를 지정하고,
`IIMULTIMODALAGENT_MODEL=model://...`로 특정 설치 모델을 선택할 수 있다.
미설치 모델을 자동 다운로드하거나 외부 서비스에 로그를 전송하지 않는다.

이미 보유한 GGUF 채팅 패키지는 `tools/provision_failure_agent.py --package <패키지>
--container <드라이브.societycontainer>`로 등록한다. 선언된 파일의 크기와 SHA-256을
검증한 후 같은 볼륨에서 하드 링크만 생성한다. 원본 가중치와 기존 패키지는
변경하지 않으며 다른 볼륨으로 가중치를 복사하는 우회 동작은 하지 않는다.

확인된 근거 보고서와 `modelExplanation`을 분리한다. 모델 설명은 검증이 필요한
해석으로 표시하며 원문 오류를 덮어쓰지 않는다. 모델 부재, 로딩 오류, 분석 시간
초과, 프로세스 오류에는 원래 근거를 유지하고 분석 불가 사유를 반환한다.
재시도, 캔버스 변경, 편집 이력 생성, 파일 복구, 게시 실행 권한은 제공하지 않는다.
로그와 프롬프트를 지시문으로 실행하지 않으며 도구 레지스트리는 비어 있다.

QML은 `latestFailure`, `failureAnalysis(jobId)`, `failureExplained(jobId, report)`를
사용한다. `Failure details` 버튼과 스크롤 가능한 LVRS 모달은 전체 설명을 표시한다.
기존 짧은 오류 표시는 유지한다. MCP의 `failure_analysis`와 `jobs.failureAnalysis`도
같은 결과를 제공한다. 보고서는 앱 데이터의 `FailureReports/.<job-id>.json`에 저장하고
에이전트 실행 기록은 그 아래 `.sessions`에 저장한다. 이는 iiFileProvider가 문서
미리보기를 생성하지 않는 내부 파일 경계이다. 저장 실패도 별도 필드로 알린다.
테스트나 관리 환경은 `DREAMSCAPES_FAILURE_REPORTS_DIR`로 별도 보고서 저장소를
지정할 수 있다. 테스트는 사용자 앱 데이터와 분리된 `build/` 하위 경로를 사용한다.

CLI 최소 예제는 제품 루트에서 `build/DreamscapesFailureAgent <
example/failure-context.json`으로 실행한다. `install.py`는 canonical 앱의 서명과
파일 명세를 검사하고 ZIP 백업 및 외부 스테이지 검증 후 macOS 앱을 원자적으로
교체한다. 기존 실행 프로세스와 버전은 유지한다. 실제 앱 실행 검증은 설치 후
별도로 수행하며, 파일 교체만으로 열린 창이 새 코드를 사용한다고 보고하지 않는다.
설치기의 파일 명세·버전 보존·원자 교체 검사는 `tests/test_install_app.py`이다.
진단 완료 알림은 원래 작업에만 적용하며 현재 생성 작업의 오류·추론 상태를 바꾸지 않는다.
진행 단계 스냅샷은 작업별 임시 진단 상태로 유지한다. 백그라운드 전환 등 화면
상태를 작업 진행으로 기록하거나, 이전 작업의 화면 상태를 실패 원인에 섞지 않는다.

검증: SDK `iiMultimodalAgent.failure_report`, 앱 `Dreamscapes.FailureAgent`,
`failedGenerationPreservesErrorAndReturnsAgentReport`, MCP 도구 계약 및
오프스크린 GUI에서 보고서 접근과 작은 창의 스크롤을 검사한다. 실제 모델 추론의
검증 여부는 실행 결과 보고서에 별도로 기록한다.

시작 검사에서는 게시 플랫폼의 고정 개수 대신 필수 플랫폼 ID가 포함되어 있는지
확인한다. 설치된 SDK가 추가 플랫폼을 제공해도 기존 필수 기능 검사는 유지한다.
