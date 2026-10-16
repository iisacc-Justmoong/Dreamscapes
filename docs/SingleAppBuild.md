<a id="single-app-build-contract"></a>

# 단일 앱 빌드 계약

macOS Release는 [성능 및 런타임 구성](Performance.md)의 Apple 추론 프로필과 compact 배포를 사용한다. `python3 scripts/build-macos-runtime.py --jobs 8`로 제품 전용 iiLocalDiffusion을 준비한 뒤 앱을 빌드한다. 미사용 Qt 모듈·스타일·디버그 자료 및 중복 LVRS 자산을 제외하고, 실제 실행 파일과 플러그인·헬퍼의 의존 관계에 필요한 런타임을 포함한다. 아키텍처 축소와 로컬 심볼 제거 후 전체 번들을 다시 서명한다.

에디터 빌드는 iiSharedCanvas 0.30.2, iiPaintEngine 0.4.1 및 iiSocietyHelper 0.8.2 패키지를 사용한다. 앱과 Helper가 동일한 캔버스 헤더·라이브러리를 사용해야 한다. iiSharedCanvas 0.29.0의 iiPaintEngine 0.1 계열 의존 계약과 iiPaintEngine 0.4 계열 설치본을 혼용하지 않는다. 기존 macOS platform 설치 경로를 사용하는 소비자도 같은 iiPaintEngine 버전으로 갱신해야 한다. SDK 릴리스 검증 중에는 `build/runtime-sdk/<SDK>`에 설치한 패키지를 `iiSharedCanvas_DIR`와 `iiSocietyHelper_DIR`로 명시할 수 있다. 앱 번들은 선택된 패키지의 런타임을 함께 포함한다.

제품은 [워크스페이스 단일 앱 정책](../../build-policy/README.md)를 사용합니다. `build/` 아래에 오직 정통 애플리케이션 번들이 생성됩니다. 테스트와 헬퍼는 일반 실행 파일이며, 런타임 배포 및 패키징은 원위치로 작동합니다. 정통 경로, 플랫폼 전환 및 검증 명령에 대한 정책을 참조하십시오.

macOS의 `cmake/single-app/deploy_macos.py`는 앱의 canonical 경로를 기준으로 배포 전체를 프로세스 간 `fcntl.flock`으로 직렬화한다. 예를 들어 `build/bin/Dreamscapes.app`의 잠금 파일은 번들 밖의 `build/bin/.Dreamscapes.app.deploy.lock`이다. 다른 이름의 symlink가 같은 앱을 가리켜도 같은 잠금을 사용한다. 같은 앱을 대상으로 하는 배포는 dylib relocation, QML 삭제·복사, framework·plugin 갱신, 전체 서명과 최종 서명 검증이 끝날 때까지 기다린다. 다른 앱 경로의 배포는 병렬로 실행할 수 있다.

잠금 파일은 종료나 실패 후에도 삭제하지 않는다. 이미 기다리는 프로세스와 이후 프로세스가 같은 inode를 사용해야 하기 때문이다. 배포 본문이 예외를 발생시켜도 잠금은 해제되며 다음 프로세스가 다시 획득할 수 있다. 이 잠금은 갱신된 배포 스크립트를 통해 진입한 bundle writer에 적용된다. 이미 구버전 스크립트로 실행 중인 배포와 별도 컴파일러·외부 번들 writer의 변경은 이 잠금에 참여하지 않는다.

잠금 계약은 다음 명령으로 검증한다. 임시 앱, marker, log는 모두 `build/` 아래에 생성하고 테스트가 정리한다. 테스트는 실제 subprocess와 파일 잠금을 사용하지만 `macdeployqt`, `install_name_tool`, `codesign`을 실행하지 않는다. 따라서 실제 배포·서명 완료 증거와 구분한다.

```sh
python3 -B -m unittest discover -s tests -p test_deploy_macos_lock.py -v
```

선택적 `DREAMSCAPES_LOCAL_RUNTIME_PROBE` 빌드는 Documents/society-models-verification.json 에 실행 중인 생성 컨트롤러의 모델 메타데이터 및 저장 상태를 기록하기 위해 `--inspect-society-models` 를 허용합니다. 이 읽기 전용 검사 모드는 절대 생성을 대기열에 추가하거나 모델을 선택하거나 모델 다운로드를 요청하지 않습니다. observedAt 를 확인하여 구식 장치 보고서를 거부합니다.

`DreamscapesGuiTests packagedApplicationStarts`는 `build/` 아래에 실제 `.societycontainer` fixture를 생성하고 그 패키지의 `.society-runtime` 안에 Helper 관찰 디렉터리와 앱 endpoint 디렉터리를 준비한다. 자식 프로세스에는 `SOCIETY_CONTAINER_PATH`, `SOCIETY_HELPER_DIRECTORY`, `IILOCALLLM_APP_ENDPOINTS`와 `--society-container`를 같은 fixture 기준으로 전달한다. Helper의 디렉터리 override는 선택된 패키지의 runtime 안에 있어야 하므로 bare 임시 디렉터리는 사용하지 않는다. 이 검증은 QML 로딩, 단일 창, 두 프로세스의 Helper 발견을 확인하며 사용자 저장소나 이미 실행 중인 앱을 재시작하지 않는다. `DREAMSCAPES_TEST_APP_PATH`에 설치된 실행 파일을 지정하면 같은 계약으로 설치본을 검증한다.

File 패널 빌드 검증은 iiSharedCanvas 0.30.2와 이를 정확히 참조하는 iiSocietyHelper 0.8.2를 `build/runtime-sdk`에 설치해 사용한다. iiGraphicAdjust는 앱의 기존 0.4.0 계약에 맞는 `SDK/iiGraphicAdjust/build/stage` 패키지를 사용한다. 별도 설치본의 더 최신 패키지로 자동 교체하지 않는다.
