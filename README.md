# Dreamscapes

Qt 6.8.3 설치본과 로컬 LVRS 프레임워크를 사용하는 데스크탑·모바일 Qt Quick 보일러플레이트이다. 빈 창 가운데 `Hello world!`만 표시한다. 아래 제품 의존성을 필수 패키지로 선언하며, CMake가 추가 패키지를 내려받거나 설치하지는 않는다.

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

요청에서 `iiLisenseManager`로 표기된 라이브러리의 실제 설치 패키지명은 `iiLicenseManager`이다. 아직 패키지가 없는 iiCSMIDI와 Society 계열 3개도 위 이름과 타깃을 제공할 예정으로 간주하여 필수 의존성에 포함한다. 패키지가 없으면 CMake 구성 단계에서 실패하며, 선택적으로 생략하거나 빈 대체 타깃을 만들지 않는다. 이 선언만으로 각 라이브러리를 사용하는 제품 기능이 구현되는 것은 아니다.

## 소스 구조

기존 프로젝트 루트·`App`·`App/Views`를 사용하며 새 소스 디렉터리를 만들지 않는다. QWidget은 사용하지 않는다.

- `main.cpp`: LVRS 런타임을 초기화하고 항상 `Main.qml`만 연다.
- `App/Main.qml`: LVRS 런타임 플랫폼 판별로 창 하나를 선택하는 `Loader`이다. 자체 창은 만들지 않는다.
- `App/Views/Desktop.qml`: Windows·macOS·Linux용 `LV.ApplicationWindow`이다.
- `App/Views/Mobile.qml`: Android·iOS용 `LV.ApplicationWindow`이다.
- `App/tst_Gui.cpp`: Main 진입점·런타임 창 선택과 두 창의 문구·중앙 정렬·수명을 검증한다.

`Main.qml`은 LVRS 런타임의 `LV.Platform.mobile`을 사용한다. 모바일이면 `Views/Mobile.qml`, 아니면 `Views/Desktop.qml`을 로드한다. 선택된 QML의 `LV.ApplicationWindow`만 생성하며 추가 숨은 부모 창은 없다. C++에 플랫폼 분기는 없고 창 너비를 줄여도 다른 플랫폼 창으로 전환하지 않는다. 각 창은 LVRS `Label`로 문구를 표시하고 자체 창 크기·최소 크기를 관리한다. 모바일 시스템 창·안전 여백 처리는 모바일 LVRS 창에 맡긴다.

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

## 검증

의존성 검증은 `cmake --preset macos-debug`부터 시작한다. 필수 패키지가 없을 때 구성 실패와 해당 패키지의 Config 파일 안내가 출력되는지 확인한다. 모든 패키지 설치 후에는 구성·빌드·CTest가 순서대로 성공해야 한다. 구성이나 빌드가 실패한 상태에서 기존 `build/`의 테스트 바이너리가 통과하더라도 새 의존성 구성의 검증 결과로 취급하지 않는다.

2026-09-04 의존성 추가 시점에는 iiCSMIDI·iiSocietyContainer·iiSocietyHelper·iiSocietySync가 로컬에 미설치되어 있다. 기존 GUI 테스트 등록이 참조하는 `App/tst_Gui.cpp`도 작업 시작 시 소스 트리에 없으므로, 전체 GUI 테스트 재빌드에는 해당 테스트 소스가 필요하다.

`ctest --preset macos-debug`는 offscreen/software 환경에서 Main 로더가 실제 창 하나만 생성하는지, LVRS 런타임과 일치하는 창 선택, 크기 변경 후 같은 창 유지, 두 창의 표시 문구와 중앙 정렬, 로더 해제 시 창 정리를 검사한다. QML 디스크 캐시를 끄므로 테스트가 사용자 캐시 디렉터리에 QML 캐시를 남기지 않는다.

창 닫기 신호와 내부 QML 로딩 실패 시 종료 코드 `1` 요청도 검사한다.

또한 런타임 라이브러리·QML 검색 경로 환경변수를 제거한 별도 프로세스로 실제 앱을 실행하여 내장된 `Main` QML 진입점이 정상 로딩되는지 검증한다.

LVRS 시작 로그의 `windowCount`는 직접 QML 루트 중 창인 객체만 센다. 따라서 Loader가 루트인 Main에서는 `windowCount: 0`이 정상이다. 실제 선택된 창 한 개의 생성·표시 여부는 별도 GUI 테스트로 검증한다.

QML 모듈은 [Qt 6.8 공식 CMake 구성](https://doc.qt.io/qt-6.8/cmake-build-qml-application.html)을 따르는 LVRS 소비자 헬퍼로 등록한다. 앱 빌드와 QML 정적 검사 명령은 다음과 같다.

```sh
cmake --build build --target Dreamscapes Dreamscapes_qmllint --parallel
```

SDK 소스의 새 위치는 `Workspace/SDK`이다. CMake 기본 힌트·프리셋·CLion
프로필은 `~/.local/SDK` 설치본을 사용한다. 아직 없는 필수 SDK에 대한 구성 실패
정책은 유지한다. `cmake --fresh --preset macos-debug`로 새 경로를 검증한다.

이번 경로 변경 검증에서도 실제 fresh 구성은 `iiCSMIDIConfig.cmake` 부재로
실패했다. 따라서 이 단계의 결과를 전체 앱 빌드·GUI 테스트 통과로 간주하지 않는다.
