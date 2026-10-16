# File 패널

실제 에디터의 File 툴은 다음 세 Figma 상태를 그대로 대응한다. 공통 도구의 편집용 입력과 작업 버튼은 이 패널에서 생성하지 않는다.

| 상태 | Figma | 고유 바인딩 | 구성 |
| --- | --- | ---: | --- |
| Information | [1000:72907](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=1000-72907) | 108 | 파일, 생성 파라미터, 샘플링, 모델, LoRA, 원본 메타데이터, 편집 이력, 저작자, 기기, 링크 |
| Contents | [1000:72178](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=1000-72178) | 94 | 캔버스, 생성 설정, 아트보드, 레이어, 자산, 타임라인, sparse bitmap, 비디오, 오디오 |
| Storage | [1000:71361](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=1000-71361) | 26 | 가용성, 동기화, 저장 리비전, 파일 연결·위치·버전, 미리보기, 오류 |

`EditorFileDefinitions.js`는 Figma의 228개 고유 키와 레이블만 포함한다. 예시 파일·계정·경로·파라미터 값은 포함하지 않는다. `EditorCanvas::fileInformation`은 iiSharedCanvas의 const Document, 생성 메타데이터, iiFileProvider의 영구 저작자 스냅샷과 FileSystem의 Alias에서 문자열 값만 산출한다. `EditorProject::fileInformation`은 실제 modified, canvasCount, currentIndex와 프로젝트 오류를 반영한다. 생성 기록의 PNG 형식과 기존 IISC 편집 저장 경로는 그대로 유지한다.

`EditorFilePanel.qml`은 LVRS IconSegmentedControl·IconButton·Label과 기존 수동 PanelRow로 구성한다. 432px 패널 기준 내부 너비는 398px, 레이블 152px, 값 234px, 열 간격 12px이다. 상단 세그먼트와 현재 상태 아이콘은 고정하며 본문만 스크롤한다. 모바일에서는 LVRS Large Sheet 내부에 같은 패널을 채운다. 좁은 도크·시트에서는 레이블 폭을 줄이고 값 영역을 줄바꿈한다. 원본 SVG 세 개를 Assets/File에 보존하며 런타임 Figma URL을 사용하지 않는다.

상단 세그먼트만 상태를 전환한다. `fileViewId`는 도구 시트가 소유하여 패널 재생성과 다른 툴 왕복 후에도 유지한다. 전환은 tool values, document revision, authorship, undo 기록과 저장 파일을 변경하지 않는다. 문서·프레임·아트보드·페이지 표시·specification·파일 연결·오류 변경 신호는 표시 모델을 갱신한다. 문서를 새로 만들거나 열어 currentCanvas가 교체되어도 프로젝트 바인딩을 다시 평가한다.

값의 의미는 다음과 같다.

- 기록이 없는 필드는 `Not recorded`, 빈 컬렉션은 `None recorded`, 문서·파일이 없는 경우는 `Not available`로 표시한다. 실제 0과 false는 각각 0과 No로 보존한다.
- 생성 시드와 리비전은 C++에서 십진 문자열로 변환하여 uint64 값을 QML double로 손실하지 않는다.
- 여러 샘플링 패스·모델·LoRA·아트보드·자산·저작자·링크·트랙·클립은 등록 순서의 번호와 함께 모두 표시한다. 저작자 필드는 인증 상태가 아닌 파일에 기록된 프로필 스냅샷이다. 런타임 인증 토큰·로그인 세션은 이 표시 모델에 포함하지 않는다.
- 원본 프롬프트·JSON·메타데이터·URL은 PlainText로 표시하며 줄바꿈한다. 네이티브 텍스트를 해석하거나 링크를 실행하지 않는다.
- 타임라인 초는 프레임 수와 분수 프레임률에서 산출한다. 오디오 길이는 interleaved PCM sample 개수 / sampleRate / channelCount이다. 비디오 out-frame은 exclusive, 레이어 frameRange는 inclusive라는 SDK 계약을 명시한다.
- creation specification이 파일에 기록되지 않은 경우 현재 크기·기본 PPI로 추정하지 않는다. 저장 Alias에 modified 등이 없는 경우도 동일하다. SHA-256 형태인 Alias.version에만 SHA-256 레이블을 적용한다.
- 작업 파일의 저장 리비전은 iiSharedCanvas 0.30.2의 DocumentFile::readRevision으로 읽는다. FileSystem이 제공한 로컬 가용성과 경로를 확인하고, path/version/exists/available별 결과를 캐시한다. 쓰기 소유자 DocumentFile::open을 호출하거나 SQLite 구현 스키마를 앱에 복제하지 않는다. 이 API는 저장 스키마와 리비전 조회이며 문서 전체의 유효성을 별도로 보증하지 않는다. 직렬화된 IISC 스냅샷에는 저장 리비전이 없으므로 Not applicable이다.

`tests/fixtures/EditorFileBindings.json`은 세 Figma 노드에서 추출한 독립적인 키 목록이다. EditorFileInfoTests는 이 228개 키 전체와 값 형식, 미기록·0·false, uint64 seed, 여러 저작자/모델/LoRA, 미디어 길이, 저장 리비전, 파일·문서 불변성, 갱신 신호를 검증한다. GuiTests::editorFileThreeViews는 실제 CanvasEditor의 도크 세 폭과 모바일 시트를 통해 세 버튼의 클릭, 항목 수·노드·아이콘 대응, 줄바꿈·스크롤·고정 헤더·상태 전환 불변성·문서 교체를 검증한다.

검증 명령:

```sh
cmake --build build --target Dreamscapes DreamscapesGuiTests DreamscapesEditorFileInfoTests Dreamscapes_qmllint -j 4
ctest --test-dir build -R '^Dreamscapes\.(EditorFileInfo|EditorFilePanel|EditorToolPanels|EditorCanvas|EditorProject|EditorTools)$' --output-on-failure
```
