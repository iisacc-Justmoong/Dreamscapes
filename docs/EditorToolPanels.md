<a id="editor-tool-bottom-sheets"></a>

# 편집기 도구 하단 시트

데스크톱 Editor의 펼쳐진 Elements 샘플과 공통 입력 상태는 [EditorElements.md](EditorElements.md)에 설명되어 있다. 아래 내용은 기존 모바일 시트의 상세 계약이다.

2026-10-03에 추가한 19개 데스크톱 오른쪽 패널과 상태 검증은 [EditorDesktopPanels.md](EditorDesktopPanels.md)에 설명되어 있다.

출처: [Dreamscapes / 편집자 / 통합 도구 세부 정보 패널](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=165-3042).

모바일 도구 모음은 선택한 도구에 대해 하나의 LVRS `Sheet`를 연다. 19개 패널과 428개 필드/동작을 모두 표현한다. 시트는 아래에서 올라오며 긴 패널은 세로로 스크롤한다. 닫기·Escape·배경 누르기·네이티브 손잡이를 아래로 끌기로 시트를 닫는다. Escape는 편집기를 떠나기 전에 패널부터 닫는다. Main이 시스템/키보드 인셋을 소유하며, 시트는 인셋을 두 번 적용하지 않고 편집기의 남은 영역을 사용한다. 데스크톱 레이아웃으로 바꾸거나 편집기를 떠나면 도구 시트와 색상 선택기를 모두 닫는다.

<a id="component-mapping"></a>

## 구성 요소 매핑

기존 시각적 컨트롤인 LVRS, `Sheet`, `Label`, `LabelButton`, `IconButton`, `InputField`, `ToggleSwitch`, `Slider`, `ListItem`, `ColorPickerButton`, `ColorPicker` 만 사용됩니다. 로컬 QML 파일은 레이아웃과 상태를 구성하며, 사용자 정의 시각적 컨트롤, 페인트 구현, 또는 컴포넌트 라이브러리를 도입하지 않습니다.

`EditorToolDefinitions.js` 는 검토된 필드 카탈로그로, Figma 노드 ID, 안정적인 필드 ID, 초기 값, 옵션, 및 수치 범위를 포함합니다. `EditorToolControl.qml` 는 필드를 렌더링하며, `EditorToolPanel.qml` 는 행과 작업을 배열하고, `EditorToolSheet.qml` 는 선택된 도구와 그 초안을 관리합니다. `CanvasEditor` 는 도구바 선택을 시트에 연결합니다. Figma 의 컴팩트 2열 슬라이더/스위치 행은 340px 의 콘텐츠 너비 아래 1열로 바뀝니다. 옵션 버튼이 감싸지며, 차원은 2 로 표시된 입력을 유지합니다. 네이티브 시트 Figma 패널에 드래그 핸들과 닫기 애퍼던스를 추가합니다.

|도구 모음|필드/작업|Figma 노드|
| --- | ---: | --- |
|요소| 18 | 196:3034 |
|텍스트| 16 | 196:3413 |
|카메라/사진| 16 | 196:3703 |
|자산| 16 | 196:3911 |
|파일| 13 | 197:3529 |
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

각 도구는 편집기 수명 동안 독립적인 메모리 내 초안을 유지합니다. 해제/다시 열기, 도구 전환, 또는 크기 조절은 이를 유지합니다. Reset은 현재 도구의 초기 값을 복원하며, 현재 보정 세션의 원본 픽셀도 복원한다. 옵션, 선택기, 스위치, 텍스트, 차원, 슬라이더 및 색상 컨트롤은 이 시안들을 업데이트합니다. 수치 입력은 Enter 키 입력 또는 포커스 손실 시 커밋되며, 형식이 잘못되거나 범위 밖이거나 역방향인 경우 이전 값으로 복원됩니다. 수치 범위는 명시적인 애플리케이션 제약 조건이며, 디자인은 편집 엔진의 제한이 아닌 예시를 제공합니다. 깊이 범위는 두 끝점을 모두 보존합니다. 색상은 LVRS 픽커 또는 6자리 16 진수 필드를 통해 커밋되며, 픽커를 취소하면 이전 색상이 보존됩니다.

이 문서는 컨트롤 패널의 입력 계약을 설명한다. 작업 버튼 및 미리보기 행은 `CanvasEditor.toolActionRequested(toolId, fieldId, values)`를 방출한다. 네이티브 문서와 연결한 파일·레이어·브러시·기본 도형 동작은 [EditorNativeCanvas.md](EditorNativeCanvas.md)에 정의되어 있다. 실제 도구 실행, 미리보기, 입력 조건 및 포맷의 한계는 [EditorToolBehavior.md](EditorToolBehavior.md)에 정의되어 있다. 지원하지 않는 항목은 성공 안내 대신 비활성 상태와 사유를 제공한다. 패널 초안 자체는 디스크에 영구 저장하지 않으며 기존 홈/결과 화면의 선택과 생성 초안을 유지한다.

<a id="verification"></a>

## 검증

`build/`에서만 빌드:

```sh
cmake -S . -B build
cmake --build build --target DreamscapesGuiTests -j 6
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_DISABLE_DISK_CACHE=1 \
  build/bin/DreamscapesGuiTests editorToolSheets mobileEditorToolbarSlidesAndSelects canvasRoutesPreserveSelectionAndDraft
```

`editorToolSheets` 는 19 패널의 402px 와 320px, 필드 개수, 가로 범위, 하단 앵커링, 세로 스크롤 및 최종 컨트롤 도달 가능성, Escape 취소, 실제 옵션/스위치/텍스트/슬라이더 상호작용, 수치 거부, 색상 수락/취소, 차원 입력, 독립적인 시안, Reset, 및 감소된 뷰포트 범위를 모두 포함합니다. 기존 툴바 테스트는 터치/마우스 슬라이딩, 아이콘, 키보드 탐색, 크기 조정 및 선택 간 시트 취소와 함께 데스크톱 가시성을 포함합니다. `DREAMSCAPES_CAPTURE_DIR` 를 `build/` 하위의 디렉토리로 설정하여 Elements, Color, Effects, 및 Eraser 의 네이티브 Qt 렌더링을 캡처합니다. 물리적 iOS / Android 동작은 별도의 기기 실행이 필요합니다.

모든 패널에 대해 최신 디자인 컨텍스트가 검색되었습니다. Figma의 MCP 계획 제한으로 인해 일부 효과 행 조회가 중단되었습니다. 저장된 통합 필드 인벤토리와 일치하는 LVRS 제어 사양은 나머지 행을 포함합니다. 유료 업그레이드가 사용되지 않았습니다.

2026-09-27번에서 확인됨: 19 개의 패널 / 428 개 렌더링된 컨트롤, 320px 개 및 402px 개의 시트 테스트, 6 개의 도구대 레이아웃, 데스크톱/모바일 캔버스 라우트, 그리고 실제 macOS Cocoa 윈도우의 402px 시트 상호작용 모범 사례. 로그 및 캡처는 `build/editor-panel-verification/` 에 있습니다. 모든 19 패널 또한 직접적인 도구 열기 전환/렌더링 스윕을 통과했습니다. 물리적 iOS / Android 유효성 검사는 미완료 상태입니다.

최종 표준 번들이 빌드되어 `codesign --verify --deep --strict` 포스트 단계를 통과했습니다. 프로덕션 기본 번들의 시작은 `root-loaded` 에서 도달했으며, 하나의 윈도우와 로딩 오류 없이 35.65 초가 걸렸습니다. 기존 `packagedApplicationStarts` 테스트는 여전히 30초의 루트 로딩 한도를 실패했습니다; 이 시작 지연 시간은 통과하는 회귀 테스트로 간주되지 않습니다. `packaged-start-final.txt` 와 `packaged-native-start.json` 를 확인 디렉토리에서 참조하세요.
