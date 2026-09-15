# 로컬 MCP 생성 제어

데스크톱 POSIX 빌드는 iiLocalLLM 0.36 계열을 통해 현재 Dreamscapes의 GenerationController를 MCP로 제어한다. SDK의 CMake 호환성 정책은 같은 minor 버전만 허용하므로 0.36.0 이상 패치 버전을 사용한다. `Dreamscapes.Mcp` 통합 검사로 설치된 SDK와 실제 앱의 연결을 검증한다. `127.0.0.1`의 인증된 HTTP 서버이며 같은 기기의 로컬 모델과 저장소를 사용한다. Society가 동기화를 담당한다.

| 도구 | 동작 |
|---|---|
| `status` | 저장소·선택 모델·큐·추론 상태·최신 결과 주소 |
| `models(offset=0, limit=20)` | 로컬 모델 ID·이름·형식, 최대 100개씩 조회 |
| `jobs(offset=0, limit=20)` | 실제 작업 ID·상태·오류·결과를 최신 작업부터 조회 |
| `select_model(id)` | 현재 목록에 존재하는 모델을 선택한다 |
| `refresh_models` | 로컬 모델 목록을 갱신한다. 생성 중에는 기존 컨트롤러 규칙에 따라 변경하지 않는다 |
| `generate(prompt, aspect_ratio="1:1", count=1)` | 현재 선택 모델로 생성 큐에 넣고 `first_job_id`, `job_ids`, `accepted`를 반환한다 |
| `cancel(id)` | 해당 인스턴스가 소유한 대기/실행 작업의 취소를 요청한다 |
| `iiLocalLLM.agent.permissions.get` | SDK가 제공하는 읽기 전용 도구로 현재 호스트 권한 정책을 조회한다 |

통합 검사는 앱 제어 도구 7개, AskUserQuestion과 SDK 권한 조회 도구의 이름, 읽기 전용 표시, 실제 정책 조회 응답을 확인한다.

generate의 count는 1~1000이고 비율은 1:1, 4:3, 3:4, 16:9, 9:16이다. 반환 job_ids는 해당 제출의 생성 순서이며 jobs의 최신순과 다르다. accepted는 생성 완료가 아니다. jobs를 조회해 completed/failed/cancelled를 확인한다. 결과는 로컬 Society의 Generation History에 저장한다. 앱 재시작 시 기존 컨트롤러와 동일하게 큐를 복원하지 않는다.

입력 스키마와 실제 컨트롤러의 조건을 모두 검사한다. 컨트롤러 호출은 Qt 주 스레드에서 수행한다. 이미 시작된 요청의 시간 초과나 연결 단절은 생성 제출을 되돌리지 않는다. 즉시 재제출하기보다 jobs를 확인한다. 같은 인스턴스에 접속한 인증된 클라이언트들은 앱 상태를 공유한다.

앱은 `IILOCALLLM_APP_ENDPOINTS` 절대 경로 또는 Qt GenericDataLocation의 `iisacc/AgentEndpoints`에 현재 사용자만 읽을 수 있는 등록 파일과 토큰을 게시한다. `IILOCALLLM_DISABLE_APP_MCP=1`로 비활성화한다. 같은 OS 사용자라는 신뢰 경계이며 앱 서명 검증은 아니다. 등록 폴더는 에이전트 workspace와 분리한다. iiLocalLLMD/iillm-mcp는 자동 발견하지만 호스트 도구 권한은 별도로 검사한다. `agent.mcp.status`의 app_id는 `com.iisacc.dreamscapes`이다. 모바일·Windows의 listener는 구현하지 않았다.

`Dreamscapes.Mcp`는 실제 앱을 별도 로컬 컨테이너에서 시작하여 모델 선택, 스키마 거절, 생성, 결과 PNG 크기, 연속 제출 ID, 대기/실행 취소, 인증, 종료/비활성을 검사한다. 기존 `fixtures/generate.py`를 사용한 프로세스 프로토콜 검증이므로 실제 확산 모델의 성능·품질 증거와 구분한다. 테스트 로그와 PNG는 build의 `mcp-dreamscapes-*`에 남는다.

## 로컬 질문 화면

`AskUserQuestion`은 iiLocalLLM 0.36의 C++ QuestionInbox와 LVRS 화면으로 사용자에게 질문한다. 단일·다중 선택, 여러 줄 자유 입력·메모, 부분 답변, 건너뛰기, 거절을 제공한다. 다음 질문이 도착해도 현재 입력을 유지한다. 미리보기와 외부 텍스트는 PlainText로 표시하며 이미지 첨부·HTML/Markdown 렌더링·전체 에이전트 채팅 UI는 포함하지 않는다.

인증된 로컬 호출자가 질문을 제출하면 앱 주 스레드의 UI가 답변을 받는다. 도구 호출 스레드가 기다리는 동안 일반 앱 호출은 계속 처리한다. 외부 호출자의 `answers`·`annotations` 주입은 거절하며 UI 답변만 원래 질문과 함께 검증한다. 클라이언트 취소·단절, 서버의 60초 요청 기한, 앱 종료는 대기를 해제한다. 호스트 정책이 질문을 거절하면 화면도 열리지 않는다.

도구의 `_meta["iisacc/userInteraction"]`은 true이며 `experimental.iisacc/userQuestions`의 `responseChannel`은 `local-ui`이다. 답변 제출이나 전체 질문 목록은 MCP 도구로 공개하지 않는다. QML은 SDK의 `IILOCALLLM_QML_DIR`에서 빌드 시 앱 리소스로 포함한다. 모바일/Windows에는 이 로컬 MCP 경로가 없으며 일반 화면은 질문 수신부 없이 동작한다.

앱 MCP 통합 검사는 공개 메타데이터·가져오기 표시, 외부 답변 위조 거절, 질문 대기 중 일반 변경 도구 실행, 취소 후 정상 호출을 검사한다. 질문 선택·한글 입력·메모·대기열 포커스와 Escape는 SDK의 LVRS UI 검사 대상이다. 실제 모델 및 설치 앱 검증 기록은 별도로 남긴다.

2026-09-15 최종 SDK 조합으로 macOS 전체 CTest 10/10(136.68초)을 통과했다. 새 패키지의 서명·번들 내부 의존성과 실제 0.36 라이브러리 로딩을 검사한 뒤, Qwen3 8B가 만든 질문에 에이전트가 네이티브 앱 화면으로 입력한 임의 코드를 모델이 2턴·99토큰으로 정확히 반환했다. 코드는 최초 모델 입력에 없으며 도구 선택과 응답 형식은 검증 호스트가 제한했다. 첫 실행은 서버의 60초 기한으로 실패했고, 직접 UI 전송은 22.52초에 성공했으며 즉시 입력한 모델 재검사도 통과했다. 일반 iOS Release는 진단 기능을 끄고 iPad 설치 영수증과 실행 프로세스를 확인했다. 교차 저장소 증거는 `SDK/iiLocalLLM/build/question-ui-apps/`의 최종 검사·패키지·UI·iPad 보고서에 보존한다.
