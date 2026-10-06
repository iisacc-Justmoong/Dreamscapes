# Home에서 Editor로 진입하는 흐름

Home의 파일 열기, 새 캔버스 생성, 최근 파일과 생성 이미지 선택은 `Main.qml`이 소유한 Editor 진입 경로를 사용한다. 데스크톱과 모바일은 같은 `CanvasEditor`와 네이티브 iiSharedCanvas 문서를 사용한다.

## 진입점

| 동작 | 입력 UI | Editor에 전달하는 내용 |
| --- | --- | --- |
| 파일 열기 | 데스크톱 Continue creating의 Open file, 모바일 Open file, 플랫폼 Open 단축키 | `.iiscp` 프로젝트, `.iisc` 작업 문서 또는 이미지 원본 |
| 최근 파일 열기 | Home의 최근 파일 카드 | 실제 `path`의 문서·이미지. 미리보기는 실제 경로가 없을 때 사용한다. |
| 새 캔버스 | 데스크톱 Canvas, 모바일 새 캔버스, 플랫폼 New 단축키 | 기존 NewCanvasDialog가 검증한 치수·배경의 새 네이티브 문서 |
| 생성 이미지 한 장 | 생성 이력 카드, 결과 화면 New Canvas, 고급 생성 Open in canvas | 원본 비트맵 레이어와 선택 항목의 메타데이터 |
| 생성 이미지 여러 장 | 생성 이력·결과 갤러리·고급 생성의 Select → 항목 선택 → Edit (개수) | 한 프로젝트 안의 독립적인 네이티브 캔버스들 |

모바일 Home의 파일 카드 클릭과 다중 선택을 Main에 연결한다. 모바일 Storage는 파일 열기, Tools는 Editor 진입, Home은 Home 스크롤 복귀를 수행한다. 영상은 기존 영상 결과 화면으로 이어지며 이미지 다중 선택에 포함하지 않는다.

Home 모델의 로컬 경로는 `QUrl::fromLocalFile()`로 변환해 전달한다. 공백, `%`와 `#`가 포함된 파일명을 URL의 쿼리·프래그먼트로 해석하지 않는다. Editor 진입과 고급 생성 화면 전환 중에 숨겨진 Home 카드는 미리보기 로딩을 중단하며, 카드 모델과 선택 상태는 유지한다.

## 여러 이미지의 프로젝트 구성

`EditorProject::openImages()`는 모든 이미지를 검증한 뒤 한 프로젝트로 채택한다. 각 이미지는 원본 치수의 독립적인 iiSharedCanvas 문서와 비트맵 레이어를 갖는다. Editor의 이전·다음 버튼과 Alt+Left/Right, Page Up/Down으로 넘나들며 각 캔버스의 편집 이력과 보기 상태를 유지한다.

동일한 실제 파일 경로는 중복 제거하며 최대 1000개, 누적 64×1024×1024 픽셀을 가져올 수 있다. 실패하면 이전 프로젝트를 유지한다. 단일 문서는 `.iisc`, 전체 프로젝트는 `.iiscp`로 저장한다. 자세한 파일 구성과 복수 선택 규칙은 [여러 캔버스 프로젝트](MultiCanvasProject.md)에 정리한다.

## 선택과 돌아가기

선택 상태는 작업 ID가 아니라 원본 이미지 경로를 기준으로 유지한다. 같은 생성 작업이 여러 출력 이미지를 반환해도 각각 선택할 수 있다. 새 결과가 추가되면 기존 선택을 유지하고, 목록에서 사라진 항목만 선택에서 제거한다. Select 모드에서는 카드를 누르면 선택을 토글한다. 결과 갤러리는 Cmd/Ctrl·Shift 클릭과 키보드 복수 선택도 지원한다. Cancel은 선택 모드와 선택을 초기화하며, 기본 모드의 카드 클릭은 한 장을 바로 연다.

결과 화면에서 상세 이미지 보기 중 Select를 누르면 갤러리로 돌아가 여러 항목을 선택할 수 있다. 선택 중에 결과가 한 장으로 줄어도 선택 갤러리를 유지한다.

Editor를 닫으면 진입한 Home 또는 결과 화면으로 돌아간다. 프롬프트 초안, 결과 갤러리와 기존 선택은 유지한다. 파일 선택 대화상자의 취소와 열기 실패는 화면 전환을 일으키지 않는다. 파일 열기 오류는 현재 화면에 표시한다. 생성 작업을 취소하거나 변경하지 않는다.

고급 생성 썸네일은 고정 크기의 LVRS AbstractButton으로 제공한다. 비동기 이미지의 원본 치수가 바뀌어도 입력 영역은 64px로 유지하며, 선택 테두리와 키보드 포커스를 제공한다.

## 검증

- `Dreamscapes.EditorProject`와 `Dreamscapes.MultiCanvasGui`: 캔버스별 치수·픽셀·편집·보기와 실행 취소 이력, 수정 키 선택, 페이지 전환, 전체 저장·재개방과 실패 시 프로젝트 유지 여부를 검사한다.
- `Dreamscapes.HomeEditorRoutes`: 데스크톱·모바일의 실제 Open file 버튼과 대화상자 콜백, 최근 파일 카드, 새 캔버스 버튼, 생성 이력 다중 선택, 상세 보기에서 결과 갤러리 다중 선택으로의 전환과 Editor 복귀를 검사한다. `advancedCanvasMatchesResolutionAndStreamsItsBatch`도 포함해 생성된 고급 배치의 여러 이미지 선택과 Editor 전달을 검사한다.
- 기존 EditorCanvasGui, DesktopHome, NewCanvasGui, QuickGenerate 검사로 단일 이미지 경로와 초안·결과·선택 보존을 함께 확인한다.

```sh
cmake -S . -B build
cmake --build build --parallel 4
ctest --test-dir build -R '^Dreamscapes\.(HomeEditorRoutes|EditorCanvas|EditorCanvasGui|DesktopHome|NewCanvas|NewCanvasGui|QuickGenerate)$' --output-on-failure
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_DISABLE_DISK_CACHE=1 build/bin/DreamscapesGuiTests advancedCanvasMatchesResolutionAndStreamsItsBatch
```

GUI 자동 입력은 macOS 빌드의 데스크톱·모바일 레이아웃에서 실행한다. iOS·Android의 실제 시스템 파일 선택기와 기기 입력은 별도 실기 검증 대상이다. 검증 로그와 캡처·네이티브 문서는 `build/home-editor/`에 보관한다.

2026-10-03 초기 단일 문서 연결 검증에서는 관련 CTest 9개를 통과했다. 여러 캔버스 프로젝트로 변경한 이후의 최종 검사·설치 결과는 `build/multi-canvas/`에 기록한다. 최종 다중 선택 보완 후 HomeEditorRoutes를 다시 실행해 데스크톱·모바일 문서 저장과 재개방을 확인하고, 렌더링된 캔버스 중앙 픽셀이 SDK의 문서 렌더링 결과와 일치한 뒤 화면을 캡처했다. 고급 생성 배치 선택 검사와 Cocoa 플랫폼의 실제 번들 기동 검사도 별도로 통과했다.

offscreen 검사 로그에는 Qt Labs Platform의 네이티브 메뉴 지원 진단과 LVRS CardImageContent의 `drawImage(), index size error` 경고가 발생했다. 이 진단이 남아 있음을 기록하며, 네이티브 캔버스의 저장·표시 결과는 실제 픽셀 비교로 확인한다. 새 모델을 이용한 AI 추론이나 모바일 실기 실행을 이 경로 검증으로 주장하지 않는다.
