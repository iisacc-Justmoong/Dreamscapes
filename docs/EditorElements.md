# Editor의 Elements 샘플

대상 디자인은 [Dreamscapes Editor `353:22614`](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=353-22614&m=dev)이다. 이 문서는 Editor 화면과 펼쳐진 `01 Elements` 컨트롤 패널의 외형 및 입력 동작을 설명한다. 이후 연결한 네이티브 도형 생성과 문서 편집은 [EditorNativeCanvas.md](EditorNativeCanvas.md)에 정의되어 있다.

## 구성

2357×1346 참조 화면에서 내비게이션은 181px, 오른쪽 패널은 432px, 중앙 작업 영역은 1744px이다. 84px 높이의 19개 도구바는 중앙 영역 아래에 붙으며 오른쪽 패널 아래로 이어지지 않는다. Elements 패널은 16px 패딩과 1px 테두리, 16px 반경을 사용한다. 제목·라벨·버튼·입력·슬라이더·스위치·색상 선택은 설치된 LVRS 컴포넌트로 구성한다.

`EditorDesktopPanel.qml`은 기존 `EditorToolPanel`과 `EditorToolControl`의 샘플 레이아웃을 사용한다. Elements의 크기 입력은 세로로 배열하며, 슬라이더의 수치 입력은 제목 오른쪽에 배치한다. 206×22px 입력, 320×22px 슬라이더, 38×22px 스위치, 28px 색상 버튼과 균등한 옵션 슬롯을 사용한다. 패널의 내용이 창 높이를 넘으면 세로로 스크롤한다.

다른 도구에도 동일한 데스크톱 규격을 적용하고 패널별 옵션 열 수·미리보기·동작 버튼을 보완한 내용은 [EditorDesktopPanels.md](EditorDesktopPanels.md)에 설명되어 있다.

`DesktopHomeSidebar.editorLayout`은 Editor에서만 16px 가로 여백·0px 상단 여백을 사용한다. Home의 기존 기본 배치는 유지한다. 화면이 좁아지면 내비게이션은 아이콘 형태로 줄어들며 도구바는 가로로 스크롤한다.

중앙 영역에는 기본 네이티브 iiSharedCanvas 문서를 생성하며, PNG 표시용 Image와 821×1032px 회색 예시 사각형은 제거했다. 제공된 이미지는 문서의 비트맵 레이어로 가져오고 `.iisc`는 네이티브 문서로 연다. Figma의 외곽 패널·도구 배치는 유지한다.

## 상태와 상호작용

기본 선택은 Elements이며 오른쪽 패널이 펼쳐져 있다. 같은 도구를 다시 선택하면 패널을 접거나 펼친다. 다른 도구를 선택하면 해당 도구의 기존 패널을 표시한다. 입력값은 기존 `EditorToolSheet.settingsByTool`에 도구별로 저장하므로 패널을 접거나 도구를 전환해도 유지된다. Reset은 현재 도구만 초기화한다. 모바일에서는 기존 하단 시트 방식을 사용한다.

옵션·크기·수치·스위치·색상은 실제 초안 값을 편집한다. 수치 범위, 잘못된 입력 거절, 색상 선택 취소는 기존 계약을 따른다. 슬라이더 위치는 실제 값과 범위를 반영한다. Figma의 고정 50% 슬라이더 예시와 수치 값이 불일치하는 항목에서는 런타임 값이 기준이다.

Home은 이전 화면으로 돌아가며 다른 내비게이션 항목은 기존 Home 라우팅에 연결한다. Escape는 열린 색상 선택창이나 도구 패널을 먼저 닫는다. 입력 초안은 아직 원본 이미지나 SDK 문서의 도형에 적용되지 않는다.

## 자산 및 검증

Figma `353:21758`의 도구 SVG 19개와 `353:22462`의 내비게이션 SVG 9개를 내려받아 기존 로컬 파일과 SHA-256을 비교했다. 모두 동일한 파일이다. 기존 자산을 재사용하고 자연 크기와 22px·18px 아이콘 슬롯 내 위치를 유지한다. Files의 13.375×15.6659px 벡터도 LVRS IconButton의 18px 슬롯을 비례 축소해 원래 크기로 배치한다. ColorPicker와 Slider 내부 표면은 같은 LVRS 컴포넌트로 재사용한다.

```sh
cmake --build build --target DreamscapesGuiTests --parallel 4
ctest --test-dir build -R '^Dreamscapes.EditorElements$' --output-on-failure
```

`desktopEditorElementsMatchesFigma`는 참조 크기에서 영역·필드·입력·슬라이더 위치, 옵션·수치·스위치 입력, 패널 접기·복원, 도구별 초안, Reset, 작은 창의 마지막 색상 컨트롤 접근, 색상 창 닫기와 Home 동작을 검사한다. 함께 실행하는 기존 테스트는 모바일 19개 도구 패널, 도구바 아이콘·스크롤·키보드, 캔버스 라우팅과 Home 내비게이션을 검사한다.

`DREAMSCAPES_CAPTURE_DIR`를 `build/` 하위 경로로 지정하면 `desktop-elements.png`를 저장한다. Figma 원본, 자산 감사와 실행 로그는 로컬 `build/figma-editor-elements/`에 보관한다. 실제 설치된 앱의 화면과 물리적 iOS·Android 실행은 별도 검증 대상이다.

2026-10-03 macOS·Qt 6.8.3 검증에서 앱과 GUI 테스트 빌드가 완료되었다. `Dreamscapes.EditorElements`는 CTest 1/1 통과, 내부 QtTest 집계는 초기화·정리를 포함해 15 passed / 0 failed이다. `packagedApplicationStarts`는 canonical 앱의 실제 진입점·QML 로드·SDK 발견을 확인했으며 3 passed / 0 failed이다. `codesign --verify --deep --strict`와 공통 정책의 앱 감사도 통과했다. `build/bin/Dreamscapes.app`만 존재하며 중첩 `.app`은 없다.

GUI 회귀 검사는 `offscreen`·소프트웨어 렌더러 환경에서 수행했다. 기존 Qt Labs Platform 메뉴의 native Menu 경고는 이 환경에서도 출력되며, 네이티브 메뉴 동작 자체는 이 검증의 대상이 아니다. 실행 중인 사용자 앱을 재시작하지 않고 별도 테스트 프로세스로 검증했다.
