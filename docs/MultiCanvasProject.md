# 생성 결과 선택과 여러 캔버스 프로젝트

생성 결과 갤러리는 일반 클릭으로 현재 이미지를 선택하고, 두 번 클릭하거나 Enter로 상세 보기를 연다. Cmd 또는 Ctrl 클릭은 선택을 추가하거나 해제한다. Shift 클릭은 마지막 선택 기준점에서 누른 이미지까지의 범위를 선택하고, Cmd/Ctrl+Shift는 그 범위를 기존 선택에 더한다. Cmd/Ctrl+A는 모든 이미지, Shift+방향키는 범위 확장이다. 영상과 생성 중인 미리보기는 이미지 선택에 포함하지 않는다. Escape는 복수 선택을 먼저 해제한다. 모바일에서는 Select 버튼을 누른 뒤 각 이미지를 눌러 선택할 수 있다.

Edit로 전달한 여러 이미지는 하나의 프로젝트 안에서 각각 독립된 iiSharedCanvas 문서가 된다. 캔버스 치수는 이미지 원본 치수이며, 각 캔버스에 해당 이미지의 비트맵 레이어를 만든다. 모든 입력을 검증한 후 프로젝트를 교체하므로 누락되거나 손상된 이미지가 있으면 현재 프로젝트를 유지한다. 동일한 실제 경로는 중복 제거하며 최대 1000장, 누적 64×1024×1024 픽셀의 가져오기 한계를 적용한다.

Editor의 Previous·Next 버튼, Alt+Left·Alt+Right, Page Up·Page Down으로 전환한다. 페이지 단축키는 캔버스 또는 에디터 본문에 포커스가 있을 때만 적용하며 입력 필드나 대화상자에서는 작동하지 않는다. 현재 위치는 Canvas n / N으로 표시한다. 각 캔버스의 레이어, 선택, 확대·이동과 iiPaintEngine 실행 취소 이력은 별도 CanvasItem 인스턴스에 유지한다. 펜 입력이 끝나기 전에는 전환하거나 저장할 수 없다. 현재 캔버스만 화면에 연결하고, 나머지 문서와 편집 이력은 프로젝트가 소유한다.

단일 작업 문서는 기존 `.iisc`로 저장한다. 여러 캔버스는 `.iiscp`로 저장하며, Save는 모든 캔버스를 저장한다. 제목의 `*`는 저장되지 않은 프로젝트 변경을 표시한다. `.iiscp`는 Dreamscapes 프로젝트 컨테이너이며 단일 iiSharedCanvas 문서 형식과 구분한다. 내부에는 순서대로 각 캔버스 이름과 SDK `encodeIisc()` 결과를 넣고 현재 캔버스 번호를 저장한다. 이미지 원본 경로를 재참조하지 않으므로 원본이 이동해도 프로젝트를 다시 열 수 있다. 저장은 임시 파일의 원자적 교체를 사용한다. 파일 크기는 1 GiB, 캔버스 이름은 1 MiB, 페이지 수는 1000으로 제한한다. 잘린 파일, 잘못된 버전·순서·길이와 불필요한 후행 데이터는 거부한다.

Home 파일 선택기, 최근 파일 카드와 Editor Open에서 `.iiscp`를 다시 열 수 있다. 단일 `.iisc` 작업 파일은 기존의 동기적 문서 편집 저장 방식을 사용한다. 프로젝트 컨테이너의 변경은 Save 또는 Save As로 저장하며, 저장한 프로젝트에 여러 캔버스의 픽셀과 레이어를 함께 포함한다.

## 검증

- EditorProject 검사는 독립 치수·픽셀, 페이지별 편집과 실행 취소, 전환 후 확대·이동 유지, 모든 페이지 저장·재개방, 실패 시 기존 프로젝트 보존과 단일 `.iisc` 호환성을 검사한다.
- MultiCanvasGui 검사는 실제 Cmd/Ctrl·Shift 클릭과 키보드 입력, 영상 제외, Editor 페이지 버튼과 단축키, 저장 대화상자와 다시 열기를 검사한다.
- HomeEditorRoutes·EditorCanvasGui·QuickGenerate는 기존 진입·복귀·브러시 편집과 결과 화면을 회귀 검사한다.

```sh
cmake --build build --parallel 4
ctest --test-dir build -R '^Dreamscapes\.(EditorProject|MultiCanvasGui|HomeEditorRoutes|EditorCanvasGui|EditorCanvas|QuickGenerate|NewCanvasGui|PromptFields|DesktopHome|AdvancedParameters)$' -j2 --output-on-failure
```

macOS 빌드에서 데스크톱·모바일 레이아웃을 검사한다. 모바일 실제 기기와 시스템 선택기 실행은 별도의 검증 범위이다. 설치 시 서명과 canonical·설치본 파일 일치를 확인하고, 격리된 저장소로 설치본 기동을 검사한다. 실행 중인 사용자 앱이나 생성 작업을 종료하지 않는다.

## 2026-10-03 설치 결과

현재 체크아웃의 빌드와 위 회귀 검사 10개, GenerationResources·AppIcons 배포 검사 2개가 모두 통과했다. `/Applications/Dreamscapes.app`를 macOS arm64 빌드로 교체했다. 서명은 로컬 ad hoc 서명이며 `codesign --verify --deep --strict`가 통과했다. canonical과 설치본의 파일 10,038개 및 심볼릭 링크 257개를 내용·경로·권한과 함께 비교하여 일치함을 확인했다. `build/`에는 canonical 앱 하나만 있으며 중첩 helper 앱은 없다.

설치된 실행 파일을 격리된 저장소와 macOS Cocoa 기본 렌더러로 실행한 `packagedApplicationStarts`가 3 passed / 0 failed로 통과했다. 이 검사는 실제 실행 진입점의 QML 화면 로드, 한 개의 창과 Society Helper 양방향 발견을 확인한다. 설치 직전 실행 중인 Dreamscapes 사용자 프로세스는 없었으며 검증용으로 시작한 자식 프로세스만 종료했다. 이전 설치본은 `build/multi-canvas/installed-before-7ca82caacca0.zip`에 압축 백업했고 ZIP 무결성 검사를 통과했다.

검증 기록은 `build/multi-canvas/verification.json`, `installation.json`, `installed-startup.log`, `single-app-audit.json`에 있다. 데스크톱·모바일 캡처와 실제 네 페이지 프로젝트 파일도 같은 경로에 남겼다. 회귀 검사에서 기존 LVRS `CardImageContent.qml`의 `drawImage(), index size error` 경고가 관측되었으며 전체 검사를 경고 없이 통과했다고 표현하지 않는다. 이번 복수 선택·프로젝트 GUI 검사와 설치본 기동은 실패 없이 통과했다.
