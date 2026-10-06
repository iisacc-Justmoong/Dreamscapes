<a id="single-app-build-contract"></a>

# 단일 앱 빌드 계약

제품은 [워크스페이스 단일 앱 정책](../../build-policy/README.md)를 사용합니다. `build/` 아래에 오직 정통 애플리케이션 번들이 생성됩니다. 테스트와 헬퍼는 일반 실행 파일이며, 런타임 배포 및 패키징은 원위치로 작동합니다. 정통 경로, 플랫폼 전환 및 검증 명령에 대한 정책을 참조하십시오.

선택적 `DREAMSCAPES_LOCAL_RUNTIME_PROBE` 빌드는 Documents/society-models-verification.json 에 실행 중인 생성 컨트롤러의 모델 메타데이터 및 저장 상태를 기록하기 위해 `--inspect-society-models` 를 허용합니다. 이 읽기 전용 검사 모드는 절대 생성을 대기열에 추가하거나 모델을 선택하거나 모델 다운로드를 요청하지 않습니다. observedAt 를 확인하여 구식 장치 보고서를 거부합니다.
