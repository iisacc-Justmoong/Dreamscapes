# Society 설치 전 공유 저장소

Dreamscapes의 기본 저장소 연결은 `iiSocietyHelper::FileSystem::openForApplication("com.iisacc.dreamscapes")`를 사용한다. 기존 선택과 Society 저장소를 우선하며, Apple에서 아직 저장소가 없으면 권한을 가진 iisacc App Group 내부에 영구 논리 디스크를 생성한다. Society 앱이 실행되거나 설치되어 있어야 할 필요가 없다.

논리 디스크는 `.societycontainer` 패키지를 강제하지 않지만 같은 섹션, 모델 목록, 생성 이미지 게시, 파일 관리 이력과 동기화 API를 제공한다. 모델 다운로드는 `Models`, 완성 이미지는 `Generation History`, 런타임 파생물은 `Models/.society-runtime/iiLocalDiffusion`에 저장된다. 앱 전용 Documents 또는 캐시로 대체하지 않는다. App Group 권한이 없거나 기존 설정이 잘못되었다면 원인을 반환한다.

Society는 공유 등록 정보를 읽어 같은 디스크를 인수한다. 원본 경로와 로컬 저장소 식별자를 유지하므로 이동·복사와 중복 저장소 생성이 필요하지 않다. 호스트에 연결하면 논리 UUID가 호스트 UUID로 바뀔 수 있지만 기기 내 저장소 식별자는 유지된다. 설치 전 생성한 파일도 첫 동기화에서 호스트에 제안하며, 기존 파일을 덮어쓰는 충돌 정책은 iiSocietyHelper가 적용한다.

명시적인 컨테이너 선택은 그대로 우선한다. 빈 선택을 다시 연결하거나 모델을 갱신할 때만 앱 소유 초기화 정책을 적용한다. 저장소를 지연 초기화하는 캔버스·뷰 모델의 수명 정책은 유지한다.

빌드 설정은 두 저장소 SDK의 `HAS_APPLICATION_STORAGE` 기능을 확인한다. 새 Container와 이전 Helper를 혼합하여 최초 동기화의 파일 보존 정책이 누락되는 구성을 거부한다. Helper는 제품이 사용하는 iiSharedCanvas의 정확한 ABI 버전으로 빌드한다.

iOS는 기존 `iiSocietyContainer_configure_ios_client`와 `Dreamscapes.entitlements`의 App Group을 사용한다. macOS의 `SocietyAppGroup`과 `Dreamscapes-Mac.entitlements`는 `SOCIETY_MAC_APP_GROUP`으로 같은 값을 생성한다. 배포 시에는 Society와 같은 팀 및 등록된 그룹 권한으로 서명해야 한다. macOS의 ad-hoc 개발 서명은 공유 그룹 자동 생성을 허용하지 않으므로 기존의 명시적 개발 저장소를 선택한다.

검증은 `DreamscapesGenerationTests sharedLogicalDiskPublishesBeforeSocietyAndSurvivesAdoption`으로 수행한다. 공유 그룹 fixture에서 모델을 읽고 실제 PNG를 게시한 뒤 Society 인수 이후에도 동일 경로와 식별자, 이미지, 모델 목록이 유지되는지 확인한다. Apple의 실제 entitlement와 App Group API 검증은 iiSocietyContainer의 iOS 플랫폼 검증으로 별도 수행한다.
