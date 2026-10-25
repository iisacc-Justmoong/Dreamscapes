# 최소 조작 에디터 레이아웃

데스크톱 에디터는 Figma `353:22614`의 2026-10-25 수정본을 따른다.
변경 범위는 패널 경계, 작은 도구 버튼과 최소형 타임라인이다. 실제 캔버스,
도구 설정과 타임라인의 문서 모델 및 편집 명령은 기존 경로를 사용한다.

## 패널 경계의 소유권

패널마다 네 변을 모두 칠하면 맞닿은 경계가 두 논리 픽셀이 된다.
`PanelFrame`의 `topEdge`, `rightEdge`, `bottomEdge`, `leftEdge`로 각 변의
소유권을 지정한다. 기본값은 모두 true이므로 독립 패널의 기존 계약을 유지한다.
색상은 LVRS `Theme.accentGrayMuted`, 폭은 한 논리 픽셀, 패널 반경은 0이다.

| 경계 | 한 번만 그리는 주체 |
|---|---|
| 에디터 작업 영역 외곽 | `panelFrame_editor-body` |
| 캔버스·타임라인 열과 세로 툴바 사이 | `panelFrame_editor-canvas`의 우측 |
| 툴바와 우측 Inspector 사이 | `panelFrame_tools`의 우측 |
| 캔버스와 타임라인 사이 | `panelFrame_editor-timeline`의 상단 |
| 여러 페이지의 목록과 캔버스 사이 | `panelFrame_pages`의 상단 |

보기 옵션 바, 공통 사이드바와 Home에는 에디터 외곽선을 씌우지 않는다.
보기 옵션 아래의 전체 작업 영역을 기준으로 도구 모음과 Inspector가 정렬된다.
기본 Inspector 폭은 Figma의 354px이며 기존 크기 조절과 접기·펼치기를 유지한다.
장식은 레이아웃 자식으로 참여하거나 입력을 가로채지 않는다.

## 툴바

데스크톱의 18개 도구는 LVRS `IconButton`을 사용한다. 이는 `PushButton`의
아이콘 모드이며 기본 크기 22×22px, 아이콘 슬롯 18px를 그대로 사용한다.
30px 도구 모음에 좌우 4px 여백을 둔다. 기존 유색 아이콘은 Figma의 18px
컴포넌트 원본을 SVG로 내보내 `Assets/tool-*.svg`에 저장했다. 내부 16px
벡터와 여백을 유지하므로 버튼 내부에서 원본 도형의 비율을 변경하지 않는다.

선택·hover·press는 버튼 상태로 표현하며 `tone=Default`를 변경하지 않는다.
기존 `EditorButtonState`를 공유하여 선택을 Default의 pressed 표면에 연결한다.
LVRS의 기본 checked 강조색으로 우회하지 않으며 선택의 소유자는 도구 목록이다.
방향키, Home/End, 스크롤, 도구별 설정 보존, 같은 도구를 다시 눌러 Inspector를
접는 동작 및 LVRS Tooltip을 유지한다. 모바일은 기존 레이블이 있는 가로 탭과
도구 Sheet를 사용한다. 전역 Theme 스케일이나 LVRS 기본 컨트롤 값을 덮어쓰지 않는다.

## 타임라인

실제 앱의 데스크톱 타임라인은 180px이며 36px 조작 행, 32px 눈금과 나머지
레이어 영역으로 구성한다. 넓은 화면에서 거터는 192px이다. 좁은 화면은
거터를 축소하고 한 행을 유지하며, 트랙이 많으면 세로로 스크롤한다.
눈금 범위·배율·프레임레이트·레이어 수는 실제 문서와 뷰 상태에서 얻는다.
Figma 예시의 트랙이나 클립을 앱에 생성하지 않는다.

이전 프레임·재생/일시정지·다음 프레임, 분할, 삭제, Fit과 레이어 추가는 바로
조작한다. `…`의 Timeline options에는 기존 조작 행들을 동일한 상태 모델에
연결한 채 표시한다. 가로로 긴 행은 옵션 영역 내부에서 스크롤할 수 있다.

- 처음/마지막 프레임, 이전/다음 컷, 직접 프레임 입력, ±1 프레임
- Layers/Keyframes/Curves, Undo/Redo, Exclude, Ripple delete
- Select/Blade/Trim/Ripple/Roll/Slip/Slide/Range, In/Out
- All/Kept/Out, Snap, 링크 이동, 배율, FPS와 선택 명령
- In/Out/Duration/Source, 프레임 단위 트림, Keep/Exclude/Solo/Lock/Duplicate
- 키프레임 위치·값·삭제, 트랙 이름, 상태·오류·뷰 범위

레이어·오디오 추가, 프레임 소스 선택과 기존 트랙 메뉴도 유지한다.
플레이헤드는 원본 Figma SVG와 같은 파란색 `#548af7`을 사용하며 드래그는 기존 C++ scrub 명령을
호출한다. 재생·클립 이동·트림·키프레임·곡선·오디오 경로를 대체하지 않는다.
옵션 입력 필드의 유리 표면은 안정적인 ApplicationWindow 배경을 공유하므로,
행을 옵션 창으로 배치할 때 입력 필드의 부모 탐색과 배경 판정이 서로 재평가되지 않는다.

눈금 높이는 `EditorTimelineModel`과 `EditorTimelineViewport`의 `rulerHeight`에
같이 전달한다. 기본값 44px는 기존 독립 사용자를 위해 유지하고, 앱의 최소형
타임라인만 32px를 요청한다. 이 속성 변경은 표시용 행의 위치와 클릭 판정만
갱신한다. 파일 저장, 문서 revision, Undo 기록과 생성 요청을 실행하지 않는다.

## 검증

`Dreamscapes.PanelFrames`는 공유 경계가 한 픽셀인지 실제 렌더링으로 검사하고,
22px 도구 버튼과 18px 원본 SVG의 로드, 추가 옵션의 기존 명령, 옵션 열기·닫기와
모드 전환의 문서 불변성을 검사한다. `Dreamscapes.TimelineViewport`는 32px
눈금 아래의 클립 클릭이 눈금 클릭으로 판정되지 않는지 검사한다.
`Dreamscapes.EditorLayerTimeline`은 눈금 높이 변경의 파일·revision·Undo 불변성과
기존 레이어·재생·스크럽·편집 동작을 검사한다.

실행과 설치 기록은 `build/editor-compact-20261025/`에 보관한다.

## 2026-10-25 설치 검증 기록

macOS Release 빌드를 완료하고 `/Applications/Dreamscapes.app`을 같은 버전·빌드
`0.1.1`로 교체했다. 배포 후보, 임시 설치본과 실제 설치본의 서명 및 전체 파일
일치를 확인했다. 이전 설치본과 열려 있던 IISC는 각각 ZIP으로 백업했다.

기존 프로세스 66576을 정상 종료한 뒤 설치본의 새 프로세스 44419에서 같은
IISC를 다시 열었다. 작은 툴바, 최소형 타임라인, 프레임 이동·재생·정지,
옵션 열기와 Keyframes/Layers 전환, Inspector 접기·펼치기를 실제 화면에서
확인했다. 확인 후 원본 IISC의 SHA-256은 작업 전과 동일했다.

네이티브 타임라인·뷰포트·오디오 검사는 통과했다. 캔버스 표시 검사는 첫 실행의
60초 시간 제한 이후 같은 제한의 단독 재실행으로 통과했다. 공유 경계 렌더링,
18개 버튼의 Default/pressed 상태와 SVG, 옵션의 문서 불변성 검사도 통과했다.
데스크톱 배치 검사는 120초 제한 안의 단독 재실행으로 통과했으며, 툴바 키보드·
스크롤과 패널 크기 조절 검사도 통과했다. 타임라인의 여섯 화면 크기 배치,
레이어 관리와 편집·재생 동작은 각 검사 로그에서 완료를 확인했다.

일부 GUI 묶음 검사와 모바일 자동화는 초기 로딩 중 120초 제한을 초과했다.
따라서 전체 회귀 검사 및 모바일 검증이 모두 완료되었다고 간주하지 않는다.
통과 항목, 시간 제한 항목, 설치 결과와 실행 화면은
`build/editor-compact-20261025/verification-summary.json`에 구분해 기록했다.
