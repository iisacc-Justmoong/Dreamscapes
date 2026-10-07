# Editor의 전체 데스크톱 도구 패널

2026-10-07: The runtime Layer tool now uses the four document-backed views described in [EditorLayers.md](EditorLayers.md). The historical generic Layers fixture remains available for design catalog checks without a canvas engine.

2026-10-07: Headings and parameter rows now use the passive [PanelRow](PanelRows.md) layout. Labels have no row hover, pressed or keyboard focus state. Preview requests use the trailing disclosure control; the enclosing preview surface remains flat. The same row contract is used by image and video generation panels.

출처는 [Dreamscapes / Editor / Unified Tool Detail Panels · LVRS](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=165-3042&m=dev)이다. 기존 Elements 화면에 이어 나머지 18개 도구의 오른쪽 패널을 구현한다. 현재 디자인은 도구마다 모든 세부 컨트롤을 모은 통합 패널이다. 선택기와 옵션을 바꾸면 선택 상태와 초안 값이 바뀌며, 디자인처럼 나머지 컨트롤도 계속 표시한다.


2026-10-06: The fixed-width geometry below is historical. Current desktop width, parameter alignment and wrapping follow [EditorPanelResizing.md](EditorPanelResizing.md), based on Figma 353:22022. Existing control semantics and the historical design catalog remain available.

## 화면과 상태

`EditorDesktopPanel`은 모든 도구에서 Figma의 데스크톱 배치를 사용한다. 패널 폭은 432px, 콘텐츠 폭은 398px, 바깥 여백은 테두리를 포함해 17px이다. 제목과 Reset 뒤에 선택기·컨트롤·동작 버튼을 16px 간격으로 배치한다. 제목에 닫기 버튼을 추가하지 않으며, 같은 도구 선택 또는 Escape로 패널을 접는다. 도구를 바꾸면 새 패널의 스크롤 위치는 위로 이동한다.

`EditorToolDefinitions`의 `desktopColumns`와 `desktopActionColumns`는 각 디자인의 옵션 슬롯을 따른다. 예를 들어 Text는 4열, Camera/Photo와 Eraser는 2열 선택기를 사용한다. 22px 버튼과 8px 간격으로 다음 줄을 배치한다. 콘텐츠가 좁아지면 옵션 열 수를 줄인다. 모바일 시트의 기존 레이아웃은 별도 경로로 유지한다.

텍스트 입력은 라벨 오른쪽의 206×22px 필드이다. Asset의 `License & Version · License`는 Figma처럼 라벨 아래에 필드를 배치하며 전체 높이는 41px이다. 이 예외는 해당 필드의 `desktopStacked` 속성으로 명시한다. 슬라이더는 제목 오른쪽의 수치 입력과 아래쪽 320×22px 조절기로 구성한다. 스위치는 오른쪽의 38×22px 컨트롤이다. 미리보기 항목은 기존 Navigation 외형을 유지하는 280×44px 평면 행으로, 72×11px `View` 값과 18px 화살표 버튼을 포함한다. 실제 이미지·히스토그램·곡선 콘텐츠의 요청 신호는 우측 화살표 버튼에서 발생한다.

데스크톱 수치 표시는 Figma의 양수 기호와 유니코드 음수 기호를 반영한다. 입력이나 슬라이더에서 설정한 값에 필요한 소수점 정밀도를 유지한다. 슬라이더 손잡이와 색상 선택 표면은 실제 초안 값을 반영하므로 Figma의 고정 예시 위치·색상과 차이가 있을 수 있다.

## 연결 범위

19개 도구의 428개 필드·동작과 49개 상위 선택 옵션, 267개 필드 옵션을 연결한다. 컨트롤의 기본·선택·호버·누름·키보드 포커스 표면은 LVRS 컴포넌트가 담당한다. 도구별 초안은 `EditorToolSheet.settingsByTool`에서 공유하며, 도구 전환·패널 접기·창 크기 변경 후에도 유지된다. Reset은 현재 도구만 초기화한다. 색상 입력 및 픽커의 수락·취소는 기존 계약을 따른다.

동작 버튼과 미리보기 조작 버튼은 `CanvasEditor.toolActionRequested(toolId, fieldId, values)`를 방출한다. 네이티브 문서 열기·저장·이미지 배치, 레이어 속성, 기본 도형과 브러시 연결은 [EditorNativeCanvas.md](EditorNativeCanvas.md)에 정의되어 있다. 나머지 필터·카메라·모델 등의 동작은 미구현 안내를 표시한다. 패널 초안은 편집기 수명 동안 메모리에 보관한다.

## 디자인 근거와 자산

19개 메인 패널의 최신 디자인 컨텍스트를 요청했으며, 큰 Color·Effects 패널은 하위 행으로 나누어 읽었다. 일부 하위 조회가 Figma MCP 요금제의 호출 한도에 도달했다. 동일 노드의 저장된 고해상도 컨텍스트를 재사용하고, `figma-use`의 읽기 기능으로 해당 16개 행의 현재 라벨·값·컴포넌트 종류·크기를 다시 확인했다. 저장된 기록에도 없던 Effects 세 행은 현재 노드의 컴포넌트 속성과 이미 조회한 동일 LVRS 컨트롤 규격을 사용했다. Figma 디자인 노드는 변경하지 않았다.

Navigation의 화살표는 설치된 LVRS 자산과 Figma 제공 파일이 일치하지 않아 제공 SVG를 `Assets/Panel/general-chevron-right.svg`로 저장했다. 18×18px 자연 크기와 오른쪽 슬롯 위치를 유지하며 CMake 리소스에 포함한다. 제목·컨트롤·동작·미리보기 행은 LVRS를 재사용한다. 전체 Figma 캡처는 비교 근거이며 제품의 구현 이미지로 사용하지 않는다.

## 검증

`fixtures/FigmaPanels.json`은 Figma에서 확인한 19개 패널의 좌표·크기·옵션·입력 예시를 담는 독립적인 검증 기준이다. `desktopEditorToolPanelsMatchFigma`는 패널별 모든 필드 위치와 초기값, 선택·동작 버튼 슬롯, 미리보기 화살표의 로컬 자산과 실제 렌더링 크기, 316개 선택 상태의 클릭, 도구별 초안 복원·Reset·스크롤·Escape를 검사한다. 기존 Elements·수치 계약·모바일 19개 시트 검사도 함께 실행한다.

```sh
cmake --build build --target DreamscapesGuiTests --parallel 4
DREAMSCAPES_CAPTURE_DIR="$PWD/build/figma-editor-panels/captures" \
  ctest --test-dir build -R '^Dreamscapes.EditorToolPanels$' --output-on-failure
```

캡처는 도구 이름별 PNG로 저장한다. 최신 컨텍스트, 비교 인벤토리, 자산 감사 및 실행 로그는 로컬 `build/figma-editor-panels/`에 보관한다. 검증은 Qt 렌더링과 빌드 번들을 기준으로 하며, 물리적 iOS·Android 기기 실행은 별도 대상이다.

2026-10-03 검증 결과: Release 빌드가 완료되었으며 `Dreamscapes.EditorToolPanels`는 73.29초에 통과했다. 내부 GUI 검사는 25개 통과·실패 0개·실행 중 QML 경고 0개이다. 19개 패널의 428개 필드·동작과 316개 옵션 선택 상태를 확인했으며 모든 패널의 실제 Qt 캡처를 저장했다. 기존 Elements 및 모바일 두 화면 크기의 19개 시트 검사도 포함한다. 빌드된 번들의 기동 검사는 3개 통과·실패 0개이며, `codesign --verify --deep --strict`와 단일 앱 감사가 통과했다. 정식 번들은 `build/bin/Dreamscapes.app` 하나이고 중첩 `.app`은 없다. 실행 중인 사용자 앱은 재시작하지 않았다. 결과와 실행 파일 SHA-256은 `build/figma-editor-panels/verification.json`에 보관한다.
