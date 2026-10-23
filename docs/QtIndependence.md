# 비GUI SDK 채택 경계

2026-10-22 `NativeGenerationWork`를 Qt가 없는 `DreamscapesNativeGeneration` 타깃으로 분리했다. 요청/결과와 취소는 기존 iiLocalDiffusion 표준 계약을 사용하고 추론 함수를 주입한다. PNG 인코딩은 iiImageCodec 0.1.0, 원자적 파일 publication은 기존 iiFileProviderNative가 담당한다.

worker의 경로는 std::filesystem::path, 프리뷰 이름은 std::string이다. GenerationExecution에서 UI 경로를 변환하고 GenerationProgress에서 UI 이미지 URL을 만드는 순간에만 Qt 값을 만든다. 표준 worker에는 QString, QImage, QFileInfo, QIODevice 및 Qt 파일 장치를 포함·링크하지 않는다. QtConcurrent 제출이나 GUI QObject 수명까지 제거한 변경은 아니다.

`tests/tst_nativegeneration.cpp`는 실제 RGB/PNG 픽셀, 중복/과거 sequence 거절, 잘못된 픽셀 크기 거절, 취소된 provider 미호출, provider 예외와 소유 lease 소멸을 검증한다. `tests/native-generation/CMakeLists.txt`는 실제 worker 소스를 독립 빌드하여 Qt 없는 소비도 검증한다. 주 앱은 기존의 단일 canonical 앱 경로와 버전 0.1.1을 유지한다.

SDK의 전체 대체 기능/소비자 이행 구분은 Workspace의 `SDK/docs/QtIndependence.md`에 있다. 이번에 추가한 XML·SQLite·WebSocket API가 Dreamscapes의 모든 서비스·문서 코드를 자동 변환하지 않는다. 새 PNG 모듈에는 이미지 처리/메타데이터/벡터/전체 포맷 동등성이 없다. 워터마크와 최종 문서 publication 경로는 기존 계약을 유지한다.

관련 SDK 테스트, 설치 소비자, 로딩 기록과 앱 회귀 결과는 `SDK/build/qt-independence-20261022/`에 저장한다. 앱의 source/build 증거와 설치·재시작 증거를 구분하며, 자동화된 fixture 검증을 실제 모델/물리 GPU/외부 서비스/전체 GUI 성능 검증으로 확대 해석하지 않는다.
