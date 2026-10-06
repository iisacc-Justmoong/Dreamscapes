<a id="dreamscapes-앱-아이콘"></a>

# Dreamscapes app icon

The original consists of `Appicon.ai`, 1024×1024, and `Artboard 1.png`. PNG uses all opaque pixels and retains the purple and blue layers along with the background. Only the dark background color `#141027`, observed at the bottom-left of the original, is used for platform-specific margins. The original file is not modified.

|Platform|Generated assets|App connection|
| --- | --- | --- |
| macOS |16 – 1024px ICNS, 1×/2× iconset, with transparent margins and rounded corners|Bundle CFBundleIconFile and Qt runtime icons|
| iPhone/iPad |Alpha-free AppIcon .appiconset, App Store 1024px|Xcode AppIcon asset catalog|
| Android |6 density legacy/round, adaptive,  API   33  monochrome, Play  512px|android:icon and android: roundIcon|
| Windows |16 – 256px with  10 size  ICO|executable  RC  resources and  Qt  runtime icons|
| Linux | 16–1024px hicolor |desktop items,  desktopFileName , install rules|
| WebAssembly |favicon, Apple touch  180px, normal·maskable  192/512px|Qt's  Dreamscapes .html and webmanifest|

iOS provides rectangular source and the operating system handles corners.  macOS  ICNS includes its own padding, corners, and soft shadows. Android places the source in the  108dp layer center's  66dp area, and monochrome converts brightness to alpha.  Windows / Linux /normal web icons preserve the full source composition.

Regeneration uses the existing workspace's  Pillow   12.3 . The installation package checks  MIT-CMU  licenses and does not add dependencies to the app runtime. Normal builds use stored assets.

```sh
python3 tools/generate_app_icons.py
python3 -B tests/test_app_icons.py
cmake --build build
ctest --test-dir build --output-on-failure
```

After modifying Illustrator files, also export and regenerate the same-named  1024×1024  full artboards.  `generated/manifest.json`  records the source and generated file's  SHA-256 ·size·color mode. If regeneration is missed after source changes, tests fail.

Android packages existing  Java ·manifest and icons into  `build/.../app-icons/android` . The source directory remains unchanged and preserves existing Activities, photo storage  FileProvider , and permissions. If the source or icon manifest changes, it automatically re-copies in the next build. Web preserves  Qt  bootstrap and does not duplicate links during iterative packaging.

Asset inspection and actual operating system execution verification are separate. This build's execution, signing, and package verification results are recorded in  `build/icon-audit/verification.json` . Development  APK  signing is separate from store distribution signing.

2026-09-12  validation passed for 82 assets, 7 auto-checked icons, and macOS CTest 6 . macOS  build, iPhone · iPad  emulator, and Android   16  arm64  emulator were replaced and execution verified. Android  APK's adaptive foreground pixels match the generation source, and the public iOS  bundle contains 19  opaque icon renditions matching independent actool compilation. Windows verified actual COFF  resource compilation, Linux verified hicolor·desktop installation, and web verified Qt   HTML  packaging. Windows / Linux / WebAssembly  full app execution must be verified in a separate environment.

Specification references: [Apple asset catalogs](https://developer.apple.com/documentation/xcode/configuring-your-app-icon), [Android adaptive icons](https://developer.android.com/develop/ui/compose/system/icon_design_adaptive), [Qt application icons](https://doc.qt.io/qt-6.8/appicon.html).

The original `.ai`  is specified as binary in `.gitattributes`  and excluded from Git line-ending normalization and text merge targets.
