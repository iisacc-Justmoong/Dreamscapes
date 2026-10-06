<a id="mobile-navigation-icon-contract"></a>

# 모바일 내비게이션 아이콘 계약

도구 모음은 Dreamscapes Figma `bn8O4AHKr1X9DWnhR1TgEy`, 노드 `103:1211`(2026-09-28)에서 정확하고 수정되지 않은 SVG 내보내기를 사용합니다. 자산은 `src/App/Views/Home/Assets/Navigation/`에 있으며 애플리케이션 리소스 컬렉션으로 컴파일되므로 설치된 장치는 Figma URL을 가져오지 않습니다.

|슬롯|Figma 소스|로컬 SVG|아트웍 크기|
| --- | --- | --- | --- |
|홈| `.MobileNavigation/Icon/home-1` | home.svg | 24 × 24 |
|도구| `collection` | tools.svg | 24 × 24 |
|Storage|`sqlFile` / 데이터베이스| storage.svg | 17.5 × 20.5545 |
|알림| `.MobileNavigation/Icon/toolwindownotifications` | notification.svg | 24 × 24 |
|계정|`role`(라이트 변형)| account.svg | 24 × 24 |
|검색| `.MobileNavigation/Icon/inputFieldSearch` | search.svg | 24 × 24 |

네비게이션은 `LV.MobileNavigationBar` 로 유지되며, 각 항목은 `preserveIconColors` 에 옵인하고 기존 일반 단색 모드는 LVRS 기본값으로 유지됩니다. 스토어지는 동일한 24px 아이콘 슬롯에 중앙 정렬된 내보낸 작품 차원을 사용하며, 데이터베이스 글리프를 전체 슬롯에 늘리지 않습니다. SVG 기하 구조나 루트 차원은 다시 작성되지 않습니다. 홈의 선택된 파란색과 다른 아이콘의 원래 색상/불투명도는 Figma 내보내기 파일에 포함되며, 홈은 `autoSelect: false` 를 사용합니다.

회귀 : `DreamscapesGuiTests mobileHomeUsesFigmaSectionsLimitsAndLvrsNavigation` 는 모든 5 탭 소스, 비어 있지 않은 자산, 이미지 로딩, 페인팅된 차원, 소스 색상 보존 및 별도의 검색 소스를 확인합니다. `DREAMSCAPES_MOBILE_HOME_CAPTURE` 를 설정하여 호스트 렌더링된 모바일 화면을 포착합니다. `python3 -B tests/test_mobile_navigation_assets.py` 는 6 내보내기 해시, SVG 루트 차원, 소스 호출 사이트 및 리소스 등록을 잠급니다. LVRS `LVRSTests_mobile_navigation source_icon_colors_and_artwork_geometry` 는 재사용 가능한 속성, 변경되지 않은 기본값, 검색 전파, 및 비활성화된 알파를 테스트합니다. 호스트 스크린샷은 물리적 장치 설치 증거가 아닙니다; 서명된 iOS 번들, 장치 설치, 및 프로세스 시작을 별도로 확인하세요.

<a id="verification--2026-09-28"></a>

## 확인 — 2026-09-28

- LVRS 모바일 내비게이션 테스트: 7는 통과했으며, 0는 실패했습니다; 선택적 갤러리 캡처 테스트는 건너뛰었습니다. macOS와 iOS LVRS 라이브러리가 모두 재구축되어 설치되었습니다.
- 정확한 SVG 자산 계약: 내보낸 6개 전체가 통과했다.
- Dreamscapes GUI 회귀 를 다시 구축: 3 통과, 0 실패, 0 건너뛰기, 5 탭 이미지 로딩/차원 주장을 포함하여 별도의 검색 소스.
- Dreamscapes macOS 빌드와 번들 서명이 완료된 후 정규 `build/bin/Dreamscapes.app` 출력을 iOS로 전환합니다.
- iOS 종속성 설치가 이 작업에서 소스를 변경하지 않고 iiSocietyContainer 0.14.1 및 iiSocietyHelper 0.7.2로 새로 고침되었습니다.
- iOS 릴리스 빌드가 런타임 프로브가 비활성화된 상태로 성공했습니다. 짝지어진 iPhone 15 Pro Max 에 대한 완전한 `tests/verify_ios_bundle.py` 검사가 통과했습니다.
- 기존 `com.iisacc.dreamscapes` 설치를 제거하지 않고 업데이트했으며, 성공적으로 실행하고 물리 장치 스크린샷을 확인했습니다. 모든 6 툴바 아이콘은 내보낸 형태, 색상 및 비율을 유지합니다.

`build/` 하단의 로컬 증거: `mobile-toolbar-gui-test.log`, `mobile-toolbar-ios-build.log`, `mobile-toolbar-ios-verification.json`, `mobile-toolbar-install.json`, `mobile-toolbar-after-launch.json`, 및 `mobile-toolbar-after-app.png` 입니다. `mobile-toolbar-before-app.png` 는 실제 업데이트 전 장치 스크린샷이며, `mobile-toolbar-host.png` 는 오직 호스트 렌더링입니다. 이 확인은 도구 모음 및 앱 설치/시작을 포함하며, 이미지 생성 또는 저장 동기화 전체 테스트를 포함하지 않습니다.
