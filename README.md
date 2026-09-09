# Dreamscapes

앱 시작 시 iiSocietyHelper 0.3.0을 `com.iisacc.dreamscapes`로 가동한다. Society를 포함해 같은 기기에서 공용 관측 위치를 사용하는 Helper들이 서로 실행 인스턴스를 발견한다. LVRS 엔진 수명에 Helper를 연결하고 전경·배경 상태를 반영하며 QML에 `societyHelper.observedApplications`와 이벤트를 제공한다. 관측이 중단된 경우 앱 상태 전환에서 재시작을 시도한다. 생성 큐나 모델 저장 위치는 이 관측 등록에 사용하지 않는다. iOS는 기존 Society App Group을 재사용하며 중단된 앱의 관측은 만료된다.

`Dreamscapes.Gui`의 패키지 실행 검증은 실제 앱과 별도 테스트 Helper가 서로를 발견하는지 검사한다. `SOCIETY_HELPER_DIRECTORY`를 테스트별 `build/` 경로로 지정한다. SDK 0.3.0 설치 위치는 `iiSocietyHelper_DIR`로 지정하며 현재 Workspace 검증본은 `SDK/iiSocietyHelper/build/install/lib/cmake/iiSocietyHelper`이다.

Qt 6.8.3 설치본과 로컬 LVRS 프레임워크를 사용하는 데스크탑·모바일 Qt Quick 앱이다. 모든 플랫폼이 하나의 UI를 공유한다. 홈에서는 QuickGenerate를 상단에 표시하며, 생성 완료 후에는 이미지 결과와 상단 이동 버튼을 표시하고 동일한 QuickGenerate를 하단에 배치한다. 플랫폼 판정과 반응형 레이아웃은 LVRS에 맡긴다. 아래 제품 의존성을 필수 패키지로 선언하며, CMake가 추가 패키지를 내려받거나 설치하지는 않는다.

Helper의 시작·전경/배경·상호 관측 이벤트는 공통 영속 발신 큐에도 기록한다. SocietyDaemon이 수신하므로 Society 창이 닫혀 있어도 이후 본체 앱에서 읽을 수 있다. 생성 요청을 다른 프로세스에 맡기거나 모델을 복제하는 변경은 아니다. 앱 고유 데이터는 `Helper::sendData(topic, payload)`로 연결할 수 있다. Qt Sql과 QSQLITE 드라이버는 SDK의 전이 의존성이다.

## 제품 의존성

다음 9개 패키지를 `find_package(... CONFIG REQUIRED)`로 탐색하고 `Dreamscapes` 앱의 `PRIVATE` 링크 의존성으로 명시한다. 기본 탐색 힌트는 각 `$HOME/.local/SDK/<패키지명>`이며, 다른 설치 위치는 `CMAKE_PREFIX_PATH` 또는 `<패키지명>_DIR`로 지정할 수 있다.

| 패키지 | CMake 링크 타깃 |
| --- | --- |
| iiCSMIDI | `iiCSMIDI::iiCSMIDI` |
| iiLicenseManager | `iiLicenseManager::iiLicenseManager` |
| iiLocalDiffusion | `iiLocalDiffusion::iiLocalDiffusion` |
| iiPaintEngine | `iiPaintEngine::iiPaintEngine` |
| iiSocietyContainer | `iiSocietyContainer::iiSocietyContainer` |
| iiSocietyHelper | `iiSocietyHelper::iiSocietyHelper` |
| iiSocietySync | `iiSocietySync::iiSocietySync` |
| iiUpdateManager | `iiUpdateManager::iiUpdateManager` |
| LVRS | `LVRS::LVRS` |

요청에서 `iiLisenseManager`로 표기된 라이브러리의 실제 설치 패키지명은 `iiLicenseManager`이다. 패키지가 없으면 CMake 구성 단계에서 실패하며, 선택적으로 생략하거나 빈 대체 타깃을 만들지 않는다. 이 선언만으로 각 라이브러리를 사용하는 제품 기능이 구현되는 것은 아니다.

## 소스 구조

프로젝트 루트·`App`·`App/Views`와 생성 큐 전용 `App/Generation`을 사용한다. QWidget은 사용하지 않는다.

- `main.cpp`: LVRS 런타임을 초기화하고 항상 `Main.qml`만 연다.
- `App/Main.qml`: 모든 플랫폼이 사용하는 단일 `LV.ApplicationWindow`이며 생성 완료·뒤로 이동과 QuickGenerate의 상단/하단 배치를 연결한다.
- `App/Views/Home/QuickGenerate.qml`: LVRS 입력란·버튼·메뉴와 반응형 스택 레이아웃을 조합한 공통 생성 패널이다.
- `App/Views/Result/GenerationResult.qml`: 생성 이미지의 Fit 표시와 Back·New Project 버튼, 생성 중/오류 상태를 제공한다.
- `App/Views/Result/Assets/right.svg`: Figma에서 내려받은 정확한 화살표 원본이며 LVRS IconButton 안에서 180도 회전한다.
- `App/tst_Gui.cpp`: Main 진입점·창 수명·화면 크기 변경 시 상태 유지·공통 패널 배치·입력 및 메뉴 선택·요청 전달을 검증한다.
- `App/Generation/GenerationController.h/.cpp`: Society 모델 참조를 고정한 앱 메모리 큐와 기존 iiLocalDiffusion 실행기 연결을 담당한다.
- `App/Generation/tst_Generation.cpp`: 큐 격리·재실행 시 소멸·모델 변경·순차 실행·결과 검증·취소·동시 창·경계 이탈을 검사한다.

GUI 테스트 타깃에는 이 저장소에 존재하는 소스만 등록한다. 현재 `App/AI` 디렉터리는
없으며, Society의 AI 연동 클래스 경로를 Dreamscapes 테스트 소스로 등록하면 CMake
생성이 실패한다. 이 소스 목록은 위 macOS 구성·빌드와 기존 GUI 회귀 테스트로 검증한다.

플랫폼별 창 파일과 이를 선택하던 Loader를 제거했다. `Main.qml`의 `LV.ApplicationWindow`가 제공하는 `isMobilePlatform`·크기 클래스·기본 adaptive scaffold 정책을 그대로 사용한다. 초기 크기는 데스크탑 960×640, 모바일 390×844이며 실제 모바일 창 크기·시스템 전환은 LVRS의 플랫폼 정책을 따른다. 최소 크기는 320×480으로, 데스크탑에서도 좁은 레이아웃을 사용할 수 있다. 창 크기가 바뀌어도 UI를 다시 생성하지 않아 프롬프트와 화면 비율 선택이 유지된다.

모든 뷰는 하나의 `appContent` 영역 안에 배치하고 `clip: true`로 경계를 제한한다. 데스크탑에서는 LVRS의 `windowDragHandleTopMargin + windowDragHandleHeight` 아래에서 시작하며 기본 상단 예약 높이는 28px이다. 이 영역은 macOS 신호등 버튼과 창 이동 핸들 전용이다. 모바일에서는 LVRS 시스템 안전 영역을 따르고, 전체 화면 또는 네이티브 타이틀바 모드에서는 불필요한 사용자 정의 핸들 여백을 추가하지 않는다. 입력기가 보이면 하단 가용 영역만 줄인다.

`windowDragExclusionItems`에는 `appContent`만 등록한다. 콘텐츠 클릭은 창을 이동시키지 않으며 상단 핸들은 제외 영역에 포함되지 않아 드래그가 가능하다. LVRS의 기본 가장자리·모서리 크기 조절 영역도 유지한다. 홈/결과 화면 전환과 핸들 높이 변경 후에도 같은 경계를 유지하는지는 `viewsLeaveWindowChromeAvailable`이 검사한다.

2026-09-08 이 변경의 macOS 빌드·QML 정적 검사, GUI 테스트 27건과 생성 컨트롤러 테스트 9건이 통과했다. 실제 추론을 수행하는 선택 테스트 1건은 이 UI 검증에서는 생략했다. Metal 렌더러로 화면 배치·뒤로 이동·연속 생성 테스트도 통과했다. 실행 중인 앱에서 신호등 및 Back 접근과 `windowMoveAttempted(true)`를 확인했으며, 자동 드래그 도구로는 실제 창 좌표 변화가 관측되지 않았다. 따라서 시스템 이동 요청의 수락과 실제 포인터 이동 완료를 구분한다. 검증 로그와 캡처는 `build/chrome-verification/`에 있다.

## 공통 QuickGenerate

[Figma QuickGenerate, 15:218](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=15-218)의 구성이다. Figma TextField는 `LV.InputField`의 Rounded 재질, DropdownButton은 `LV.LabelMenuButton`, PushButton은 `LV.LabelButton`에 대응한다. 레이아웃은 `LV.VStack`·`LV.HStack`을 사용하며 별도 UI 라이브러리나 복제한 아이콘을 추가하지 않는다. `generalchevronDown`은 Figma와 벡터 모양·색상이 같은 LVRS 내장 자산이다.

바깥 여백은 `LV.Theme.gap10`, 행 간격과 선택 버튼 간격은 `LV.Theme.gap8`이다. LVRS의 플랫폼 공통 치수 정책에 따라 데스크탑·iOS·Android 모두 높이 69px, 입력란 19px, 버튼 22px, Body 글자 13px을 사용한다. 좁은 화면에서는 가로 버튼 간격만 남은 폭에 맞춰 줄여 버튼의 글자와 화살표가 겹치지 않게 한다. 홈에서는 상단 핸들 및 시스템 안전 영역 아래의 콘텐츠 시작점에 고정된다. 결과 화면에서는 하단 안전 영역과 Qt 입력기가 보고한 키보드 영역 위에 배치하며, 메뉴를 버튼 위쪽으로 연다.

초기 상태는 빈 `Prompt`, `Image`, `1:1`, 수량 `1`이다. 출력 형식 메뉴에는 현재 지원하는 UI 형식인 Image만 표시한다. 비율 메뉴는 `1:1`, `4:3`, `3:4`, `16:9`, `9:16`을 제공하며, 메뉴의 폭은 안전 영역의 가용 너비 안으로 제한한다. Generate 클릭 또는 입력란의 Enter는 앞뒤 공백을 제거한 프롬프트와 현재 설정으로 `Main.generateRequested(prompt, mediaType, aspectRatio, count)` 신호를 한 번 전달한다. 빈 입력은 요청을 전달하지 않고 입력란에 포커스를 둔다. 입력 내용은 요청 후에도 유지한다.

Generate는 요청 신호를 유지하면서 선택 수량만큼 해당 앱 인스턴스의 메모리 큐에 제출한다. 아래 모델 선택 메뉴는 Society의 `Models/`를 읽는다. 모델이 없거나 입력이 잘못되면 요청을 실행하지 않고 오류를 표시한다. `Refresh models`와 앱 활성화 시 모델 목록을 갱신한다.

QuickGenerate는 [Figma 15:218](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=15-218)에 맞춰 `Image`, 종횡비, 생성 수량 드롭다운과 `Generate`를 배치한다. 수량의 기본값은 1이며 옵션은 `1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 30, 40, 50, 100, 200, 500, 1000`이다. 기존 LVRS `LabelMenuButton`·`ContextMenu`·`MenuItem`을 재사용하며 긴 수량 목록만 Qt Quick ListView로 스크롤한다. 메뉴 높이를 창 안으로 제한하고 선택 항목을 다시 열 때 표시한다. 마우스·터치 스크롤과 방향키·Home·End·Enter 선택을 지원한다. 새 외부 패키지를 추가하지 않았다.

수량은 `generateRequested(prompt, mediaType, aspectRatio, count)`로 전달한다. `GenerationController.enqueue(prompt, aspectRatio, count = 1)`은 프롬프트·비율·모델을 한 번 검증하고 동일한 모델 참조로 수량만큼 작업을 메모리 큐에 넣는다. 반환값은 첫 작업 ID이며 기존 단일 생성 호출은 그대로 1개를 요청한다. 1 미만 또는 1000 초과 수량과 잘못된 입력은 작업을 추가하지 않는다. 기존 실행기가 이미지를 한 장씩 순서대로 처리하므로 요청 수량이 GPU 동시 배치 크기가 되지 않는다. 각 작업의 취소·실패·완료 및 완성 이미지의 Society 저장 경계를 유지한다.

`Dreamscapes.Gui`는 정확한 20개 옵션, 최대 수량의 화면 배치·스크롤·재선택·Enter 제출, 화면 전환과 크기 변경 시 수량 보존, 상단/하단 메뉴 위치, UI 제출 후 이미지 3개 저장을 검사한다. `Dreamscapes.Generation`은 1000개 요청의 고유 ID·모델 스냅샷·일괄 큐 등록, 잘못된 수량의 무변경 처리, 단일 실행기의 순차 이미지 3개 생성을 검증한다. 생성 검증에는 기존 시험용 실행기를 사용한다.

## 생성 결과 화면

[Figma 생성 결과, 31:81](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=31-81)을 `GenerationResult.qml`로 구현한다. Figma의 402×547은 창 장식을 제외한 콘텐츠 기준이며, 기본 데스크탑 창은 상단 핸들 28px을 포함해 402×575이다. 콘텐츠 상단 버튼은 22px, 가운데 이미지 영역은 402×242px, 하단 QuickGenerate는 69px이며 이미지 위아래 여백은 각각 107px이다. 창 크기가 바뀌면 이미지 프레임은 안전 영역의 전체 너비를 채우고 상단 버튼과 하단 패널 사이 중앙에 놓인다. 세로 공간이 부족하면 이미지 영역을 줄인다. 배경·버튼·입력란·글자는 기존 LVRS 테마와 컴포넌트를 재사용한다. 새로운 UI/이미지 의존성은 없다.

생성 이미지는 `Image.PreserveAspectFit`으로 원본 비율을 유지하면서 프레임 안에 전체 이미지가 보이도록 맞춘다. 이미지는 가로·세로 중앙에 배치하며 프레임과 비율이 다르면 남는 공간은 여백으로 둔다. `clip: true`로 이미지가 버튼과 패널 영역에 그려지지 않게 한다. Generate 요청이 검증되어 앱 메모리의 대기열에 들어가면 프로세스 시작이나 이미지 완성을 기다리지 않고 즉시 결과 화면으로 이동한다. 입력·모델 검증에 실패하면 현재 화면에서 오류를 표시한다. 첫 중간 이미지가 오기 전에는 마지막 완료 이미지와 준비 상태를 표시하고, 이후 매 디노이징 단계의 실제 VAE 디코딩 이미지를 Fit으로 갱신한다. 비동기 로딩 중에는 직전 이미지를 유지한다. Back은 홈으로 돌아가며 생성 중에도 `View result`로 다시 열 수 있다. 중간 이미지·실패·취소·대기열 알림은 Back 이동을 취소하지 않는다.

`GenerationController.previewImage`, `previewStep`, `previewTotalSteps`는 현재 프로세스의 `IILD_PREVIEW` 이벤트를 받아 갱신한다. iiLocalDiffusion의 `--preview-dir` 콜백은 샘플러의 latent를 복사해 매 단계마다 VAE로 디코딩한다. 노이즈 이미지나 진행률을 UI에서 임의로 만들어 내지 않는다. 원자적으로 저장한 단계별 PNG가 준비된 뒤 이벤트를 전송하며, 화면에는 실제 단계 수를 표시한다. 미리보기는 긴 변 512px 이하이고 Society 밖의 앱 전용 임시 작업 위치에만 존재한다. 완료·실패·취소 후 제거하며, 완성 파일 검증에 통과한 이미지만 `Generation History/`와 `latestResult`에 남는다. 단계별 VAE 디코딩에는 추가 연산 시간이 든다.

2026-09-08 macOS 실기 검증에서는 Society의 `redLilyIllu_v10.safetensors`를 MPS/FP16, 512×512, 20단계로 실행했다. 20개의 서로 다른 중간 PNG를 관찰했고 컨트롤러도 20/20단계를 수신했다. 실제 창의 즉시 전환과 디노이징 중 화면 갱신, 최종 이미지 저장 및 임시 디렉터리 제거를 확인했다. 로컬 검증 기록과 프레임은 `build/live-preview-verification/verification.json` 및 같은 디렉터리에 있다.

Back 버튼은 상단 핸들 아래에 배치되므로 LVRS의 기본 모서리 리사이즈 영역과 겹치지 않는다.

Right-click or press and hold a completed image to open the LVRS context menu. **Save to File** opens the system save dialog. The menu responds only within the rendered image bounds and stays inside narrow windows. A short primary click and temporary generation previews do not open it. If another generation finishes while the dialog is open, saving still exports the originally selected image.

`ImageFileExporter` copies the original bytes, preserving resolution and metadata. It uses `QSaveFile` for atomic file replacement and accepts Android document-provider `content:` destinations. Cancelling leaves files untouched. Success and failure appear in the result status. The menu, dialog title, file filter, progress, success messages, and application-defined errors use English. The implementation reuses Qt 6.8.3 [FileDialog](https://doc.qt.io/qt-6.8/qml-qtquick-dialogs-filedialog.html), [TapHandler](https://doc.qt.io/qt-6.8/qml-qtquick-taphandler.html), and [QSaveFile](https://doc.qt.io/qt-6.8/qsavefile.html).

On macOS 11+, iOS 14+, and Android, **Save to Photos** adds the original image to the system photo library without a file picker. Duplicate requests are blocked during an import. Permission denials and failures allow a retry. Support follows the native platform, so applying a mobile theme on Windows or Linux does not expose the Photos action.

Apple platforms use [PhotoKit](https://developer.apple.com/documentation/photos/phphotolibrary), `PHAccessLevelAddOnly`, and `PHAssetCreationRequest` to import the original resource. The existing Qt/Society Info.plist receives `NSPhotoLibraryAddUsageDescription`; full-library read permission is unnecessary. **iCloud Photos syncs the saved image when enabled for the system photo library.** [Apple identifies the System Photo Library as the library used by iCloud Photos](https://support.apple.com/en-ie/104946). The app does not change synchronization settings. “Saved to Photos” confirms the local library transaction; cloud upload completion depends on connectivity, account state, and available storage.

Android writes the original bytes to `Pictures/Dreamscapes` through [MediaStore.Images](https://developer.android.com/training/data-storage/shared/media), making the image available to the default Photos or gallery app. Android 10+ needs no photo-read or storage permission for these new images and uses `IS_PENDING` to publish only completed writes. Android 9 requests storage-write permission; the manifest limits that permission to API 28 and earlier. Failed writes remove the incomplete MediaStore entry. Both backends use existing Qt and native OS APIs without an additional external package.

Validation on 2026-09-08: macOS build, qmllint, and all four CTest suites passed, covering menu gestures, asynchronous completion, failures, duplicate prevention, and callbacks after a view closes. A generated flower image was saved through the actual app menu and appeared in Photos with “Synced to iCloud” status. Exporting its unmodified original from Photos produced the same 346,972-byte PNG and SHA-256 as the generated source. Evidence: `build/photo-library-verification.json` and `build/photo-library-original/image.png`.

The Android 16 (API 36) emulator test APK exercised the C++ → JNI → Java → MediaStore path: original bytes matched, the saved row had `IS_PENDING=0`, and failed writes left no row. All four checks passed, and the test-created image was removed. The app's only photo-related manifest permission is `WRITE_EXTERNAL_STORAGE maxSdkVersion=28`. The signed iOS device bundle passed PhotoKit-link, add-only permission-description, and Society App Group checks. Saving to Photos on a physical iOS device and the Android 9 permission dialog have not been exercised.

To rerun the native Android tests, prepare the SDKs with `scripts/build-android.sh`, connect an API 29+ emulator, and run the following commands. Test mode uses separate Qt packaging directories for the app and test APK. The regular build script disables test mode and retains the usual app APK path.

```sh
cmake -S . -B build/android/build -DDREAMSCAPES_BUILD_ANDROID_PHOTO_TESTS=ON
env -u QT_QML_IMPORT_PATH -u QML_IMPORT_PATH -u QML2_IMPORT_PATH \
  ANDROID_SERIAL=emulator-5554 \
  JAVA_HOME=/Applications/CLion.app/Contents/jbr/Contents/Home \
  GRADLE_USER_HOME="$PWD/build/android/gradle" ANDROID_USER_HOME="$PWD/build/android/user" \
  /Volumes/Storage/Qt/6.8.3/macos/bin/androidtestrunner \
  --path "$PWD/build/android/build/android-build-DreamscapesPhotoNativeTests" \
  --apk "$PWD/build/android/build/android-build-DreamscapesPhotoNativeTests/DreamscapesPhotoNativeTests.apk" \
  --make "cmake --build $PWD/build/android/build --target DreamscapesPhotoNativeTests_make_apk" \
  --skip-install-root --adb /opt/homebrew/bin/adb --timeout 60 \
  -- -o "$PWD/build/photo-native-results.txt,txt"
```

`resultImageContextMenu` checks right-click, mouse/touch press-and-hold, and narrow-window placement. `resultImageSaveDialogPreservesSelectionAndCancel` checks the English menu and dialog flow, cancellation, the frozen source selection, and full-resolution output. `resultImageSaveToPhotos` checks the English Photos action and feedback. `Dreamscapes.ImageExport` covers original bytes, Unicode/spaces/special characters in paths, overwriting an existing file, and preserving files on invalid input. Native mobile file pickers require separate device validation.

The earlier file-export verification passed the build, three CTest suites, and qmllint. The actual macOS app exported the generated “A beautiful flower” image through the native save dialog; the 512×512 PNG matched the Society source and generation manifest hash. Evidence: `build/flower-save-verification.png` and `build/image-save-verification.json`.

QuickGenerate는 홈과 결과 화면을 오갈 때 `implicitHeight`를 유지하고 세로 위치만 변경한다. 위·아래 앵커를 동시에 전환하면서 패널이 창 전체 높이로 늘어나는 문제를 방지한다. 저장소 연동 GUI 테스트는 Back/재진입 후 패널 높이와 `View result` 버튼의 실제 클릭 가능 영역도 검사한다.

QuickGenerate는 이동·리사이즈 시 재생성하지 않는다. 현재 앱 세션에서는 직전 프롬프트와 비율·수량을 유지하므로 하단에서 수정하고 연속 생성할 수 있다. 앱을 다시 열면 큐·프롬프트·결과 화면은 복원하지 않으며, Society에 저장한 완성 이미지 파일은 유지한다. 작업 도중 사용자가 입력한 다음 프롬프트는 완료 이벤트가 덮어쓰지 않는다. 제출 시 키보드와 메뉴를 닫으며, 생성 중·대기·실패·이미지 로딩 오류는 결과 화면 안에 표시한다.

`GenerationController.latestResult`는 검증된 최근 완료 이미지 URL(`imageSource`)과 같은 작업의 ID·프롬프트·비율·모델·저장 위치 정보를 함께 반환한다. `New Project`는 대기·생성이 끝나고 완성 이미지가 로딩된 경우에만 활성화되고 `Main.newProjectRequested(url imageSource, var generationResult)`를 전달한다. 중간 미리보기는 프로젝트 입력으로 전달하지 않는다. 이 값은 화면에 표시된 생성 작업의 정보이며, 입력란에서 수정 중인 다음 프롬프트와 구분된다. 프로젝트 편집 화면과 프로젝트 파일 생성은 현재 저장소에 없으며 이 연결 인터페이스를 소비할 후속 기능이다.

`resultScreenLayout`은 기준 화면·좁은 창·데스크탑·모바일 테마·가로 화면에서 치수, 원본 비율과 전체 이미지가 보존되는 Fit 크기, 하단 패널 및 위로 열리는 메뉴를 검사한다. 가로·세로·정사각형 원본의 실제 표시 크기를 프레임에 맞춘 예상 크기와 비교한다. `generateOpensResultImmediatelyAndDisplaysEveryPreview`는 Generate 직후 전환, 매 단계 미리보기 교체, 진행 단계, New Project 비활성화, 생성 중 Back/재진입과 초안 보존을 검사한다. 생성 컨트롤러 테스트는 나뉘어 도착한 이벤트, 중복·잘못된 이벤트, 미리보기 이후 실패·취소, 임시 파일 정리와 최종 파일 분리를 검사한다. `generateButtonUsesSocietyStorage`는 완료 전환과 연속 생성, 프로젝트 입력을 검사한다. 테스트용 결정적 생성기는 UI·저장 프로토콜을 검증하며 실제 모델 추론을 대신하지 않는다. 아래 명령으로 캡처하는 이미지도 Fit 검사용 색상 패턴이다.

```sh
QSG_RHI_BACKEND=metal QML_DISABLE_DISK_CACHE=1 DREAMSCAPES_CAPTURE_DIR="$PWD/build/result-verification" \
  build/bin/DreamscapesGuiTests resultScreenLayout:result-figma-402 resultScreenLayout:result-ios
```

## Society 모델로 이미지 생성

Society에서 컨테이너를 열면 `iiSocietyContainer::SharedStorage`에 원본 경로와 UUID가 등록된다. Dreamscapes는 같은 기본 저장소를 자동으로 열며 `--society-container /absolute/source/path`로 특정 원본을 지정할 수도 있다. 이 경로는 Finder의 공개 드라이브가 아닌 8개 영역이 있는 원본이다. 기본 저장소를 바꾼 경우 Dreamscapes를 다시 열면 새 선택을 따른다. 앱별 모델 디렉터리·모델 복사본을 만들지 않는다.

Society 창에 `.safetensor` 또는 `.safetensors`를 드롭한 뒤 Dreamscapes에서 모델을 선택하고 Generate를 누른다. Diffusers 패키지(`model_index.json` 포함)도 Society의 `Models/`에서 사용할 수 있다. 목록은 저장 형식 후보이고 모델 아키텍처·LoRA 등 역할·필수 구성요소는 iiLocalDiffusion이 검사한다. 분리된 VAE·텍스트 인코더·어댑터를 조립하는 UI는 현재 포함하지 않는다.

완성된 생성 이미지는 Asset이 아니다. 모든 앱의 결과를 `Generation History/` 바로 아래에 이미지 파일로 저장하며 앱별·작업별 하위 폴더를 만들지 않는다. Dreamscapes는 `<UUID>-0001.png`처럼 작업 UUID와 이미지 순번으로 이름 충돌을 피한다. 생성만으로 `Asset Library/`에 파일을 추가하지 않는다.

큐, 프롬프트, 모델 참조, 실행 상태와 결과 메타데이터는 해당 앱 인스턴스의 메모리에만 둔다. Society나 다른 영구 저장소에 요청 JSON을 기록하지 않고, 다른 앱 인스턴스가 이 큐를 읽거나 재실행 시 복원하지 않는다. 앱을 종료하면 대기 요청과 세션 정보는 사라진다. `Generation History/`에 이미 저장한 완성 이미지는 유지된다.

생성기 CLI가 파일 경로를 요구하는 출력·미리보기·실행 자료·캐시는 앱이 소유한 `QTemporaryDir`에서 처리한다. 앱의 시스템 임시 위치를 기본으로 사용하고 작업마다 고유한 디렉터리를 만든다. `--work-dir`, `--cache-dir`, `--preview-dir`, `--output-dir` 및 하위 프로세스 임시 경로는 이 위치를 사용한다. 완료·실패·취소·앱 정상 종료 시 임시 파일을 정리하며 실패 기록도 영구 저장하지 않는다. `DREAMSCAPES_TEMP_DIRECTORY` 또는 테스트용 `GenerationRuntime.temporaryDirectory`로 이미 존재하는 임시 상위 디렉터리를 지정할 수 있지만 Society 내부 경로는 거부한다. 시험용 경로는 `build/` 아래로 격리한다.

모델 선택을 바꾸어도 메모리에 들어간 요청의 `{containerId, path, format, fingerprint}` 참조는 유지된다. 실행 직전과 완료 시 같은 원본 모델인지 다시 확인하며 삭제·변경·리디렉션된 모델로 자동 대체하지 않는다. fingerprint는 SDK의 제한된 변경 감지 값이고 전체 가중치 스냅샷은 아니다. `.safetensor`·대문자 확장자는 원본 이름을 바꾸지 않고 임시 작업용 링크에서 정규화한다.

SD1·SDXL 단일 체크포인트는 iiLocalDiffusion 설치본에 포함된 모델 설정·토크나이저를 사용한다. ComfyUI 설치·서버·노드 초기화가 필요 없으며, `--backend local`을 명시하여 독립 실행 경로를 사용한다.

기본 시드는 생성 요청마다 무작위로 정한다. Dreamscapes는 고정 시드를 전달하지 않으며, 체크포인트 실행기가 선택한 실제 시드를 포함한 결과 메타데이터는 작업 검증 후 앱 메모리의 `generation` 필드에 남는다. SDK에서 시드를 명시하면 해당 값을 유지한다.

앱 인스턴스마다 자기 메모리 큐를 순서대로 실행한다. 앱 인스턴스 사이에 큐 파일·상태 폴링·공유 작업자 잠금을 두지 않는다. Cancel은 이 인스턴스의 대기 요청이나 현재 실행을 취소한다. 데스크톱 Unix에서는 해당 작업의 별도 프로세스 그룹과 하위 프로세스까지 종료한다.

`iild-generate --worker`를 앱 인스턴스당 한 번 시작하고 `--model-path <Society 원본> --prompt ...` 등의 인자 배열을 요청 ID와 함께 stdin의 JSON 한 줄로 전달한다. Python 의존성 초기화, 모델 해시 및 호환되는 로드 모델의 메모리 캐시는 iiLocalDiffusion이 담당한다. Dreamscapes는 SDK의 `IILD_RESULT`를 받은 뒤 다음 큐를 전달한다. 취소·프로세스 비정상 종료 시 다음 요청에서 실행기를 다시 시작한다. 일반 요청 실패 뒤에도 다음 요청을 처리하며 큐와 실행 진단은 앱 메모리에만 남긴다.

Python/JIT가 사용하는 실행기 임시 디렉터리는 프로세스가 종료될 때까지 유지하고, 이미지·미리보기·요청 파일은 작업마다 별도 임시 디렉터리에 두어 완료·실패·취소 시 제거한다. 두 디렉터리 모두 Society 밖에 위치한다. 앱 종료 시 실행기를 종료하고 임시 디렉터리를 제거한다. `PYTHONDONTWRITEBYTECODE=1`을 유지하되 새 `PYTHONPYCACHEPREFIX`는 지정하지 않아 설치된 Python 바이트코드 캐시를 읽는다.

생성 시 가중치를 내려받지 않는다. SDK 완료 이벤트·실행 기록·모든 이미지의 크기와 디코딩을 검증한 뒤 완성 이미지 파일만 `Generation History/`에 원자적으로 저장한다. 앱 임시 위치와 Society가 다른 볼륨이어도 처리하며, 이미지 확장자와 바이트를 보존한다. 여러 이미지도 같은 영역 바로 아래에 저장한다. 실패·취소·미리보기·JSON·캐시를 결과 목록에 추가하거나 기존 파일을 덮어쓰지 않는다.

이전 버전이 만들었던 Society의 `.dreamscapes/generation/` 및 `Generation History/Dreamscapes/`는 저장소 연결 시 정리한다. 이전 대기 요청은 실행하지 않는다. 유효한 완료 요청이 가리키는 예전 Asset Library 이미지가 있으면 Generation History로 옮겨 보존하고, 기존 완성 이미지·모델·그 밖의 Asset은 유지한다. 구버전 작업자가 사용 중이거나 상위 경로가 리디렉션된 경우 해당 파일을 지우지 않는다. 이때만 이전 작업자의 잠금을 확인하며 새 큐를 저장하는 용도로 사용하지 않는다.

`Dreamscapes.Generation`은 인스턴스별 큐 격리, 재실행 시 큐 소멸, 완료 이미지 보존, 정상·실패·취소·종료 때 임시 자료 제거, Society 내부 임시 경로 거부, 이전 자료 정리와 모델 링크 보존을 검증한다. 모델 참조·여러 결과·이름 충돌·부분 실패·미리보기 검사도 유지한다. `Dreamscapes.Gui`는 실제 Generate 완료 이미지가 Generation History에서 로드되는지 검사한다.

상주 실행기 검사는 연속 작업의 PID 재사용, 일반 오류·분할 수신된 긴 한국어 오류 뒤 다음 요청 처리, 프로세스 중단 후 재시작, 작업 임시 파일과 실행기 임시 파일의 수명 분리를 포함한다. `realSocietyInference`는 설치된 SDK로 실제 두 요청을 실행해 같은 PID와 최초 모델 구성·장치 배치 각 1회를 확인한다. 두 번째 요청에서는 전체 모델 해시·구성 파일 읽기·모델 재구성·장치 배치가 모두 0회이고 캐시가 적중해야 한다. 구성 시 `--worker`가 없는 구버전 SDK는 거부한다.

iiLocalDiffusion은 모델 구성과 장치 배치를 각각 유지한다. 샘플러·프롬프트·시드 같은 생성 옵션 변경은 모델 재구성이나 재배치를 유발하지 않는다. 장치·배치 설정만 바뀌면 기존 구성 요소를 재사용하고 필요한 배치만 갱신한다. 구성 파일 해석도 변경 감지에 따라 캐시되며, 해당 캐시나 가중치를 Dreamscapes 또는 Society에 별도로 영구 저장하지 않는다.

앱이 `Qt::ApplicationActive`가 되면 `GenerationController`가 iiLocalDiffusion에 `foreground` 상태와 선택된 Society 모델을 전달한다. 생성 버튼을 누르기 전부터 SDK가 모델 구성과 GPU 배치를 준비하고, 큐가 비어도 해당 실행기에서 유지한다. 준비 전용 명령은 이미지·미리보기·작업 기록을 만들지 않는다. `inferenceStatus`는 SDK가 확인한 준비 상태·장치·GPU 상주 여부·오류를 노출한다. 실행기 시작 이벤트만으로 모델 준비 완료로 판정하지 않는다.

활성 상태나 모델 선택이 준비 중에 바뀌면 최신 상태를 모아 현재 명령 완료 뒤 처리한다. 실제 생성 큐를 우선하며 큐와 준비 명령을 같은 작업으로 기록하지 않는다. Background에서는 새 사전 준비를 시작하지 않고 진행 중인 생성과 기존 세션 캐시는 유지한다. 다시 Foreground가 되면 SDK의 기존 모델을 검증하고 재사용한다. 모델이 없으면 기존 모델을 준비 완료로 표시하지 않는다. 프로세스 종료·취소로 사라진 캐시는 다음 준비/생성 시 다시 구성한다.

전송 전에 `IILD_READY.capabilities`의 `foreground-residency` 지원을 확인한다. 구버전 실행기에 준비 명령이 잘못된 이미지 생성 요청으로 전달되지 않도록 차단한다. `foregroundPreparesWithoutAQueueAndReusesTheWorker`와 `foregroundModelChangesAndQueuedRequestsRemainSeparate`가 준비·실제 큐·모델 선택의 분리를 검증하고, GUI의 `foregroundApplicationPreparesBeforeGenerate`가 실제 앱 활성 상태 연결을 검사한다. `foregroundPreparationFailureCanRecoverWithAnotherModel`과 `foregroundControlRequiresTheSdkCapability`는 오류 복구와 구버전 SDK 감지를 검증한다. 모바일은 같은 앱 활성 상태를 관측하지만 기존 문서의 네이티브 추론 미구현 제한은 그대로 적용된다.

다음 선택 검증은 설치된 SDK로 GPU 사전 준비와 첫 생성의 구성·배치 재사용을 확인한다. `Generation History`에 결과 이미지 한 개를 남기므로 로컬 SD1 Diffusers 모델이 있는 별도의 검증 컨테이너를 사용한다.

```sh
DREAMSCAPES_REAL_SMOKE_CONTAINER=/path/to/verification-society \
  IILD_GENERATOR_EXECUTABLE="$HOME/.local/SDK/iiLocalDiffusion/bin/iild-generate" \
  build/DreamscapesGenerationTests realForegroundPreparation
```

기존 Qt Core/Gui의 QProcess·QTemporaryDir·QSaveFile과 iiLocalDiffusion을 재사용한다. 추가 큐 프레임워크나 영구 데이터베이스를 도입하지 않는다. 추론 의존성과 라이선스는 iiLocalDiffusion의 기존 Diffusers/PyTorch 런타임과 번들 설정 리소스의 라이선스를 따른다. SDK 라이브러리만 링크했다고 Python 추론 환경이 설치되는 것은 아니다.

필수 버전은 `iiSocietyContainer >= 0.7.0`, `iiLocalDiffusion >= 0.3.0`이다. 데스크톱 구성 시 발견한 실행기의 `--model-path` 계약도 검사하여 오래된 설치본을 거부한다. `DREAMSCAPES_DIFFUSION_EXECUTABLE` CMake 경로나 `IILD_GENERATOR_EXECUTABLE` 실행 환경으로 SDK 실행기를 지정한다. Python 환경은 SDK의 관리 환경을 기본으로 사용하며 `DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE` CMake 옵션 또는 우선하는 `IILD_PYTHON_EXECUTABLE` 환경변수로 지정할 수 있다. 단일 체크포인트는 현재 SDK의 독립 실행기와 번들 SD1/SDXL 설정을 사용하며 ComfyUI를 요구하지 않는다. 누락된 런타임·지원하지 않는 모델은 실패 원인을 표시한다.

iOS는 `iiSocietyContainer_configure_ios_client()`와 `SOCIETY_IOS_APP_GROUP`·`SOCIETY_IOS_TEAM`으로 Society와 같은 App Group의 모델·완성 이미지에 접근한다. 큐는 각 앱의 메모리에만 둔다. Documents나 Files 투영본으로 내부 모델을 공개하지 않는다. [Qt QProcess는 iOS에서 지원하지 않으므로](https://doc.qt.io/qt-6/qprocess.html) 이 데스크톱 Python 실행기를 모바일에서 실행하지 않는다. iOS·Android에서는 현재 앱 세션의 메모리 큐와 상태 열람을 제공하고 실행기 미지원 상태를 표시한다. 모바일 네이티브 추론·다른 기기로 요청 전달·계정 간 저장소 동기화는 아직 구현하지 않았다. Android 앱 간 저장소 발견은 iOS App Group과 동일하게 제공되지 않는다.

프로세스 계약 테스트는 `fixtures/generate.py`의 명시적인 테스트 이미지를 사용하며 추론 증거로 취급하지 않는다. 실제 엔진 검증은 준비된 검증용 Society 컨테이너를 지정하여 별도로 실행한다.

```sh
DREAMSCAPES_REAL_SMOKE_CONTAINER="$PWD/build/society-inference-verification" \
IILD_GENERATOR_EXECUTABLE="/path/to/current/iiLocalDiffusion/bin/iild-generate" \
IILD_PYTHON_EXECUTABLE="/path/to/managed/.venv/bin/python" \
  build/DreamscapesGenerationTests realSocietyInference
```

Main 자체가 최상위 창이며 `transientParent: null`을 명시한다. 별도 부모 창이나 중간 로더가 없으며, 마지막 창을 닫으면 Qt의 기본 종료 정책을 따른다.

## macOS 빌드와 실행

Qt 설치 경로는 `/Volumes/Storage/Qt/6.8.3/macos`이며, 위 9개 제품 패키지의 설치본이 모두 필요하다. CMake 3.31 이상, Ninja, C++20 컴파일러가 필요하다. `find_package`의 `EXACT` 조건으로 다른 Qt 버전이 선택되는 것을 막는다.

```sh
cmake --preset macos-debug
cmake --build --preset macos-debug --parallel
ctest --preset macos-debug
open build/bin/Dreamscapes.app
```

모든 생성 파일은 `build/`에 둔다. 기존 `cmake-build-debug/`는 보존하지만 사용하지 않는다. CLion의 기존 Debug 프로필도 `build/`와 같은 Qt·LVRS 설치본을 사용하도록 설정한다.

macOS 개발 앱에는 설치된 LVRS 라이브러리 디렉터리를 build RPATH로 기록하므로 별도의 `DYLD_LIBRARY_PATH` 설정 없이 실행할 수 있다. 현재 산출물은 설치된 Qt·LVRS를 참조하는 개발 빌드이며 재배포용 독립 패키지는 아니다.

## 다른 플랫폼

### iOS 기기 패키지

iOS 빌드는 필수 SDK 9개를 유지한다. 정적 LVRS·Society Container·Helper와 나머지
대상 iOS SDK를 연결하고, 동적 제품 라이브러리 및 iiCSMIDI의 전이 의존성
iiFileProvider를 앱의 `Frameworks/`에 포함해 함께 서명하고 실행 파일의 rpath로 연결한다. 정적 Qt의 SQLite
플러그인을 명시적으로 등록하므로 Helper의 영속 큐가 기기에서도 DB를 열 수 있다.
앱 ID는 `com.iisacc.dreamscapes`, 공유 그룹은 Society와 같은
`group.com.iisacc.society`이다. Xcode 자동 서명에는 `SOCIETY_IOS_TEAM`과 기기가
등록된 프로파일이 필요하다. 모든 iOS 생성물은 `build/ios-device/` 안에 둔다.

실제 서명된 기기 패키지는 다음 명령으로 플랫폼·arm64·공유 권한·기기 프로파일·
내장 동적 라이브러리의 완결성과 LVRS 등록·리소스를 검증한다. 이 검증 후 기기 설치·실행을 별도로 확인한다.

```sh
DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer \
python3 -B tests/verify_ios_bundle.py build/ios-device/bin/Debug/Dreamscapes.app \
  --device <iPhone-UDID>
```

iOS의 생성 실행기 미지원 범위는 위 ‘Society 모델로 이미지 생성’ 설명과 같다.
앱 설치나 SDK 링크만으로 모바일 네이티브 추론이 연결되는 것은 아니다.

2026-09-08 Xcode 27 beta 6와 Qt 6.8.3으로 iPhone 15 Pro Max(iOS 27)에 개발 서명
빌드 0.1.0을 설치했다. 앱 목록·실행 프로세스·실제 홈 화면을 확인했고 Society를
실행한 뒤 재실행하여 같은 App Group 저장소에 연결되는 것을 확인했다. 검증 로그와
화면은 `Workspace/build/ios-install/`에 있다. 현재 동적 SDK에 정적으로 포함된 Qt의
Objective-C 클래스 중복 경고가 남아 있으므로 장시간 사용 안정성 검증과는 구분한다.

대상 플랫폼의 Qt **6.8.3** 키트와 같은 ABI의 제품 의존성 9개 설치본이 필요하다. 해당 키트의 `qt-cmake`로 프로젝트를 구성하고 `LVRS_DIR`에 설치 루트를 지정한다. LVRS 설치 패키지가 대상 플랫폼용 라이브러리를 선택한다. 다른 제품 패키지도 해당 플랫폼용 설치본을 제공해야 한다. Windows·Linux 키트는 해당 운영체제에서 사용한다.

Android는 Android SDK/NDK/JDK, iOS는 Xcode와 대상 기기 또는 시뮬레이터용 라이브러리가 별도로 필요하다. 크로스 빌드에서는 호스트 GUI 테스트를 생성하지 않는다. 플랫폼 전환 시 서로 다른 툴체인의 CMake 캐시를 섞지 말고, 생성물 전용 `build/`를 정리한 후 같은 경로에 다시 구성한다.

현재 Android LVRS 설치본은 `arm64-v8a`용이므로 `android_arm64_v8a` 키트와 맞춘다. iOS의 LVRS 정적 아카이브는 별도 QML 플러그인이 없으므로 앱 링크에서 `WHOLE_ARCHIVE`를 적용해 QML 타입 등록과 리소스 초기화 객체가 제거되지 않도록 한다.

호스트에서는 Main의 단일 창과 LVRS의 실제 런타임 플랫폼 값을 검증한다. iOS·Android 테마를 적용한 레이아웃 검사도 같은 Main을 사용한다. 읽기 전용 런타임 플랫폼을 강제로 바꾸지 않으므로 모바일 바이너리 빌드·시스템 안전 영역·실기기 실행을 대신하지 않는다.

### Android 에뮬레이터 실행

`scripts/build-android.sh`는 설치된 Qt 6.8.3 Android arm64 키트·NDK·SDK와 JDK 21을 사용해 서명된 디버그 APK를 만든다. 호스트 빌드를 유지하면서 모든 Android 생성물을 `build/android/` 아래에 두고, 각 CMake 빌드 디렉터리 이름도 `build/`로 사용한다. 필수 제품 의존성 9개를 APK에 포함하며 빈 대체 라이브러리를 만들지 않는다.

Android 설치본이 없는 iiCSMIDI·Society 계열 3개·iiLocalDiffusion과 iiCSMIDI의 전이 의존성 iiFileProvider는 `Workspace/SDK`의 실제 소스로 빌드해 `build/android/sdk`에 설치한다. iiFileProvider를 먼저 빌드하고 APK에도 포함한다. 기존 macOS SDK 설치본은 유지한다. iiLocalDiffusion은 기존 옵션으로 Apple 전용 Core ML·MLX와 선택적 LibTorch를 끄며, 기존 의존성 [json-c 0.18](https://github.com/json-c/json-c/releases/tag/json-c-0.18-20240915)을 SHA-256으로 확인한 뒤 정적으로 링크한다. 생성 엔진과 앱 UI의 연결 범위는 바뀌지 않는다.

현재 머신에서 확인한 환경은 Android SDK `/opt/homebrew/share/android-commandlinetools`, NDK r29 `/opt/homebrew/share/android-ndk`, JDK 21 `/Applications/CLion.app/Contents/jbr/Contents/Home`이다. SDK·NDK는 `ANDROID_SDK_ROOT`·`ANDROID_NDK_ROOT`, JDK는 `DREAMSCAPES_JAVA_HOME`, Qt는 `DREAMSCAPES_QT_ROOT`, 제품 SDK 경로는 `DREAMSCAPES_SDK_SOURCE_ROOT`·`DREAMSCAPES_SDK_INSTALL_ROOT`로 바꿀 수 있다. Gradle 캐시와 Android 빌드 설정도 `build/android` 아래에 둔다.

```sh
./scripts/build-android.sh
adb -s emulator-5554 install -r build/android/build/android-build/Dreamscapes.apk
adb -s emulator-5554 shell am start -W \
  -n com.iisacc.dreamscapes/com.iisacc.dreamscapes.DreamscapesActivity
```

기존 `WeUs_API_36` AVD는 Pixel 9 Pro 설정·Android 16(API 36)·arm64-v8a이다. 에뮬레이터가 꺼져 있으면 별도 터미널에서 아래 명령으로 켠다. `-read-only -no-snapshot`은 기존 AVD 데이터와 스냅샷을 보존하며, 이번 실행에서 설치한 앱은 이 에뮬레이터 세션에만 유지된다.

```sh
ANDROID_SDK_ROOT=/opt/homebrew/share/android-commandlinetools \
ANDROID_AVD_HOME="$HOME/.config/.android/avd" \
  /opt/homebrew/share/android-commandlinetools/emulator/emulator \
  -avd WeUs_API_36 -read-only -no-snapshot -no-audio -gpu auto
```

2026-09-06 Android 에뮬레이터에서 APK 서명 검증·설치·Activity 실행·LVRS의 Android/Vulkan 초기화·모바일 상단 패널 표시를 확인했다. 실제 Android 키보드로 프롬프트를 입력하고 메뉴에서 16:9를 선택한 뒤 Generate를 눌러도 앱이 유지되는 것을 확인했다. 실행 화면과 로그는 `build/android/emulator-*.png`·`build/android/emulator-runtime.log`에 저장한다. 설치된 LVRS는 Qt 6.8에 없는 창 패딩 속성 4개에 대해 경고를 출력하지만, 앱 로딩과 표시를 막지는 않았다. 물리 기기와 iOS 실행은 별도 검증이 필요하다.

## 검증

의존성 검증은 `cmake --preset macos-debug`부터 시작한다. 필수 패키지가 없을 때 구성 실패와 해당 패키지의 Config 파일 안내가 출력되는지 확인한다. 모든 패키지 설치 후에는 구성·빌드·CTest가 순서대로 성공해야 한다. 구성이나 빌드가 실패한 상태에서 기존 `build/`의 테스트 바이너리가 통과하더라도 새 의존성 구성의 검증 결과로 취급하지 않는다.

`ctest --preset macos-debug`는 offscreen/software 환경에서 Main의 단일 최상위 창·LVRS 플랫폼 판정·마지막 창 닫힘 신호·엔진 해제 시 창 정리를 검사한다. 창 크기를 960→320→800→1440→390px로 바꾸며 LVRS 크기 클래스 변화, 동일 패널과 입력·비율 상태 유지, 변경 후 요청 전달을 검증한다. 공통 패널은 데스크탑 320·960·1440px, 402px 원본 치수, iOS·Android 테마의 320·360·390·844px 폭에서 상단 배치와 버튼 겹침을 검증한다. 데스크탑과 모바일 테마 모두에서 키 입력·메뉴 열기와 선택·Generate 및 Enter 요청·공백 입력 차단도 검사한다. QML 디스크 캐시를 끄므로 사용자 캐시에 QML 캐시를 남기지 않는다.

테스트는 `QGuiApplication::exec()` 안에서 실행한다. [Qt의 마지막 창 닫힘 신호](https://doc.qt.io/qt-6.8/qguiapplication.html#lastWindowClosed)는 이 주 이벤트 루프가 실행 중일 때 발생하므로, 단순한 이벤트 처리만으로 창 종료 검사를 대신하지 않는다.

호스트의 실제 렌더러로 공통 UI를 확인하고 캡처하려면 아래 명령을 사용한다. 테스트 실행 중 창이 잠시 표시되고 `build/desktop-960.png`와 `build/ios-390.png`가 저장된다. 후자는 호스트 창에 모바일 LVRS 테마를 적용한 미리보기이다.

```sh
QML_DISABLE_DISK_CACHE=1 DREAMSCAPES_CAPTURE_DIR="$PWD/build" \
  build/bin/DreamscapesGuiTests sharedPanelLayout:desktop-960 sharedPanelLayout:ios-390
```

또한 런타임 라이브러리·QML 검색 경로 환경변수를 제거한 별도 프로세스로 실제 앱을 실행하여 내장된 `Main` QML 진입점이 정상 로딩되는지 검증한다.

LVRS 시작 로그의 `windowCount`는 직접 QML 루트 중 창인 객체만 센다. Main 자체가 창이므로 `windowCount: 1`이어야 한다. 공통 패널의 표시와 입력 동작은 GUI 테스트와 실제 데스크탑 창으로 검증한다.

QML 모듈은 [Qt 6.8 공식 CMake 구성](https://doc.qt.io/qt-6.8/cmake-build-qml-application.html)을 따르는 LVRS 소비자 헬퍼로 등록한다. 앱 빌드와 QML 정적 검사 명령은 다음과 같다.

```sh
cmake --build build --target Dreamscapes Dreamscapes_qmllint --parallel
```

SDK 소스의 새 위치는 `Workspace/SDK`이다. CMake 기본 힌트·프리셋·CLion
프로필은 `~/.local/SDK` 설치본을 사용한다. 아직 없는 필수 SDK에 대한 구성 실패
정책은 유지한다. `cmake --fresh --preset macos-debug`로 새 경로를 검증한다.

2026-09-06에는 필수 SDK 9개가 모두 탐색되었으며, 누락되어 있던 GUI 테스트 소스도 복구했다. 모바일 테마를 사용하는 호스트 검증은 실제 iOS·Android 기기에서의 시스템 안전 영역·키보드·터치 검증을 대신하지 않는다.

2026-09-07 공통 UI 통합 후 macOS Debug 구성·빌드, GUI 테스트 17/17, QML 정적 검사를 통과했다. 실제 데스크탑 앱에서 QuickGenerate 표시·프롬프트 입력·16:9 선택·Generate 조작과 LVRS 시작 로그의 `windowCount: 1`을 확인했다. Android APK도 통합된 Main으로 다시 빌드하고 서명을 검증했다. 이번 변경의 Android 에뮬레이터 실행과 iOS 빌드·실행은 수행하지 않았다.


2026-09-08 당시 저장 구조에서 Society 공통 스토리지 연결을 검증했다. 다음 경로는 수정 전 검증 아티팩트이며 현재 생성 저장 규약은 위 절을 따른다. macOS의 실제 Dreamscapes 창에서 Generate를 눌러 Society의 작은 무작위 가중치 SD 1.x 검증 패키지를 사용한 512×512·20스텝 이미지를 생성했다. `generation.json`의 실행 장치는 `mps`, GPU 가속은 `true`이며 모델 원본 파일 해시와 저장 경로를 함께 기록했다. 결과는 `build/society-inference-verification/Asset Library/Dreamscapes/af21b3b3-4743-463e-8d11-7011697b42bc/`, 요청은 같은 검증 컨테이너의 `Generation History/Dreamscapes/af21b3b3-4743-463e-8d11-7011697b42bc/request.json`에 있다. CPU 64×64·1스텝 실제 실행도 별도로 통과했다. 검증 모델은 이미지 품질이나 사용자의 체크포인트 호환성을 입증하지 않는다.

이 빌드의 iiLocalDiffusion 패키지는 `SDK/iiLocalDiffusion/build/society-consumer/install`에 설치했다. Python 환경은 기존 `SDK/iiLocalDiffusion/reference/diffusers/.venv`를 사용하고, 체크포인트용 ComfyUI도 해당 설치 패키지의 관리 경로에 준비했다. 호스트의 Metal 컴파일러가 없어 C++ 선택 백엔드 MLX·LibTorch·Core ML을 끈 별도 `build/society-consumer/build`에서 패키지와 70개 CTest를 검증했다. 실제 위 GPU 추론은 Python PyTorch MPS 경로이다. iOS 저장소 브리지와 생성 컨트롤러의 iOS 조건부 코드는 호스트 Catalyst 헤더로 문법·타입을 검사했으며, Xcode/iOS SDK가 없어 iOS 앱 링크·기기 실행은 검증하지 않았다.


2026-09-08 독립 추론 검증: SDK의 standalone Diffusers/PyTorch 실행기로 교체한 뒤
`redLilyIllu_v10.safetensors`를 사용하여 실제 Generate 버튼에서 512×512·20스텝
MPS FP16 이미지를 생성하고 네이티브 결과 화면에 표시했다. ComfyUI는 실행하지
않았다. 작업 ID는 `4e4f253e-33e2-4ee8-8c0e-89c176883dc8`이며 이전 실패 기록은
보존했다. 재빌드 후 GUI·생성 테스트 2/2가 통과했다. 세부 SDK 검증은
`SDK/iiLocalDiffusion/docs/standalone-validation.md`에 기록했다.
