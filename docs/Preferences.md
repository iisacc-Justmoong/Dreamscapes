# 환경설정

데스크톱 글로벌 메뉴의 `Edit → Preferences…` 또는 macOS의 `⌘,`, Windows/Linux의 `Ctrl+,`로 별도의 LVRS 환경설정 창을 연다. 현재 macOS의 네이티브 메뉴 표시명은 `Edit → Settings…`이다. Home 툴바의 설정 버튼도 같은 창을 사용한다. 창은 한 번 생성하고 재사용한다. `Escape`, `⌘W` 또는 닫기 버튼으로 숨기며, 마지막으로 선택한 항목을 유지한다. 주 창을 닫으면 환경설정 창도 닫힌다. 모바일에서는 별도의 데스크톱 창을 생성하지 않는다.

## 항목

환경설정의 모든 시각 구성 요소는 설치된 LVRS를 사용한다. 레이아웃은 `LV.HStack`, `LV.VStack`, `LV.Spacer`로, 스크롤은 `LV.List` 기반 `PreferenceList`로 구성한다. 앱 뷰에서 Qt Quick Controls, Qt Quick Dialogs, 원시 Rectangle·Flickable·ListView·RowLayout·ColumnLayout·Flow를 직접 사용하지 않는다. `QtQuick.Layouts`는 LVRS 스택의 `Layout` 부착 속성에만 사용한다.

폴더 선택은 `SocietyFolderPicker`의 `LV.Modal`, `LV.InputField`, `LV.List`, `LV.ListItem`, LVRS 버튼으로 구성한다. 하위 폴더 탐색, Up, 경로 직접 입력, 현재 폴더 선택, Cancel과 Escape를 제공한다. 목록 로딩 중에는 행 선택을 차단하며 각 행이 가진 폴더 URL로 이동한다. 폴더 선택은 입력란만 갱신하며 Apply에서 기존 드라이브 검증·저장을 수행한다. 취소하면 입력란과 연결 상태를 유지한다. 파일시스템 데이터는 시각 요소가 없는 [FolderListModel](https://doc.qt.io/qt-6.8/qml-qt-labs-folderlistmodel-folderlistmodel.html)이 공급한다.

사이드바는 [Figma 지정 노드](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=353-5587)의 207px 폭, 10px 패딩과 12px 그룹 간격을 따른다. 맨 위 Account는 44px `LV.ListItem.Navigation` 프로필 행이며, 디자인의 `Display Name`과 `@user_id` 샘플 텍스트를 표시한다. 나머지는 24px `LV.MenuItem`이며 18px LVRS 아이콘과 Pretendard Body를 사용한다. 사이드바 내용은 424px이며 작은 창에서는 세로 스크롤로 모든 항목에 접근한다. 키보드 포커스가 이동하면 해당 행을 화면에 표시한다.

아이콘은 각 Figma 행의 `Icon` 인스턴스 스왑 값과 원본 컴포넌트 이름을 읽어 설치된 LVRS 아이콘셋의 동일 이름에 대응한다. 별도의 SVG 복제나 공통 사각형 자산을 사용하지 않는다.

| 메뉴 | Figma 컴포넌트 / LVRS iconName |
| --- | --- |
| General | `settings` |
| Appearence | `stroke` |
| Storage | `sqlFile` |
| Image | `imageToImage` |
| Video | `render-preview` |
| Audio | `audioClassification` |
| Agents | `reinforcementLearning` |
| Share | `cwmShare` |
| Intergration | `persistenceRelationship` |
| Publish | `export` |
| About | `statusinfo` |
| Accessibility | `accessMethod` |
| Keyboard Shortcut | `keyboard` |

Account 사진은 `GenerationController.account`가 제공하는 iiAccountManager의 `Account.avatarUrl`에 바인딩한다. 생성 컨트롤러가 소유한 하나의 SDK `AccountSession`을 Society 클라이언트와 환경설정이 공유하므로 사진용 계정 세션을 별도로 만들지 않는다. 세션 복원은 기존 SDK의 보안 저장소 계약을 사용한다. SDK가 사진 값을 변경하면 열린 창에서도 갱신하며, 사진 값이 없거나 계정이 해제되면 LVRS `user` 아이콘으로 복귀한다. 계정 객체는 읽기 전용 프로필 스냅샷이며 인증 정보는 뷰에 노출하지 않는다. 이름과 User ID 텍스트는 이번 아이콘 변경 범위에서 디자인 샘플을 유지한다.

메뉴 그룹은 다음 순서이다. 표시명 `Appearence`와 `Intergration`은 Figma의 원문을 그대로 유지하며 내부 카테고리 ID는 `Appearance`, `Integration`이다.

사이드바와 상세 패널은 LVRS 창 드래그 영역 및 네이티브 창 버튼 아래에 배치하여 상단 프로필과 macOS 창 버튼이 겹치지 않도록 한다. 내부 10px 패딩과 그룹 간격은 유지한다.

1. General, Appearence, Storage
2. Image, Video, Audio, Agents
3. Share, Intergration, Publish
4. About, Accessibility, Keyboard Shortcut

| 항목 | 내용 |
| --- | --- |
| Account | Society에서 iisacc 계정을 관리하는 안내와 Open Society 버튼 |
| Storage | 기존 Society 드라이브 위치 확인, 폴더 선택과 Apply |
| Image | Default image generation model 드롭다운, Default prompt, Default negative prompt |
| Video | Default video generation model 드롭다운, Default prompt, Default negative prompt |
| Audio, Agents | 해당 유형의 Default prompt, Default negative prompt |
| 기타 항목 | 선택 상태와 해당 카테고리 제목 표시. 추가 설정 기능은 이 사이드바 변경 범위에 포함하지 않는다. |

Image와 Video의 드롭다운은 `LV.ComboBox`와 스크롤 가능한 `LV.ContextMenu`로 구성한다. 마우스 클릭 또는 포커스 상태에서 Space/Enter/아래 방향키로 열고, 방향키와 Home/End로 이동하고 Enter로 선택한다. 모델 이름을 표시하되 ID를 저장하므로 같은 표시 이름을 가진 모델도 구분한다. 이미지 목록은 기존 이미지 생성 모델 목록을, 비디오 목록은 기존 LTX 비디오 모델 목록을 사용한다. VAE는 기본 생성 모델 선택에 포함하지 않는다.

Image, Video, Audio, Agents에는 기본 프롬프트와 기본 네거티브 프롬프트를 이 순서로 세로 배치한다. 두 입력란은 QuickGenerate의 공통 `PromptField`를 재사용한다. LVRS의 22px 한 줄 높이·패딩을 유지하며, 폭을 채우면 단어 또는 긴 문자열을 다음 줄로 감싸고 내용 높이만큼 확장한다. 텍스트 삭제 시 한 줄 높이로 복귀한다. 키보드 입력·붙여넣기는 즉시 저장하며, 편집 커서는 LVRS 목록의 화면 안으로 스크롤된다. 각 입력란은 32,000자까지 받는다.

## 저장과 적용

각 선택은 즉시 저장하며 현재 생성 모델도 갱신한다. 저장이 실패하면 기본값과 현재 선택을 그대로 유지하고 현재 Image 또는 Video 패널에 오류를 표시한다. 기본값은 OS별 앱 설정 위치의 `generation-models.conf`에 UTF-8 모델 ID로 저장한다. 파일 읽기·쓰기는 Qt에 의존하지 않는 C++23 `ModelPreferences`가 수행하며, 임시 파일 쓰기 후 rename으로 이전 설정을 대체한다. `GenerationRuntime::modelPreferencesFile`로 테스트용 경로를 주입한다. 빈 경로는 메모리에서만 설정을 유지한다.

프롬프트도 같은 파일에 유형별로 독립 저장한다. 새 설정의 네 유형 모두 프롬프트와 네거티브 프롬프트는 빈 문자열이다. 비디오에 예시 네거티브 프롬프트를 자동으로 채우지 않는다. 버전 2 형식은 두 모델 ID와 Image·Video·Audio·Agents 순서의 프롬프트/네거티브 프롬프트 쌍을 보관하며, 따옴표·역슬래시·개행·UTF-8 텍스트를 유지한다. 기존 `Dreamscapes generation models 1` 파일은 읽을 수 있으며, 설정을 변경할 때 모델 ID를 보존한 채 버전 2로 저장한다. 기존에 저장된 사용자의 프롬프트는 유지한다. 파일 크기는 1MiB로 제한한다. 저장 실패 시 파일과 메모리 기본값은 이전 값을 유지하고 현재 생성 설정 패널에 오류를 표시한다.

QuickGenerate는 비어 있거나 이전 기본값 그대로인 프롬프트에 현재 Image/Video 기본값을 적용한다. 사용자가 작성한 프롬프트는 설정 변경이나 유형 전환으로 덮어쓰지 않는다. Image·Video 작업 공간은 새 초안과 Reset에 두 기본값을 적용하며, 기존 초안과 저장된 프리셋은 유지한다. QuickGenerate의 기본 네거티브 프롬프트는 제출 시 작업에 복사하여 로컬 네이티브/프로세스 생성에 전달한다. 명시적 고급/비디오 초안의 빈 네거티브 프롬프트도 그대로 사용한다. 이미 제출한 작업은 이후 설정 변경에 영향을 받지 않는다. Audio·Agents는 설정 저장까지 제공하며, 이 변경으로 해당 생성 실행 화면이나 백엔드를 추가하지 않는다. Society 호스트의 네거티브 프롬프트 적용은 별도 호스트 기능 범위이다.

앱 시작 또는 Society 드라이브 연결 시 저장된 기본 모델을 우선한다. `Automatic`은 해당 유형의 첫 모델을 사용한다. 저장된 모델이 목록에서 사라지면 ID를 보존한 상태로 사용 가능한 첫 모델을 사용하며, 드롭다운에는 `Unavailable: <model ID>`를 표시한다. 모델이 다시 나타나면 기본값을 복원한다. 해당 유형의 모델이 없으면 선택을 비우고 드롭다운을 비활성화한다. 저장된 누락 모델이 있으면 Automatic으로 초기화할 수 있다.

Home 또는 고급 생성 화면에서 지정한 모델은 현재 세션의 선택이다. 이 선택은 기본값 파일을 바꾸지 않고 모델 목록 새로고침으로 덮어쓰지 않는다. 고급 생성의 명시적 모델 지정도 유지한다. 환경설정에서 기본값을 바꾸면 현재 세션에도 새 기본값을 적용한다. 이미 제출된 작업은 제출 시점의 모델 참조를 유지하므로 실행 중이거나 대기 중인 작업의 모델을 변경하지 않는다.

## 검증

- `Dreamscapes.Preferences`: Account 최상단 배치, 13개 메뉴의 순서·크기·그룹 간격·LVRS 아이콘 이름과 SVG 디코딩, 모든 항목 전환, 작은 창에서 키보드 포커스 스크롤, 이미지·비디오 패널 분리, 키보드 드롭다운 선택과 실제 생성 컨트롤러 적용, 창 재사용을 검사한다.
- `preferencesPromptsWrapAndPersist`: 네 생성 유형의 두 입력란, 720/560px 창 폭에서 한글·영문 줄바꿈, 자동 확장/축소, 세로 배치, 편집 커서 스크롤, 붙여넣기·삭제의 즉시 저장과 새 컨트롤러 복원을 검사한다. `generationPromptDefaultsSeedNewDrafts`는 QuickGenerate와 Image/Video 새 초안·Reset의 기본값 적용 및 작성한 초안 보존을 검사한다. `DREAMSCAPES_PROMPT_PREFERENCES_SCREENSHOT`으로 실제 설정 창의 PNG를 저장한다.
- `preferencesAccountAvatarFollowsSdk`: 실제 SDK Account 객체의 Main → Preferences 연결, 루프백 HTTP로 제공한 WebP 사진 표시·교체, 계정 해제 후 `user` 복귀와 18px 슬롯을 검사한다. 실제 사용자 로그인·원격 프로필 변경은 수행하지 않는다. GUI 테스트는 보안 저장소의 실제 세션 복원을 비활성화한다.
- `preferencesLvrsViewsAndFolderPicker`: 외부 UI 구성 요소 재도입을 금지하는 소스 계약, LVRS 폴더 목록 탐색, 공백·특수 문자가 포함된 폴더 URL, Up, Escape 취소, 폴더 선택·Apply를 검사한다. 모델 메뉴는 32개 모델로 End 선택과 스크롤을 확인한다.
- `DreamscapesGuiTests mainCreatesOneSharedWindow`: Edit 메뉴 소속, 환경설정 단축키, Storage의 기존 드라이브 설정과 창 닫기 동작을 네이티브 창에서 검사한다.
- `Dreamscapes.Generation`: 기본 모델 저장·재생성 후 복원, 세션 선택 보존, 누락 모델의 대체·복원, Automatic 초기화, 잘못된 유형 거부, 저장 실패와 손상된 설정 처리, 기존 대기 작업 보존을 검사한다.
- `defaultPromptsPersistAndReadLegacyModels`, `defaultPromptSaveFailureAndQueuedSnapshot`: 네 유형의 긴 UTF-8·따옴표·역슬래시·개행 복원, 모델 설정 버전 1 호환, 빈 문자열 저장, 잘못된 유형/과도한 길이 거부, 저장 실패 시 원본 보존, 제출 시 네거티브 프롬프트 스냅샷을 검사한다.

`DREAMSCAPES_SIDEBAR_PREFERENCES_SCREENSHOT`, `DREAMSCAPES_IMAGE_PREFERENCES_SCREENSHOT`, `DREAMSCAPES_GENERATE_PREFERENCES_SCREENSHOT`, `DREAMSCAPES_FOLDER_PREFERENCES_SCREENSHOT`, `DREAMSCAPES_AVATAR_PREFERENCES_SCREENSHOT`과 `DREAMSCAPES_PREFERENCES_SCREENSHOT`을 설정하면 해당 GUI 테스트가 검증 화면을 PNG로 저장한다. 설정과 모델 전달 검증은 실제 모델의 생성 품질을 의미하지 않는다.
