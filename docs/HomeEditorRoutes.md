# Home에서 Editor로 진입하는 흐름

Home의 파일 열기, 새 캔버스 생성, 최근 파일과 생성 이미지 선택은 `Main.qml`이 소유한 Editor 진입 경로를 사용한다. 데스크톱과 모바일은 같은 `CanvasEditor`와 네이티브 iiSharedCanvas 문서를 사용한다.

Home의 All files와 Generation History / View all은 Dreamscapes 내부의 전체 파일·갤러리 화면을 연다. 같은 `iiSocietyContainer` 미러의 전체 Alias 목록을 읽으며, Society로 이동하지 않는다. 폴더 탐색, 검색, 정렬과 다중 이미지 편집을 지원한다. 이 화면에서 에디터를 열었다가 닫으면 탐색하던 위치를 유지한다. 상세한 범위와 검증은 [내부 저장소 탐색](StorageBrowser.md)에 정리한다.

## 진입점

| 동작 | 입력 UI | Editor에 전달하는 내용 |
| --- | --- | --- |
| 파일 열기 | 데스크톱 Continue creating의 Open file, 모바일 Open file, 플랫폼 Open 단축키 | `.iiscp` 프로젝트, `.iisc` 작업 문서 또는 이미지 원본 |
| 최근 파일 열기 | Home의 최근 파일 카드 | 실제 `path`의 문서·이미지. 미리보기는 실제 경로가 없을 때 사용한다. |
| 새 캔버스 | 데스크톱 Canvas, 모바일 새 캔버스, 플랫폼 New 단축키 | 기존 NewCanvasDialog가 검증한 치수·배경의 새 네이티브 문서 |
| 생성 이미지 한 장 | 생성 이력 카드, 결과 화면 Select → 한 장 선택 → Edit (1), 고급 생성 Open in canvas | 원본 비트맵 레이어와 선택 항목의 메타데이터 |
| 생성 이미지 여러 장 | 생성 이력·결과 갤러리·고급 생성의 Select → 항목 선택 → Edit (개수) | 하나의 `.iisc` 문서 안의 독립적인 대지들 |

모바일 Home의 파일 카드 클릭과 다중 선택을 Main에 연결한다. 모바일 Storage는 내부 전체 파일 탐색, Tools는 Editor 진입, Home은 Home 스크롤 복귀를 수행한다. 영상은 기존 영상 결과 화면으로 이어지며 이미지 다중 선택에 포함하지 않는다.

Home 모델의 로컬 경로는 `QUrl::fromLocalFile()`로 변환해 전달한다. 공백, `%`와 `#`가 포함된 파일명을 URL의 쿼리·프래그먼트로 해석하지 않는다. Editor 진입과 고급 생성 화면 전환 중에 숨겨진 Home 카드는 미리보기 로딩을 중단하며, 카드 모델과 선택 상태는 유지한다.

## 여러 이미지의 프로젝트 구성

`EditorProject::openImages()`는 모든 이미지를 검증한 뒤 한 프로젝트로 채택한다. 각 이미지는 단일 iiSharedCanvas 문서 안에서 원본 치수의 대지와 독립적인 fill 레이어를 갖는다. Editor는 대지를 수평으로 표시하며 가로 스크롤, 대지 이름 선택, 이전·다음 버튼과 Alt+Left/Right, Page Up/Down으로 탐색한다. 편집 이력은 문서 전체에서 공유한다.

동일한 실제 파일 경로는 중복 제거하며 최대 1000개, 누적 64×1024×1024 픽셀을 가져올 수 있다. 실패하면 이전 프로젝트를 유지한다. 전체 프로젝트를 `.iisc`로 저장한다. 기존 `.iiscp`는 원본을 보존하면서 대지 문서로 변환하여 연다. 자세한 파일 구성과 복수 선택 규칙은 [여러 캔버스 프로젝트](MultiCanvasProject.md)에 정리한다.

## 선택과 돌아가기

선택 상태는 작업 ID가 아니라 원본 이미지 경로를 기준으로 유지한다. 같은 생성 작업이 여러 출력 이미지를 반환해도 각각 선택할 수 있다. 새 결과가 추가되면 기존 선택을 유지하고, 목록에서 사라진 항목만 선택에서 제거한다. Select 모드에서는 카드를 누르면 선택을 토글한다. 결과 갤러리는 Cmd/Ctrl·Shift 클릭과 키보드 복수 선택도 지원한다. 선택 모드의 Cancel은 선택 모드와 선택을 초기화한다. 기본 모드의 생성 이력 카드 클릭은 한 장을 바로 연다. 결과 갤러리는 클릭으로 이미지를 선택하고 더블 클릭으로 상세 보기를 연다.

결과 화면에서 상세 이미지 보기 중 Select를 누르면 갤러리로 돌아가 여러 항목을 선택할 수 있다. 선택 중에 결과가 한 장으로 줄어도 선택 갤러리를 유지한다.

실시간 생성 결과 화면의 상단에는 생성 Cancel과 이미지 Select를 제공하며, 화살표와 New Canvas 버튼은 제공하지 않는다. Select는 결과가 한 장인 경우에도 사용할 수 있고, 완료된 이미지가 없을 때에는 비활성 상태로 표시한다. 생성 Cancel은 선택 모드의 Cancel과 별개이다. 생성 Cancel을 누르면 현재 제출 배치의 대기 작업을 먼저 취소한 뒤 실행 중인 작업의 엔진 취소를 요청하고, 엔진 종료를 기다리지 않고 같은 입력 처리에서 Home으로 돌아간다. 완료된 이미지와 다른 제출의 작업은 유지한다. 생성이 완료되거나 실패한 뒤에도 생성 Cancel로 Home에 돌아갈 수 있다.

하단 QuickGenerate의 Generate는 현재 제출에 대기·실행·호스트 연결·다운로드 중인 작업이 있으면 비활성화된다. Enter와 직접 `submit()` 호출도 같은 상태 검사로 중복 제출을 차단한다. 배치 전체가 완료·실패·취소 상태가 되면 Generate를 다시 활성화한다. Home 복귀는 현재 결과·미리보기·선택을 정리하며, 프롬프트·비율·개수와 참조 이미지 초안은 유지한다.

Editor를 닫으면 진입한 Home 또는 결과 화면으로 돌아간다. 프롬프트 초안, 결과 갤러리와 기존 선택은 유지한다. 파일 선택 대화상자의 취소와 열기 실패는 화면 전환을 일으키지 않는다. 파일 열기 오류는 현재 화면에 표시한다. 생성 작업을 취소하거나 변경하지 않는다.

고급 생성 썸네일은 고정 크기의 LVRS AbstractButton으로 제공한다. 비동기 이미지의 원본 치수가 바뀌어도 입력 영역은 64px로 유지하며, 선택 테두리와 키보드 포커스를 제공한다.

## 검증

- `Dreamscapes.EditorProject`와 `Dreamscapes.MultiCanvasGui`: 캔버스별 치수·픽셀·편집·보기와 실행 취소 이력, 수정 키 선택, 페이지 전환, 전체 저장·재개방과 실패 시 프로젝트 유지 여부를 검사한다.
- `Dreamscapes.HomeEditorRoutes`: 데스크톱·모바일의 실제 Open file 버튼과 대화상자 콜백, 최근 파일 카드, 새 캔버스 버튼, 생성 이력 다중 선택, 상세 보기에서 결과 갤러리 다중 선택으로의 전환과 Editor 복귀를 검사한다. `advancedCanvasMatchesResolutionAndStreamsItsBatch`도 포함해 생성된 고급 배치의 여러 이미지 선택과 Editor 전달을 검사한다.
- 기존 EditorCanvasGui, DesktopHome, NewCanvasGui, QuickGenerate 검사로 단일 이미지 경로와 초안·결과·선택 보존을 함께 확인한다.
- `resultCancelsTheActiveGeneration`은 데스크톱·모바일 레이아웃의 대기 배치, 모델 로딩, 미리보기, 네이티브 콜백 실행 중 취소를 검사한다. 취소 즉시 Home 표시, 배치 전체 취소, 중복 클릭·Enter 차단, 초안 보존과 늦은 콜백으로 결과가 다시 열리지 않는 동작을 검증한다. 네이티브 콜백은 취소 플래그를 수신한 뒤 테스트가 해제할 때까지 종료를 보류하여 Home 전환이 엔진 종료를 기다리지 않는지 확인한다.
- `Dreamscapes.LiveGeneration`은 위 취소 검사와 미리보기 갱신·완료 후 Generate 재활성화·다른 제출 보존·단일 이미지 Select 편집·읽을 수 없는 이미지의 편집 실패를 함께 실행한다. GUI의 Society 임시 저장소는 현재 SDK의 `.societycontainer` 패키지 경계에 맞춘다.

```sh
cmake -S . -B build
cmake --build build --parallel 4
ctest --test-dir build -R '^Dreamscapes\.(HomeEditorRoutes|EditorCanvas|EditorCanvasGui|DesktopHome|NewCanvas|NewCanvasGui|QuickGenerate|LiveGeneration)$' --output-on-failure
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_DISABLE_DISK_CACHE=1 build/bin/DreamscapesGuiTests advancedCanvasMatchesResolutionAndStreamsItsBatch
```

GUI 자동 입력은 macOS 빌드의 데스크톱·모바일 레이아웃에서 실행한다. iOS·Android의 실제 시스템 파일 선택기와 기기 입력은 별도 실기 검증 대상이다. 검증 로그와 캡처·네이티브 문서는 `build/home-editor/`에 보관한다.

2026-10-03 초기 단일 문서 연결 검증에서는 관련 CTest 9개를 통과했다. 여러 캔버스 프로젝트로 변경한 이후의 최종 검사·설치 결과는 `build/multi-canvas/`에 기록한다. 최종 다중 선택 보완 후 HomeEditorRoutes를 다시 실행해 데스크톱·모바일 문서 저장과 재개방을 확인하고, 렌더링된 캔버스 중앙 픽셀이 SDK의 문서 렌더링 결과와 일치한 뒤 화면을 캡처했다. 고급 생성 배치 선택 검사와 Cocoa 플랫폼의 실제 번들 기동 검사도 별도로 통과했다.

offscreen 검사 로그에는 Qt Labs Platform의 네이티브 메뉴 지원 진단과 LVRS CardImageContent의 `drawImage(), index size error` 경고가 발생했다. 이 진단이 남아 있음을 기록하며, 네이티브 캔버스의 저장·표시 결과는 실제 픽셀 비교로 확인한다. 새 모델을 이용한 AI 추론이나 모바일 실기 실행을 이 경로 검증으로 주장하지 않는다.
