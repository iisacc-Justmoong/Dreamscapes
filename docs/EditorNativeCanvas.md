# Editor와 네이티브 iiSharedCanvas 문서 연결

Editor의 중앙 영역은 `EditorCanvas`, 즉 `iiSharedCanvas::CanvasItem`의 소비자 어댑터이다. 기본 화면에는 1024×1024 흰색 네이티브 `Document`를 생성한다. `New canvas`에서 선택한 치수와 배경도 동일한 문서 경로를 사용한다. Figma의 회색 예시 사각형과 별도의 PNG 표시용 `Image`는 제거했다. 흰색·검은색 배경은 벡터 자산이며, 투명 배경은 빈 문서이다.

## 열기와 저장

상단의 Open, File 패널의 Open, 플랫폼 Open 단축키, 로컬 파일 드롭, Home의 `.iisc` 파일 선택은 동일한 `openDocumentSource()`로 들어간다. SQLite 작업 문서는 `DocumentFile::open()` 후 직접 바인딩한다. 바이너리 `.iisc` 스냅샷은 한계를 적용해 디코딩한 네이티브 문서로 열며, Save As에서 새 SQLite 작업 파일을 만든다. 알 수 없는 형식, 손상된 파일과 원격 URL은 오류를 표시하고 현재 문서를 유지한다. 현재 의존 패키지는 iiSharedCanvas 0.26.0이며 `.iisc` 1.17까지의 자산·레이어 계약을 사용한다.

Save와 플랫폼 Save 단축키는 아직 파일에 연결되지 않은 문서에 저장 대화상자를 연다. 새 경로의 `.iisc`를 만든 뒤 작업 파일에 바인딩한다. 그 이후 브러시 완료, Undo/Redo와 레이어 편집은 SDK의 검증된 편집 경로를 통해 즉시 동기 커밋한다. 별도 타이머나 문서 전체 덤프를 사용하지 않는다. Save As는 새 경로만 허용하며 이미 존재하는 다른 파일을 덮어쓰지 않는다. 기존 작업 파일의 Save는 이미 커밋된 상태를 유지한다. 파일 CRUD와 SQLite 실행은 SDK의 iiFileProvider 경로가 담당한다.

PNG·JPEG 등의 생성 결과는 원본 픽셀을 가진 `RasterAsset`과 `BitmapLayer`로 가져온다. File 패널의 Place Image는 현재 문서에 별도 비트맵 레이어를 추가하며 Fit, Fill, 1:1 배치를 지원한다. 이미지 파일을 직접 수정하거나 네이티브 문서를 미리보기 이미지로 평탄화하지 않는다. 현재 가져오기는 Qt 이미지 코덱을 사용하며 선택적인 외부 코덱 실행은 활성화하지 않는다.

Home의 파일 선택기, 최근 파일, 새 캔버스와 생성 이미지 단일·다중 선택은 같은 Editor를 사용한다. 여러 원본은 프로젝트 안의 독립적인 캔버스로 가져온다. 모든 입력을 먼저 검증하고 실패 시 현재 프로젝트를 유지한다. [여러 캔버스 프로젝트](MultiCanvasProject.md)에 선택·페이지 전환·저장을, [Home 연결](HomeEditorRoutes.md)에 진입·복귀를 정리한다.

File 패널의 Open as copy를 켜면 작업 파일도 분리된 메모리 문서로 가져와 원본에 바인딩하지 않는다. 새 `.iisc` 경로에 저장하기 전까지 그 편집은 원본 파일에 반영되지 않는다. 상단 Open과 Home 파일 열기는 해당 작업 파일을 직접 편집하는 경로이다. Place Image의 Embed 모드를 연결했으며 Link와 Smart 모드는 안내 후 거부한다. 네이티브 저장은 레이어를 항상 보존한다. File 패널의 형식 필터·외부 변경 감시·평탄화 같은 나머지 초안 설정은 아직 적용하지 않는다.

## 보기와 편집

- SDK의 혼합 레이어 렌더러가 현재 프레임의 실제 문서를 표시한다. Fit 버튼, SDK 확대·축소, 중간 버튼 이동을 사용한다. 저장된 무한 캔버스와 자산·타임라인 정보도 네이티브 문서에 보존한다. 현재 화면에는 프레임 탐색 컨트롤을 추가하지 않았다.
- Brush는 크기, 경도, 투명도, 흐름, 간격, 안정화와 상단 Paint Color를 iiPaintEngine 브러시에 전달한다. 입력 경로는 `EditorCanvas → iiSharedCanvas::CanvasItem → BitmapEditor / ChunkedBitmapEditor → iiPaintEngine`이다. iiPaintEngine의 `appendRasterDabs()`, `projectBrushDabs()`, `paintRasterSamples()`가 실제 픽셀을 생성하며, 제품은 별도의 브러시 래스터라이저를 구현하지 않는다. 비트맵 레이어가 선택되지 않았으면 새 Paint 레이어를 만든다. 포인터 궤적과 Dab은 실행 중에만 사용하고, 커밋된 픽셀만 `.iisc` 문서에 저장한다.
- Eraser의 Pixel 모드는 같은 iiPaintEngine의 `DestinationOut` 합성으로 선택된 비트맵 레이어를 지운다. 다른 지우개 모드에는 엔진 동작을 연결하지 않았다. Undo/Redo는 SDK가 제공하는 선택 비트맵의 픽셀 편집 이력에 적용된다. 구조·벡터·레이어 속성 편집을 취소하는 전체 문서 이력은 아직 없다.
- Elements에서 캔버스를 드래그하면 Rectangle, Ellipse, Polygon 또는 직선의 네이티브 벡터 레이어를 만든다. 모서리 반경, 다각형 변 수, Solid/None 채우기, 채우기·선 색, 선 두께, 투명도, Normal/Multiply/Screen 합성을 반영한다. 세부 Arc·Star·선 끝 장식·획 정렬·Gradient/Image 채우기는 아직 연결하지 않았다. `Line / Arrow` 선택은 현재 직선 생성으로 제한된다.
- Select의 Object 모드는 최상단부터 현재 프레임의 렌더링 픽셀을 검사해 실제 레이어를 선택한다. Layers 버튼은 문서의 실제 레이어를 위에서 아래 순서로 표시한다. 선택, 표시 여부, 이름, 투명도, X/Y 이동, Paint 레이어 추가와 레이어 삭제를 제공한다. Layers 패널의 Opacity와 Blend 입력, Canvas 패널의 Dimensions도 네이티브 편집에 연결된다.

유한 비트맵 레이어는 SDK의 64×1024×1024 픽셀 할당 한계를 적용한다. 큰 인쇄용 캔버스의 벡터 배경과 희소 렌더링은 유지하며, 무한 캔버스의 새 Paint 레이어는 청크 기반 자산을 사용한다.

19개 Figma 도구의 외형과 초안 설정은 유지한다. Reset은 현재 도구의 초안과 브러시 설정을 초기화하며, 이미 편집한 문서를 복원하지 않는다. 카메라·생성·필터·마스크·외부 연결 등의 다른 동작은 미구현 안내를 표시한다. 초안은 Editor 수명 동안 메모리에 유지하며, 네이티브 문서에 설정값 전체를 저장한 것으로 취급하지 않는다.

## Native image presentation (iiSharedCanvas 0.25.0)

The editor consumes the SDK's full-resolution raster assets from binary `.iisc`
snapshots and SQLite working documents. It does not mount a reduced thumbnail or
resize source pixels. `EditorProject::attachCanvas` selects the SDK's smooth
presentation policy, mounts the native document item in its destination window,
and supplies the actual viewport size before fitting. Remounting an unchanged
viewport preserves the user's zoom and pan. Presentation changes do not mark the
document as edited.

SDK tile LOD includes the window's physical pixel ratio. A 45% view on a 2× display
retains native-resolution texture tiles. Minification uses alpha-correct filtering,
and GPU textures use linear presentation. Nearest sampling remains available in
the SDK for pixel inspection; no second product-specific resampler is introduced.

`Dreamscapes.EditorRenderQuality` opens both persisted document forms through the
real LVRS editor, compares source pixels after reopening, and compares the visible
canvas with a smooth reference image. The test writes screenshots under
`build/verification/iisc-render-quality/`. `DREAMSCAPES_RENDER_IMAGE` may select a
local source image for native Metal verification. `DREAMSCAPES_RENDER_DOCUMENT`
additionally checks an existing working document against that source without
editing the file. `mountUsesFullDocumentAndPhysicalViewport`
checks viewport sizing, window pixel ratio, smooth rendering and preserved camera
state. These checks do not imply that previously submitted generation jobs were
rerun or that an active user document was replaced.

## 검증

2026-10-05 raster-presentation verification uses iiSharedCanvas 0.25.0 on macOS
arm64 with Qt 6.8.3. All 53 SDK functional tests and 18 staged-package consumer
tests passed. The separate documentation contract still fails on a pre-existing
English literal missing from the translated dependency document. Native Metal
checks passed at 2× and 4× device pixel ratios.

The editor opened binary snapshots, new working documents, and the working
document saved by the earlier app. All original 1024×1368 raster pixels matched
after reopening. At 45% zoom on a 2× display, all three visible Metal canvases
used LOD 1 and had mean RGB error 1.8304 against the smooth source reference.
Source PNG and existing working-file SHA-256 hashes remained unchanged.
Screenshots are in `build/verification/iisc-render-quality/native/`; logs and
source-preservation records are in `build/verification/iisc-render-quality/`
and `build/render-quality-native.log`. The full editor-canvas test executable
also passed all 16 QtTest cases. The canonical bundle remains
`build/bin/Dreamscapes.app`, with one app and no nested app, and its deployment
performs strict recursive code-signature verification.

The final scoped CTest run passed 7/7: EditorCanvas, EditorProject,
EditorRenderQuality, EditorCanvasGui, HomeEditorRoutes, MultiCanvasGui and
HomeCanvas. The broader run passed 27/29; the all-in-one GUI run crashed in
the main-window shortcut context, and MCP exceeded its 90-second limit.
A preceding-generation-view test chain reproduces shortcut failure without
the new render-quality test. Extended MCP startup reaches the root but a
separate generation-queue fixture reports a failed job. These broader failures
remain recorded, rather than reported as passing renderer checks.
The user's already-running app was left intact after a new active generation
was observed; the verified canonical bundle is ready for a later launch.

`Dreamscapes.EditorCanvas`는 네이티브 빈 문서, 브러시와 Undo/Redo의 작업 파일 반영, 바이너리 스냅샷·이미지 가져오기, 기존 원본 보존, 잘못된 열기·편집 거부를 검사한다. `Dreamscapes.EditorCanvasGui`는 실제 Editor의 포인터 브러시·지우개 입력, Paint Color, Open/Save 대화상자 연결, Undo/Redo 버튼, 레이어 시트, 벡터 드래그 생성, Object 선택, 저장 문서의 독립 재개방을 검사한다. 기존 Home·결과 화면의 초안·선택 유지 검사도 함께 실행한다.

`brushPixelsMatchIiPaintEngine`은 iiPaintEngine 공개 API를 직접 호출한 기준 픽셀과 Editor의 `.iisc` 작업 파일을 독립적으로 재개방한 픽셀을 전체 비교한다. 유한·무한 캔버스별로 불투명 브러시, 부드러운 반투명 브러시, 간격·압력 입력, Flow 0의 8개 조합을 사용한다. 무한 캔버스에서는 음수 좌표와 청크 경계를 가로지른다. Pixel Eraser, Undo/Redo, 진행 중 스트로크 취소도 같은 기준 픽셀과 비교한다. 실제 Figma 패널의 세부 Shape·Blend·압력 영향 비율·Tilt 설정 전체를 연결했다는 의미는 아니다.

2026-10-03 iiPaintEngine 경로의 추가 검증에서 위 8개 조합은 모두 통과했다. `Dreamscapes.EditorCanvas`와 `Dreamscapes.EditorCanvasGui` CTest 2개가 통과했으며, QtTest 세부 결과는 초기화·종료를 포함해 20개 통과·실패 0개이다. 제품 전체 빌드도 통과했다. 빌드된 앱과 iiSharedCanvas 0.24.0 라이브러리의 실제 의존성이 번들 내부 `libiiPaintEngine.dylib`를 참조함을 확인했다. 기록은 `build/native-editor/paint-engine-*.log`와 `paint-engine-verification.json`에 보관한다.

```sh
cmake -S . -B build
cmake --build build --parallel 4
ctest --test-dir build -R '^Dreamscapes\.(EditorCanvas|EditorCanvasGui|EditorToolPanels|NewCanvas|NewCanvasGui)$' --output-on-failure
```

GUI 증거와 `.iisc` 예시는 로컬 `build/native-editor/`에 보관한다. Qt 자동 입력 및 macOS 빌드 번들 검증이며, 물리적 태블릿의 압력 입력과 다른 운영체제의 실제 기기 실행은 별도 검증 대상이다.

2026-10-03 macOS arm64·Qt 6.8.3 Release 검증에서 관련 CTest 6개가 모두 통과했다. 기존 19개 도구 패널, 네이티브 문서, 실제 Editor 입력·저장·재개방, 새 캔버스 및 Home 캔버스 검사를 포함한다. GUI 실행의 QML 경고는 0개이며 빌드된 앱의 진입점 기동 검사는 3개 통과·실패 0개이다. `codesign --verify --deep --strict`와 단일 앱 감사도 통과했다. canonical 산출물은 `build/bin/Dreamscapes.app` 하나이며 중첩 앱은 없다.

`build/native-editor/example.iisc`는 GUI에서 그린 비트맵과 네이티브 벡터 레이어를 함께 보존한 64×64 검증 문서이다. SQLite 무결성 검사 결과는 `ok`이다. `editor-native.png`는 이 문서를 확대해 표시한 실제 Editor 캡처이며 문서 자체의 대체물이 아니다. 실행 로그와 산출물 SHA-256은 `verification.json`에 기록했다. 실행 중인 사용자 앱을 재시작하거나 설치된 앱 번들을 교체하지 않았다.

2026-10-03 검증에서는 iiSharedCanvas 소스의 빌드와 전체 SDK 검사도 실행했다. 기능 검사 52개는 통과했으며, 기존 한국어 문서 번역 이후 영어 문구를 찾는 `iiSharedCanvas.ProjectContract` 1개가 실패했다. 해당 기존 변경은 보존했다. 스테이징 패키지의 독립 설치 소비자 17개와 별도 소비자 실행 프로그램은 모두 통과했으며, 검증한 SDK 0.24.0 설치본에 제품을 연결했다. 그 당시에는 SDK 코드 변경이 없었다.
