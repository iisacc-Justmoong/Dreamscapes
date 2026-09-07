# Dreamscapes

Qt 6.8.3 설치본과 로컬 LVRS 프레임워크를 사용하는 데스크탑·모바일 Qt Quick 앱이다. 모바일 창 상단에는 프롬프트·출력 형식·화면 비율·생성 버튼으로 구성한 QuickGenerate 패널을 표시한다. 데스크탑 창은 기존 `Hello world!` 화면이다. 아래 제품 의존성을 필수 패키지로 선언하며, CMake가 추가 패키지를 내려받거나 설치하지는 않는다.

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

기존 프로젝트 루트·`App`·`App/Views`를 사용하며 새 소스 디렉터리를 만들지 않는다. QWidget은 사용하지 않는다.

- `main.cpp`: LVRS 런타임을 초기화하고 항상 `Main.qml`만 연다.
- `App/Main.qml`: LVRS 런타임 플랫폼 판별로 창 하나를 선택하는 `Loader`이다. 자체 창은 만들지 않는다.
- `App/Views/Desktop.qml`: Windows·macOS·Linux용 `LV.ApplicationWindow`이다.
- `App/Views/Mobile.qml`: Android·iOS용 `LV.ApplicationWindow`이다.
- `App/Views/Home/QuickGenerate.qml`: LVRS 입력란·버튼·메뉴를 조합한 모바일 상단 패널이다. `Mobile.qml`은 `Home` 디렉터리를 명시적으로 가져온다.
- `App/tst_Gui.cpp`: Main 진입점·창 수명·모바일 패널 배치·입력 및 메뉴 선택·요청 전달을 검증한다.

GUI 테스트 타깃에는 이 저장소에 존재하는 소스만 등록한다. 현재 `App/AI` 디렉터리는
없으며, Society의 AI 연동 클래스 경로를 Dreamscapes 테스트 소스로 등록하면 CMake
생성이 실패한다. 이 소스 목록은 위 macOS 구성·빌드와 기존 GUI 회귀 테스트로 검증한다.

`Main.qml`은 LVRS 런타임의 `LV.Platform.mobile`을 사용한다. 모바일이면 `Views/Mobile.qml`, 아니면 `Views/Desktop.qml`을 로드한다. 선택된 QML의 `LV.ApplicationWindow`만 생성하며 추가 숨은 부모 창은 없다. C++에 플랫폼 분기는 없고 창 너비를 줄여도 다른 플랫폼 창으로 전환하지 않는다. 각 창은 자체 창 크기·최소 크기를 관리한다. 모바일 패널은 LVRS가 제공하는 시스템 안전 영역의 위·왼쪽·오른쪽 여백을 적용한다.

## 모바일 QuickGenerate

[Figma QuickGenerate, 15:218](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=15-218)의 구성이다. Figma TextField는 `LV.InputField`의 Rounded 재질, DropdownButton은 `LV.LabelMenuButton`, PushButton은 `LV.LabelButton`에 대응한다. 레이아웃은 `LV.VStack`·`LV.HStack`을 사용하며 별도 UI 라이브러리나 복제한 아이콘을 추가하지 않는다. `generalchevronDown`은 Figma와 벡터 모양·색상이 같은 LVRS 내장 자산이다.

바깥 여백은 `LV.Theme.gap10`, 행 간격과 선택 버튼 간격은 `LV.Theme.gap8`이다. LVRS의 플랫폼 공통 치수 정책에 따라 데스크탑·iOS·Android 모두 높이 69px, 입력란 19px, 버튼 22px, Body 글자 13px을 사용한다. 좁은 화면에서는 가로 버튼 간격만 남은 폭에 맞춰 줄여 버튼의 글자와 화살표가 겹치지 않게 한다. 패널은 화면 너비를 따르고 시스템 안전 영역 바로 아래에 고정된다. 키보드 입력 중에도 상단 위치를 유지한다.

초기 상태는 빈 `Prompt`, `Image`, `1:1`이다. 출력 형식 메뉴에는 현재 지원하는 UI 형식인 Image만 표시한다. 비율 메뉴는 `1:1`, `4:3`, `3:4`, `16:9`, `9:16`을 제공하며, 두 메뉴의 폭은 안전 영역의 가용 너비 안으로 제한한다. Generate 클릭 또는 입력란의 Enter는 앞뒤 공백을 제거한 프롬프트와 현재 설정으로 `Mobile.generateRequested(prompt, mediaType, aspectRatio)` 신호를 한 번 전달한다. 빈 입력은 요청을 전달하지 않고 입력란에 포커스를 둔다. 입력 내용은 요청 후에도 유지한다.

현재 구현 범위는 입력 UI와 요청 신호이다. 실제 이미지 생성 엔진·작업 대기열·결과 저장은 연결되어 있지 않으며, 이 버튼만으로 이미지를 생성하거나 외부 서비스에 요청하지 않는다.

[Qt 6.8 Loader](https://doc.qt.io/qt-6.8/qml-qtquick-loader.html)는 `Item`뿐 아니라 창 객체도 로드한다. 창을 `Loader`의 크기에 맞추거나 `Item`으로 취급하지 않으며, 창 자체의 크기와 표시 상태를 사용한다.

두 창은 `transientParent: null`을 명시한다. 따라서 화면에 연결되지 않은 Main 로더를 부모로 가져도 표시가 보류되지 않는다. Main 내부의 창 로딩이 실패하면 종료 코드 `1`을 요청하며, 마지막 창을 닫으면 Qt의 기본 종료 정책을 따른다.

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

대상 플랫폼의 Qt **6.8.3** 키트와 같은 ABI의 제품 의존성 9개 설치본이 필요하다. 해당 키트의 `qt-cmake`로 프로젝트를 구성하고 `LVRS_DIR`에 설치 루트를 지정한다. LVRS 설치 패키지가 대상 플랫폼용 라이브러리를 선택한다. 다른 제품 패키지도 해당 플랫폼용 설치본을 제공해야 한다. Windows·Linux 키트는 해당 운영체제에서 사용한다.

Android는 Android SDK/NDK/JDK, iOS는 Xcode와 대상 기기 또는 시뮬레이터용 라이브러리가 별도로 필요하다. 크로스 빌드에서는 호스트 GUI 테스트를 생성하지 않는다. 플랫폼 전환 시 서로 다른 툴체인의 CMake 캐시를 섞지 말고, 생성물 전용 `build/`를 정리한 후 같은 경로에 다시 구성한다.

현재 Android LVRS 설치본은 `arm64-v8a`용이므로 `android_arm64_v8a` 키트와 맞춘다. iOS의 LVRS 정적 아카이브는 별도 QML 플러그인이 없으므로 앱 링크에서 `WHOLE_ARCHIVE`를 적용해 QML 타입 등록과 리소스 초기화 객체가 제거되지 않도록 한다.

호스트에서는 실제 LVRS 런타임이 선택한 창과 두 창의 독립 로딩을 검증한다. 읽기 전용 런타임 플랫폼을 강제로 바꾸지 않으므로 모바일 운영체제에서의 Main 분기·바이너리 빌드·실기기 실행을 대신하지 않는다.

### Android 에뮬레이터 실행

`scripts/build-android.sh`는 설치된 Qt 6.8.3 Android arm64 키트·NDK·SDK와 JDK 21을 사용해 서명된 디버그 APK를 만든다. 호스트 빌드를 유지하면서 모든 Android 생성물을 `build/android/` 아래에 두고, 각 CMake 빌드 디렉터리 이름도 `build/`로 사용한다. 필수 제품 의존성 9개를 APK에 포함하며 빈 대체 라이브러리를 만들지 않는다.

Android 설치본이 없는 iiCSMIDI·Society 계열 3개·iiLocalDiffusion은 `Workspace/SDK`의 실제 소스로 빌드해 `build/android/sdk`에 설치한다. 기존 macOS SDK 설치본은 유지한다. iiLocalDiffusion은 기존 옵션으로 Apple 전용 Core ML·MLX와 선택적 LibTorch를 끄며, 기존 의존성 [json-c 0.18](https://github.com/json-c/json-c/releases/tag/json-c-0.18-20240915)을 SHA-256으로 확인한 뒤 정적으로 링크한다. 생성 엔진과 앱 UI의 연결 범위는 바뀌지 않는다.

현재 머신에서 확인한 환경은 Android SDK `/opt/homebrew/share/android-commandlinetools`, NDK r29 `/opt/homebrew/share/android-ndk`, JDK 21 `/Applications/CLion.app/Contents/jbr/Contents/Home`이다. SDK·NDK는 `ANDROID_SDK_ROOT`·`ANDROID_NDK_ROOT`, JDK는 `DREAMSCAPES_JAVA_HOME`, Qt는 `DREAMSCAPES_QT_ROOT`, 제품 SDK 경로는 `DREAMSCAPES_SDK_SOURCE_ROOT`·`DREAMSCAPES_SDK_INSTALL_ROOT`로 바꿀 수 있다. Gradle 캐시와 Android 빌드 설정도 `build/android` 아래에 둔다.

```sh
./scripts/build-android.sh
adb -s emulator-5554 install -r build/android/build/android-build/Dreamscapes.apk
adb -s emulator-5554 shell am start -W \
  -n com.iisacc.dreamscapes/org.qtproject.qt.android.bindings.QtActivity
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

`ctest --preset macos-debug`는 offscreen/software 환경에서 Main 로더의 단일 창 선택·크기 변경 후 창 유지·로더 해제 시 창 정리·데스크탑 문구의 중앙 정렬·QML 로딩 실패 시 종료 코드 `1` 요청을 검사한다. 모바일 패널은 402px 원본 치수와 iOS·Android 테마의 320·360·390·844px 폭에서 상단 배치와 버튼 겹침을 검증한다. 키 입력·메뉴 열기와 선택·Generate 및 Enter 요청·공백 입력 차단도 검사한다. QML 디스크 캐시를 끄므로 사용자 캐시에 QML 캐시를 남기지 않는다.

호스트의 실제 렌더러로 모바일 화면을 확인하고 캡처하려면 아래 명령을 사용한다. 테스트 실행 중 창이 잠시 표시되고 `build/ios-390.png`가 저장된다. 이는 호스트 창에 모바일 LVRS 테마를 적용한 미리보기이다.

```sh
QML_DISABLE_DISK_CACHE=1 DREAMSCAPES_CAPTURE_DIR="$PWD/build" \
  build/bin/DreamscapesGuiTests mobilePanelLayout:ios-390
```

또한 런타임 라이브러리·QML 검색 경로 환경변수를 제거한 별도 프로세스로 실제 앱을 실행하여 내장된 `Main` QML 진입점이 정상 로딩되는지 검증한다.

LVRS 시작 로그의 `windowCount`는 직접 QML 루트 중 창인 객체만 센다. 따라서 Loader가 루트인 Main에서는 `windowCount: 0`이 정상이다. 실제 선택된 창 한 개의 생성·표시 여부는 별도 GUI 테스트로 검증한다.

QML 모듈은 [Qt 6.8 공식 CMake 구성](https://doc.qt.io/qt-6.8/cmake-build-qml-application.html)을 따르는 LVRS 소비자 헬퍼로 등록한다. 앱 빌드와 QML 정적 검사 명령은 다음과 같다.

```sh
cmake --build build --target Dreamscapes Dreamscapes_qmllint --parallel
```

SDK 소스의 새 위치는 `Workspace/SDK`이다. CMake 기본 힌트·프리셋·CLion
프로필은 `~/.local/SDK` 설치본을 사용한다. 아직 없는 필수 SDK에 대한 구성 실패
정책은 유지한다. `cmake --fresh --preset macos-debug`로 새 경로를 검증한다.

2026-09-06에는 필수 SDK 9개가 모두 탐색되었으며, 누락되어 있던 GUI 테스트 소스도 복구했다. 모바일 테마를 사용하는 호스트 검증은 실제 iOS·Android 기기에서의 시스템 안전 영역·키보드·터치 검증을 대신하지 않는다.
