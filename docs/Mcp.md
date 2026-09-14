# 로컬 MCP 생성 제어

데스크톱 POSIX 빌드는 iiLocalLLM 0.10.0을 통해 현재 Dreamscapes의 GenerationController를 MCP로 제어한다. `127.0.0.1`의 인증된 HTTP 서버이며 같은 기기의 로컬 모델과 저장소를 사용한다. Society가 동기화를 담당한다.

| 도구 | 동작 |
|---|---|
| `status` | 저장소·선택 모델·큐·추론 상태·최신 결과 주소 |
| `models(offset=0, limit=20)` | 로컬 모델 ID·이름·형식, 최대 100개씩 조회 |
| `jobs(offset=0, limit=20)` | 실제 작업 ID·상태·오류·결과를 최신 작업부터 조회 |
| `select_model(id)` | 현재 목록에 존재하는 모델을 선택한다 |
| `refresh_models` | 로컬 모델 목록을 갱신한다. 생성 중에는 기존 컨트롤러 규칙에 따라 변경하지 않는다 |
| `generate(prompt, aspect_ratio="1:1", count=1)` | 현재 선택 모델로 생성 큐에 넣고 `first_job_id`, `job_ids`, `accepted`를 반환한다 |
| `cancel(id)` | 해당 인스턴스가 소유한 대기/실행 작업의 취소를 요청한다 |

generate의 count는 1~1000이고 비율은 1:1, 4:3, 3:4, 16:9, 9:16이다. 반환 job_ids는 해당 제출의 생성 순서이며 jobs의 최신순과 다르다. accepted는 생성 완료가 아니다. jobs를 조회해 completed/failed/cancelled를 확인한다. 결과는 로컬 Society의 Generation History에 저장한다. 앱 재시작 시 기존 컨트롤러와 동일하게 큐를 복원하지 않는다.

입력 스키마와 실제 컨트롤러의 조건을 모두 검사한다. 컨트롤러 호출은 Qt 주 스레드에서 수행한다. 이미 시작된 요청의 시간 초과나 연결 단절은 생성 제출을 되돌리지 않는다. 즉시 재제출하기보다 jobs를 확인한다. 같은 인스턴스에 접속한 인증된 클라이언트들은 앱 상태를 공유한다.

앱은 `IILOCALLLM_APP_ENDPOINTS` 절대 경로 또는 Qt GenericDataLocation의 `iisacc/AgentEndpoints`에 현재 사용자만 읽을 수 있는 등록 파일과 토큰을 게시한다. `IILOCALLLM_DISABLE_APP_MCP=1`로 비활성화한다. 같은 OS 사용자라는 신뢰 경계이며 앱 서명 검증은 아니다. 등록 폴더는 에이전트 workspace와 분리한다. iiLocalLLMD/iillm-mcp는 자동 발견하지만 호스트 도구 권한은 별도로 검사한다. `agent.mcp.status`의 app_id는 `com.iisacc.dreamscapes`이다. 모바일·Windows의 listener는 구현하지 않았다.

`Dreamscapes.Mcp`는 실제 앱을 별도 로컬 컨테이너에서 시작하여 모델 선택, 스키마 거절, 생성, 결과 PNG 크기, 연속 제출 ID, 대기/실행 취소, 인증, 종료/비활성을 검사한다. 기존 `fixtures/generate.py`를 사용한 프로세스 프로토콜 검증이므로 실제 확산 모델의 성능·품질 증거와 구분한다. 테스트 로그와 PNG는 build의 `mcp-dreamscapes-*`에 남는다.
