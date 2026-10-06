<a id="new-canvas-chooser"></a>

# 새로운 캔버스 선택기

홈의 **새 캔버스** 작업과 플랫폼 **새** 단축키는 `NewCanvasDialog.qml` 을 엽니다. 선택기는 설치된 LVRS `LabelButton`, `MenuItem`, `InputField`, `LabelMenuButton`, 및 `ContextMenu` 를 사용하여 Dreamscapes 홈의 Figma 새 캔버스 보드를 구현합니다. 이 작업 컨트롤은 LVRS 의 기본, 호버, 누름, 해제 리바운드, 및 키보드 포커스 동작을 유지합니다. 프리셋 정보는 `ListItem.Navigation` 의 분리된 외관을 사용하며, 버튼 동작 없이 LVRS 레이블로 구성됩니다. 모달 호스트는 Qt 퀉 컨트롤의 포커스와 Escape 처리를 사용합니다.

<a id="design-and-catalogue"></a>

## 디자인 및 카탈로그

- Figma 파일: `bn8O4AHKr1X9DWnhR1TgEy`, 홈 페이지 `0:1`.
- [동영상 참조](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=302-9026).
- [맞춤 크기 참조](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=310-1970).
- 런타임 카탈로그: `src/App/Views/Home/Data/canvas-presets.json`, Figma 카탈로그를 기반으로 2026-10-06 플랫폼 제거를 반영하였다. 289개 프리셋을 14개 카테고리에 포함한다. 소셜 미디어 카테고리는 X, Threads, Facebook, Bluesky, Mastodon 등을 포함한 13개 플랫폼의 58개 포맷으로 구성된다. 카운트는 의도적으로 카테고리에 따라 다릅니다.
- JSON 는 소스 URL, 확인 날짜 및 `documented-recommendation` / `starter` 분류를 유지합니다. 이는 모든 플랫폼이 해당 크기를 정확히만 허용한다는 주장이 아닌 시작 캔버스 차원입니다. 이 파일과 회귀 체크를 함께 업데이트하세요.
- 검색은 모든 카테고리를 포괄하며 이름, 플랫폼, 카테고리, 섹션, 네이티브 차원 및 픽셀 차원과 일치합니다. `A4`는 11의 기존 프리셋을 반환합니다. 빈 검색 결과는 복구 작업을 표시하고 생성을 비활성화합니다.

### 2026-10-06 플랫폼 프리셋 제거

TikTok 3개, KakaoTalk 4개, LINE 4개를 제거하였다. 한국 기업에서 출발한 서비스까지 포함하는 사용자 요청의 범위에 따라 네이버 계열에서 출발한 LINE도 제거 대상으로 적용하였다. 기업 계보의 근거는 [네이버 공식 연혁](https://www.navercorp.com/en/company/history)이다. 현재 전체 카탈로그는 14개 카테고리·289개 프리셋이며, 소셜 미디어는 13개 플랫폼·58개 프리셋이다.

삭제한 ID `01-07-*`, `01-15-*`, `01-16-*`는 다시 선택할 수 없다. 나머지 프리셋의 ID, 이름, 크기, 단위, PPI 및 소스 정보는 유지하였다. 검색은 기존의 전역 키워드 규칙을 유지하므로 `Timeline`과 같은 일반 캔버스도 검색할 수 있다.

[Figma 소셜 미디어 보드](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=302-8548)에서도 해당 세 그룹과 11개 카드를 삭제하였다. 17개 새 캔버스 보드의 전체 개수 표기는 289개이며 소셜 미디어 표기는 58개이다. 삭제 후 세로 레이아웃도 남은 콘텐츠에 맞추었다. 이번 변경의 카탈로그·Figma·네이티브 검증 자료는 `build/new-canvas-preset-removal/`에 기록한다.

최종 macOS Release 빌드에서 `Dreamscapes`, `DreamscapesNewCanvasTests`, `DreamscapesGuiTests` 대상이 모두 성공하였다. 배포 스크립트의 `codesign --verify --deep --strict` 검사도 통과하였다. `Dreamscapes.NewCanvas`는 7개 항목, `Dreamscapes.NewCanvasGui`는 19개 항목을 통과하였으며 실패 및 건너뛴 항목은 없다. GUI 검사는 offscreen·software 환경에서 수행하였다. `build/` 아래 앱은 canonical `build/bin/Dreamscapes.app` 하나이며 중첩 앱은 없다. 초기 패키징 실패의 원인을 확정하지 않은 상태에서 재빌드가 성공한 것이므로, 실패 로그도 검증 자료에 보존하였다. `/Applications/Dreamscapes.app` 설치본은 교체하지 않았고 기존 실행 중 앱을 중단하지 않았다.

<a id="layout-and-interaction"></a>

## 레이아웃과 상호작용

모달의 모든 가장자리에는 12px 패딩이 있으며, 이는 콘텐츠 프레임과 일치합니다.
[LVRS ApplicationWindow](https://www.figma.com/design/0GkItQYSNIR0lZ3iJhfJzc/Layerd-Visual-Render-System?node-id=997-3)입니다. `Main.qml` 는 사용 가능한 배치 영역으로 `appContent` 를 제공합니다. 중앙 정렬, 최대 크기 및 크기 조정 동작은 해당 영역에 이미 적용된 제목 막대 예약, 모바일 안전 영역, 및 키보드 인셋을 존중하며, 콤팩트 팝업은 이제 창 상단 컨트롤을 덮지 않습니다.

각 프리셋의 44px 정보 행은 기본 ListItem 레이아웃과 일치합니다: 12px 가로 인셋, 본문 및 캡션 레이블, 및 4px 텍스트 간격입니다. 호버, 누름, 해제 애니메이션 또는 탭 정지점이 없습니다. 포함된 카드가 포인터 선택, 접근성 및 Space/Enter 활성화 소유합니다. 선택 및 키보드 초점이 카드 경계에서 표시됩니다. 키보드 초점이 뷰포트 레이아웃이 완료된 후 카드를 화면에 표시하고 뷰포트 크기 변경을 따릅니다. 뷰포트 가 카드보다 짧으면 정보 행이 계속 표시됩니다. Figma 의 17 검토 보드와 [가 제공하는 예시](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=300-29539) 는 이러한 정보 행에 대해 상속된 프로토타입 반응을 사용하지 않는 일반 분리 프레임을 사용합니다.

도구 모드가 검색, 사용자 지정 크기 및 닫기를 유지합니다. 데스크톱은 공간이 허용될 때 200픽셀 범주 메뉴, 3열 사전 설정 갤러리 및 268픽셀 세부 정보 패널을 사용합니다. 갤러리와 세부 정보는 창 내에서 독립적으로 스크롤됩니다. 푸터가 선택 정보, 취소 및 캔버스 생성을 표시합니다. Figma 의 긴 검토 프레임은 런타임 창 높이보다 짧지 않습니다. 1000 픽셀 이하에서 범주 드롭다운이 사이드바를 대체하며, 갤러리와 세부 정보는 하나의 스크롤 가능한 본문에 쌓이고 한계가 설정된 도구 모드와 푸터를 가집니다. 갤러리와 세부 정보 대리자는 선택기가 표시되는 동안만 로드되므로 홈 시작 시 범주의 미리보기 카드를 인스턴스화하지 않습니다.

프리셋을 선택하면 너비, 높이, 단위 및 PPI 이 공급됩니다. 차원을 편집하면 커스텀 선택을 생성합니다. 단위 변경은 기존 차원을 변환하며, 같은 숫자를 다시 해석하지 않습니다. 물리적 단위는 mm 와 인치를 지원하며, `round(mm / 25.4 * PPI)` 은 정확한 반 픽셀에서 가장 가까운 짝수 로 반올림하여 작성된 카탈로그와 일치하는 픽셀을 생성합니다. A4 는 300 PPI 에서 2480 × 3508 픽셀입니다. 픽셀 차원은 정수여야 합니다. 입력은 유한하고 양수이며, 축당 최대 32768 픽셀, 총 268435456 픽셀이어야 하며, PPI 는 양수여야 하고 최대 2400이어야 합니다. 모든 289 설계된 프리셋은 해당 범위에 맞습니다.

탭은 네이티브 LVRS 컨트롤과 프리셋 카드에 순차적으로 이동하며, 스페이스/엔터는 메뉴 및 카드 선택을 활성화합니다. 플랫폼 찾기 단축키는 선택기의 검색을 초점화하고, Ctrl+Return 은 유효한 캔버스를 생성하며, Escape 은 선택기를 해제합니다. 취소는 홈 프롬프트와 현재 편집기 문서를 보존합니다. 생성된 이미지를 선택하면 편집기에 직접 원래 이미지와 메타데이터를 엽니다.

<a id="canvas-document"></a>

## 캔버스 문서

`EditorCanvas` 는 `iiSharedCanvas::CanvasItem` 의 소비자 어댑터이며, 해결된 픽셀 범위에 유한한 SDK `Document` 을 생성합니다. 흰색과 검은색 배경은 편집 가능한 벡터 레이어로, 인쇄 크기에 대한 큰 초기 비트맵 를 피합니다. 투명한 문서는 배경 레이어 없이 시작됩니다. 편집기는 사전 설정된 정체성, 물리적 치수, PPI, 배경을 유지하며 사용 가능한 뷰포트 에 문서를 맞춥니다. 선택기가 닫히기 전에 생성이 성공합니다. 잘못된 입력은 현재 문서를 그대로 유지합니다.

선택기는 메모리 내 편집기 문서를 생성한다. 이후 `.iisc` 작업 파일 저장과 실제 보기·편집 연결은 [EditorNativeCanvas.md](EditorNativeCanvas.md)에 정의되어 있다. 선택기 자체는 새 프로젝트가 Society에 유지되었다고 주장하지 않는다.

<a id="verification"></a>

## 검증

`Dreamscapes.NewCanvas` 는 모든 289 정체성과 치수, SNS 섹션, 전역 검색, 물리적 변환, 잘못된 값, SDK 문서 배경을 확인합니다. `Dreamscapes.NewCanvasGui` 는 데스크톱, 콤팩트, 모바일 창 크기, 생성, 빈 검색, 키보드 선택, 취소, 초안 유지, 결과 이미지 라우팅 및 기존 모바일 편집기 도구 모음을 확인합니다. 원본 카탈로그 도입 당시의 증거는 `build/new-canvas-verification/` 하위에 저장되어 있다. 2026-10-06 제거 변경의 최종 결과는 `build/new-canvas-preset-removal/verification-result.json`에 기록하였다. `newCanvasPaddingAndStaticDetails` 는 12px 패딩, 앱 콘텐츠 내 배치, 크기 조정, 분리된 레이블 기하학, 변경되지 않은 호버/누름/방출 외관, 데스크톱, 콤팩트, 모바일 및 최소 너비에서의 카드 키보드 선택을 확인합니다. 이 버전의 증거는 `build/new-canvas-static-verification/` 하위에 저장됩니다. `packagedApplicationStarts` GUI 테스트는 `DREAMSCAPES_TEST_APP_PATH` 를 해당 `Contents/MacOS/Dreamscapes` 실행 파일로 설정하여 설치된 번들을 또한 확인할 수 있습니다. 그것은 프로덕션 등록, 루트 로딩, 하나의 창 및 Society 헬퍼 핸드셰이크를 확인합니다.
