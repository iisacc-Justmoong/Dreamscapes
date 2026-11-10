<a id="mobile-navigation-icon-contract"></a>

# 모바일 내비게이션 아이콘 계약

모바일 내비게이션은 Dreamscapes Figma `103:1211`의 화면 구성과 LVRS의 공통 아이콘 및 상태 색상을 사용합니다. 아이콘은 제품 전용 SVG 사본이 아니라 LVRS 리소스 이름으로 조회합니다.

| 슬롯 | LVRS 아이콘 | 아트웍 크기 |
| --- | --- | --- |
| 홈 | `home` | 24 × 24 |
| 도구 | `collection-asset` | 24 × 24 |
| Storage | `sqlFile` | 24 × 24 |
| 알림 | `toolwindownotifications` | 24 × 24 |
| 계정 | `role` | 24 × 24 |
| 검색 | `generalsearch` | 24 × 24 |

`LV.MobileNavigationBar`가 소유하는 기본·선택·비활성화 전경색을 사용하며, 선택된 악센트 아이콘은 LVRS의 한 단계 어두운 역할을 따른다. 각 아이콘은 같은 24px 슬롯에서 LVRS 도형 비율을 유지한다. `autoSelect: false`와 기존 목적지 신호 계약은 유지한다.

`DreamscapesGuiTests mobileHomeUsesFigmaSectionsLimitsAndLvrsNavigation`은 다섯 탭과 검색의 LVRS 리소스, 로딩, 크기 및 공통 상태 색상을 확인한다. `tests/test_mobile_navigation_assets.py`는 제품별 SVG나 자체 색상 보존 옵션이 재도입되지 않는지 검사한다. 이 호스트 테스트는 물리적 iOS·Android 기기 실행을 대신하지 않는다.

<a id="verification--2026-09-28"></a>

## 과거 확인 — 2026-09-28

다음은 당시 제품별 SVG를 사용했던 과거 검증 기록이며, 위의 현재 리소스 계약이나 이번 변경의 기기 검증을 뜻하지 않는다.

- LVRS 모바일 내비게이션 테스트: 7는 통과했으며, 0는 실패했습니다; 선택적 갤러리 캡처 테스트는 건너뛰었습니다. macOS와 iOS LVRS 라이브러리가 모두 재구축되어 설치되었습니다.
- 정확한 SVG 자산 계약: 내보낸 6개 전체가 통과했다.
- Dreamscapes GUI 회귀 를 다시 구축: 3 통과, 0 실패, 0 건너뛰기, 5 탭 이미지 로딩/차원 주장을 포함하여 별도의 검색 소스.
- Dreamscapes macOS 빌드와 번들 서명이 완료된 후 정규 `build/bin/Dreamscapes.app` 출력을 iOS로 전환합니다.
- iOS 종속성 설치가 이 작업에서 소스를 변경하지 않고 iiSocietyContainer 0.14.1 및 iiSocietyHelper 0.7.2로 새로 고침되었습니다.
- iOS 릴리스 빌드가 런타임 프로브가 비활성화된 상태로 성공했습니다. 짝지어진 iPhone 15 Pro Max 에 대한 완전한 `tests/verify_ios_bundle.py` 검사가 통과했습니다.
- 기존 `com.iisacc.dreamscapes` 설치를 제거하지 않고 업데이트했으며, 성공적으로 실행하고 물리 장치 스크린샷을 확인했습니다. 모든 6 툴바 아이콘은 내보낸 형태, 색상 및 비율을 유지합니다.

`build/` 하단의 로컬 증거: `mobile-toolbar-gui-test.log`, `mobile-toolbar-ios-build.log`, `mobile-toolbar-ios-verification.json`, `mobile-toolbar-install.json`, `mobile-toolbar-after-launch.json`, 및 `mobile-toolbar-after-app.png` 입니다. `mobile-toolbar-before-app.png` 는 실제 업데이트 전 장치 스크린샷이며, `mobile-toolbar-host.png` 는 오직 호스트 렌더링입니다. 이 확인은 도구 모음 및 앱 설치/시작을 포함하며, 이미지 생성 또는 저장 동기화 전체 테스트를 포함하지 않습니다.
