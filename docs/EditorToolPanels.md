<a id="editor-tool-bottom-sheets"></a>

# 편집기 도구 하단 시트

Audio, Canvas, Generative, Select, Retouch, Fill and Brush share the current [compact state panels](EditorCompactPanels.md) on desktop and mobile, from Figma `772:80714`. Their historical unified form specifications below remain reference material only. All original stable parameter IDs and drafts are preserved; unsupported Figma controls remain visible and disabled.

Elements는 데스크톱과 모바일 모두 [10개 Figma 상태별 전용 패널](EditorElements.md)을 사용한다. Shape·Selection·Fill·Stroke의 현재 구성과 실제 문서 선택 계약은 해당 문서에 기록한다. 아래 내용은 나머지 공통 도구와 기존 모바일 시트의 상세 계약이다.

모든 도구 패널의 버튼 타입은 동작 중 고정이다. Hover·Pressed·클릭·현재 선택 여부에 따라 Accent/Primary 또는 다른 타입으로 바꾸지 않는다. 선택 상태는 같은 타입의 pressed 표면을 유지한다. 공통 구현 및 검증 계약은 [EditorButtonStates.md](EditorButtonStates.md)에 기록한다.

2026-10-03에 추가한 19개 데스크톱 오른쪽 패널과 상태 검증은 [EditorDesktopPanels.md](EditorDesktopPanels.md)에 설명되어 있다.

출처: [Dreamscapes / 편집자 / 통합 도구 세부 정보 패널](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=165-3042).

모바일 도구 모음은 선택한 도구에 대해 하나의 LVRS `Sheet`를 연다. 현재 앱은 18개 도구를 제공하며 Asset은 툴바와 패널 카탈로그에서 제거되었다. Camera/Photo 다음에는 File이 배치된다. 시트는 아래에서 올라오며 긴 패널은 세로로 스크롤한다. 닫기·Escape·배경 누르기·네이티브 손잡이를 아래로 끌기로 시트를 닫는다. Escape는 편집기를 떠나기 전에 패널부터 닫는다. Main이 시스템/키보드 인셋을 소유하며, 시트는 인셋을 두 번 적용하지 않고 편집기의 남은 영역을 사용한다. 데스크톱 레이아웃으로 바꾸거나 편집기를 떠나면 도구 시트와 색상 선택기를 모두 닫는다.

<a id="component-mapping"></a>

## 구성 요소 매핑

기존 시각적 컨트롤인 LVRS, `Sheet`, `Label`, `LabelButton`, `IconButton`, `InputField`, `ToggleSwitch`, `Slider`, `ListItem`, `ColorPickerButton`, `ColorPicker` 만 사용됩니다. 로컬 QML 파일은 레이아웃과 상태를 구성하며, 사용자 정의 시각적 컨트롤, 페인트 구현, 또는 컴포넌트 라이브러리를 도입하지 않습니다.

`EditorToolDefinitions.js` 는 검토된 필드 카탈로그로, Figma 노드 ID, 안정적인 필드 ID, 초기 값, 옵션, 및 수치 범위를 포함합니다. `EditorToolControl.qml` 는 필드를 렌더링하며, `EditorToolPanel.qml` 는 행과 작업을 배열하고, `EditorToolSheet.qml` 는 선택된 도구와 그 초안을 관리합니다. `CanvasEditor` 는 도구바 선택을 시트에 연결합니다. Figma 의 컴팩트 2열 슬라이더/스위치 행은 340px 의 콘텐츠 너비 아래 1열로 바뀝니다. 옵션 버튼이 감싸지며, 차원은 2 로 표시된 입력을 유지합니다. 네이티브 시트 Figma 패널에 드래그 핸들과 닫기 애퍼던스를 추가합니다.

데스크톱의 공통 `EditorToolPanel`은 해당 도구에 공통 패널이 필요한 경우에만 Loader로 생성한다. Color, Adjust (`auto-enhance`), Mask (`masking`), Effects의 전용 패널이 활성인 동안에는 공통 패널을 생성하지 않는다. Elements, Text, Layers의 전용 패널도 같은 생성 경계를 사용한다. 숨겨진 공통 입력이 전용 도구의 초기 값을 편집 신호로 전달해서는 안 된다.

|도구 모음|필드/작업|Figma 노드|
| --- | ---: | --- |
|요소| 18 | 196:3034 |
|텍스트| 16 | 196:3413 |
|카메라/사진| 16 | 196:3703 |
|파일| Information 108 / Contents 94 / Storage 26 | 1000:72907 / 1000:72178 / 1000:71361 |
|배경| 16 | 197:3649 |
|오디오 트랙| 16 | 197:3907 |
|캔버스| 20 | 197:4160 |
|생성| 31 | 197:4366 |
|레이어| 27 | 197:4816 |
|선택| 17 | 197:5165 |
|색상| 48 | 197:5507 |
|효과| 48 | 197:6429 |
|리터치| 23 | 197:7454 |
|채우기| 16 | 197:7857 |
|브러시| 27 | 197:8150 |
|자동 향상| 20 | 197:8764 |
|마스킹| 26 | 197:9090 |
|지우개| 14 | 197:9637 |

<a id="interaction-contract"></a>

## 상호작용 계약

각 도구는 편집기 수명 동안 독립적인 메모리 내 초안을 유지한다. 해제·다시 열기, 도구 전환, 크기 조절은 이를 유지한다. 공통 패널의 Reset은 현재 도구의 초기 값을 복원하며 현재 보정 세션의 원본 픽셀도 복원한다. 전용 Graphic 패널의 Reset은 [Graphic 입력 계약](EditorGraphicPanels.md)에 따라 현재 뷰의 초안을 복원한다. 옵션, 선택기, 스위치, 텍스트, 크기, 슬라이더 및 색상 컨트롤은 사용자 입력에 따라 초안을 갱신한다.

숫자·색상·크기 입력의 초기 값 바인딩과 편집 없는 포커스 이동은 `edited` 신호를 보내지 않는다. 포커스 손실에 따른 입력 확정은 사용자의 `textEdited` 이후에만 수행한다. Enter의 `accepted` 또는 명시적인 `commit()` 호출도 입력 확정 경로이다. 형식이 잘못되거나 범위 밖이거나 역방향인 입력은 전달하지 않고 이전 값의 표시로 복원한다. 수치 범위는 명시적인 애플리케이션 제약 조건이며 디자인의 예시 값 자체를 편집 의도로 해석하지 않는다. 깊이 범위는 두 끝점을 모두 보존한다. 색상은 LVRS 픽커 또는 6자리 16진수 필드를 통해 확정되며 픽커를 취소하면 이전 색상을 보존한다.

Elements와 Text의 숫자 입력에도 같은 계약을 적용한다. 정수로 반올림한 표시 문자열은 편집값이 아니다. 크기 `24.5×16.5 px`의 첫 축이 `25 px`로 표시되거나 13 px 글꼴의 `120%` 행간 `15.6 px`가 `16 px`로 표시되어도, 입력을 편집하지 않은 포커스 이동과 Color 전환은 원래 수치와 형상, 행간 프리셋을 보존한다.

도구 선택, 패널 재열기, 세부 화면 탐색은 문서 픽셀·revision·Undo 기록·Redo 분기를 보존한다. 사용자가 값을 편집하거나 작업을 명시적으로 실행하기 전에는 표시 기본값을 문서로 전달하지 않는다. Graphic 초안 입력과 Preview는 문서 픽셀을 바꾸지 않으며 실제 적용은 Apply를 통해 수행한다.

이 문서는 컨트롤 패널의 입력 계약을 설명한다. 작업 버튼 및 미리보기 행은 `CanvasEditor.toolActionRequested(toolId, fieldId, values)`를 방출한다. 네이티브 문서와 연결한 파일·레이어·브러시·기본 도형 동작은 [EditorNativeCanvas.md](EditorNativeCanvas.md)에 정의되어 있다. 실제 도구 실행, 미리보기, 입력 조건 및 포맷의 한계는 [EditorToolBehavior.md](EditorToolBehavior.md)에 정의되어 있다. 지원하지 않는 항목은 성공 안내 대신 비활성 상태와 사유를 제공한다. 패널 초안 자체는 디스크에 영구 저장하지 않으며 기존 홈/결과 화면의 선택과 생성 초안을 유지한다.

<a id="verification"></a>

## 검증

`build/`에서만 빌드:

```sh
cmake -S . -B build
cmake --build build --target DreamscapesGuiTests -j 6
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_DISABLE_DISK_CACHE=1 \
  build/bin/DreamscapesGuiTests editorToolSheets mobileEditorToolbarSlidesAndSelects canvasRoutesPreserveSelectionAndDraft
ctest --test-dir build -R '^Dreamscapes.EditorColorPassiveSelection$' --output-on-failure
```

`editorToolControlsDoNotCommitUneditedFocus`는 Slider, Dimensions, Color 입력에 실제 포커스를 주었다가 이동한다. 초기 표시와 값 바인딩 갱신만으로 편집 신호가 발생하지 않는지 검사하고, 실제 키 입력 뒤 포커스를 이동하면 올바른 값이 정확히 한 번 전달되는지도 검사한다.

`editorColorToolSelectionDoesNotEditDocument`는 데스크톱과 모바일 레이아웃의 여섯 행을 검사한다. `color-selection-desktop`과 `color-selection-mobile`은 실제 48×32 bitmap과 Canvas PPI 입력을 사용한다. `color-fractional-elements-desktop`과 `color-fractional-elements-mobile`은 실제 backend로 생성한 `24.5×16.5 px` native vector의 `25 px` 표시 입력에 포커스를 주고 편집 없이 Color로 전환한다. `color-fractional-text-desktop`과 `color-fractional-text-mobile`은 실제 두 줄 native text에 13 px 글꼴과 `120%` 행간을 적용하고 `16 px` 표시 입력을 편집하지 않은 채 전환하여 실제 `15.6 px`와 프리셋 보존을 검사한다. 각 행은 불투명도 편집과 Undo로 Redo 분기를 만든 뒤 Color 최초 선택, 같은 도구의 닫기·재열기, 하위 뷰 탐색, Brush 왕복에서 전체 IISC 문서 바이트, 렌더링 픽셀, revision, 선택 레이어, Undo·Redo 가능 상태가 같은지 확인한다. bitmap 행에서는 원본 픽셀도 비교한다. `revisionChanged`·`undoRedoChanged`가 발생하지 않는지 확인하고 마지막 실제 Redo로 원래 불투명도 편집을 복원한다. 호스트의 모바일 레이아웃 검사는 물리 모바일 기기 검증과 구분하며 통과 여부는 해당 실행 로그로 확인한다.

`editorToolSheets`는 현재 18개 도구의 402px와 320px 시트, 464개 카탈로그 필드/동작, 가로 범위, 하단 앵커링, 세로 스크롤 및 최종 컨트롤 도달 가능성, Escape 취소, 실제 옵션/스위치/텍스트/슬라이더 상호작용, 수치 거부, 색상 수락/취소, 차원 입력, 독립적인 시안, Reset 및 감소된 뷰포트 범위를 검사한다. 툴바 테스트는 Asset의 부재, Camera/Photo와 File의 연속 배치, 터치/마우스 슬라이딩, 아이콘, 키보드 탐색, 크기 조정 및 선택 간 시트 취소를 확인한다. `DREAMSCAPES_CAPTURE_DIR`를 `build/` 하위의 디렉터리로 설정하면 Elements, Color, Effects 및 Eraser의 네이티브 Qt 렌더링을 캡처한다. 물리적 iOS/Android 동작은 별도의 기기 실행이 필요하다.

모든 패널에 대해 최신 디자인 컨텍스트가 검색되었습니다. Figma의 MCP 계획 제한으로 인해 일부 효과 행 조회가 중단되었습니다. 저장된 통합 필드 인벤토리와 일치하는 LVRS 제어 사양은 나머지 행을 포함합니다. 유료 업그레이드가 사용되지 않았습니다.

2026-09-27번에서 확인됨: 19 개의 패널 / 428 개 렌더링된 컨트롤, 320px 개 및 402px 개의 시트 테스트, 6 개의 도구대 레이아웃, 데스크톱/모바일 캔버스 라우트, 그리고 실제 macOS Cocoa 윈도우의 402px 시트 상호작용 모범 사례. 로그 및 캡처는 `build/editor-panel-verification/` 에 있습니다. 모든 19 패널 또한 직접적인 도구 열기 전환/렌더링 스윕을 통과했습니다. 물리적 iOS / Android 유효성 검사는 미완료 상태입니다.

최종 표준 번들이 빌드되어 `codesign --verify --deep --strict` 포스트 단계를 통과했습니다. 프로덕션 기본 번들의 시작은 `root-loaded` 에서 도달했으며, 하나의 윈도우와 로딩 오류 없이 35.65 초가 걸렸습니다. 기존 `packagedApplicationStarts` 테스트는 여전히 30초의 루트 로딩 한도를 실패했습니다; 이 시작 지연 시간은 통과하는 회귀 테스트로 간주되지 않습니다. `packaged-start-final.txt` 와 `packaged-native-start.json` 를 확인 디렉토리에서 참조하세요.
# Current text views

The Text tool uses the newer four-view implementation described in
[EditorTextPanel.md](EditorTextPanel.md), sourced from Figma nodes `627:95323`,
`631:15674`, `631:15675` and `631:15676`. Its dedicated component replaces the
historical single text form. Color, Adjust (the existing `auto-enhance` key), Mask (`masking`) and Effects use
[EditorGraphicPanels.md](EditorGraphicPanels.md). These four tools now expose the
requested 381 detailed Figma views through a shared LVRS renderer, with separate
UI drafts, non-destructive before/after previews, and explicit Apply through
[iiGraphicAdjust](EditorGraphicAdjust.md). Other tools retain their layouts.

The Figma `Parameter`, `ToolTile` and `Curve plot` components map to
`EditorGraphicRow.qml` field, gallery and dynamic curve variants. The toolbar
contains 18 tools after removing Asset. Graphic subview navigation also synchronizes the
selected toolbar tool when moving from Adjust to Masks. Desktop dock and mobile
sheet use the same definitions and draft settings.

Run the focused graphic checks with `ctest --test-dir build -R
'Dreamscapes.EditorGraphic|Dreamscapes.EditorTools' --output-on-failure`. The
catalog and state tests are independent of native GUI rendering; graphic GUI
coverage opens all 381 views and compares 1,197 field contracts to a fixture
created from the supplied Figma inventory and original design ledger. Physical
iOS, Android and Windows execution require separate target runs.

Camera / Photo는 [전용 패널 계약](EditorCameraPhoto.md)을 따른다. Camera, Capture, Gallery 세 뷰를 데스크톱 dock과 모바일 sheet에서 공유하며, 19개의 이름 기반 설정을 사용한다. 촬영 결과는 Import 전까지 문서를 변경하지 않고, 갤러리는 iiPhotoLibrary의 네이티브 원본 및 로컬 폴더 이미지를 실제 레이어로 가져온다. 기존 scan/RAW 화면은 현재 Figma의 뷰에 포함되지 않는다. 전용 GUI 및 백엔드 테스트가 치수, 선택/가져오기, 취소/비동기 요청 수명과 원본 해상도를 검증한다.

File은 [3개 상태의 읽기 전용 전용 패널](EditorFilePanel.md)을 사용한다. 공통 필드·작업 카탈로그 대신 `EditorFileDefinitions.js`가 표시 항목과 바인딩만 소유한다. 상단 세그먼트는 표시 상태만 전환하며, File 내용 편집·저장·내보내기 동작은 제공하지 않는다. 독립적인 문서 열기·저장 경로는 `EditorDocumentControls`가 소유한다.
