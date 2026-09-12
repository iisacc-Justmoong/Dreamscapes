# Dreamscapes 앱 아이콘

원본은 `Appicon.ai`와 1024×1024 `Artboard 1.png`이다. PNG는 모든 픽셀이 불투명하며, 보라색·파란색 레이어와 배경을 그대로 사용한다. 플랫폼별 여백에만 원본 왼쪽 아래에서 확인한 짙은 배경색 `#141027`을 사용한다. 원본 파일은 수정하지 않는다.

| 플랫폼 | 생성 자산 | 앱 연결 |
| --- | --- | --- |
| macOS | 16–1024px ICNS, 1×/2× iconset, 투명 여백과 모서리 | 번들 CFBundleIconFile 및 Qt 런타임 아이콘 |
| iPhone/iPad | 알파 없는 AppIcon.appiconset, App Store 1024px | Xcode AppIcon 에셋 카탈로그 |
| Android | 6개 밀도의 legacy/round, adaptive, API 33 monochrome, Play 512px | android:icon 및 android:roundIcon |
| Windows | 16–256px의 10개 크기를 담은 ICO | 실행 파일 RC 리소스 및 Qt 런타임 아이콘 |
| Linux | 16–1024px hicolor | desktop 항목, desktopFileName, 설치 규칙 |
| WebAssembly | favicon, Apple touch 180px, 일반·maskable 192/512px | Qt의 Dreamscapes.html 및 webmanifest |

iOS에는 사각형 원본을 제공하고 운영체제가 모서리를 처리한다. macOS ICNS에는 자체 여백·모서리·약한 그림자를 포함한다. Android는 108dp 레이어 중앙의 66dp 영역에 원본을 배치하며, monochrome은 명도를 알파로 변환한다. Windows/Linux/일반 웹 아이콘은 전체 원본 구도를 유지한다.

재생성에는 기존 작업 환경의 Pillow 12.3을 사용한다. 설치 패키지에서 MIT-CMU 라이선스를 확인했으며, 앱 런타임에 의존성을 추가하지 않는다. 일반 빌드는 저장된 자산을 사용한다.

```sh
python3 tools/generate_app_icons.py
python3 -B tests/test_app_icons.py
cmake --build build
ctest --test-dir build --output-on-failure
```

Illustrator 파일을 수정한 뒤 PNG도 같은 이름의 1024×1024 전체 아트보드로 내보내고 재생성한다. `generated/manifest.json`이 원본과 생성 파일의 SHA-256·크기·색상 모드를 기록한다. 원본 변경 후 재생성을 빠뜨리면 테스트가 실패한다.

Android는 기존 Java·매니페스트와 아이콘을 `build/.../app-icons/android`에 모아 패키징한다. 원본 디렉터리는 변경하지 않으며 기존 Activity, 사진 저장용 FileProvider와 권한을 보존한다. 원본 또는 아이콘 manifest가 바뀌면 다음 빌드에서 자동으로 다시 복사한다. 웹은 Qt 부트스트랩을 보존하고 반복 패키징 시 링크를 중복 삽입하지 않는다.

자산 검사와 실제 운영체제 실행 검증은 별개이다. 이번 빌드의 실행·서명·패키지 검증 결과는 `build/icon-audit/verification.json`에 기록한다. 개발용 APK 서명은 스토어 배포용 서명과 별개이다.

2026-09-12 검증에서 자산 82개, 아이콘 자동 검사 7개와 macOS CTest 6개가 통과했다. macOS 실행본, iPhone·iPad 실기기와 Android 16 arm64 에뮬레이터에 교체 적용하고 실행을 확인했다. Android APK의 adaptive foreground 픽셀은 생성 원본과 일치하며, 공용 iOS 번들에는 독립적인 actool 컴파일과 일치하는 불투명 아이콘 렌디션 19개가 들어 있다. Windows는 실제 COFF 리소스 컴파일, Linux는 hicolor·desktop 설치, 웹은 Qt HTML 패키징을 검증했다. Windows/Linux/WebAssembly 전체 앱 실행은 별도 환경에서 검증해야 한다.

규격 근거: [Apple 에셋 카탈로그](https://developer.apple.com/documentation/xcode/configuring-your-app-icon), [Android adaptive icons](https://developer.android.com/develop/ui/compose/system/icon_design_adaptive), [Qt 앱 아이콘](https://doc.qt.io/qt-6.8/appicon.html).

Illustrator `.ai` 원본은 `.gitattributes`에서 바이너리로 지정하여 Git의 줄바꿈 정규화·텍스트 병합 대상에서 제외한다.
