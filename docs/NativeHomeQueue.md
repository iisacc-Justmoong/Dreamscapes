# Home 생성 큐의 LVUI 이전

## 현재 범위

`NativeHomeQueue.lvui`는 Dreamscapes Home의 기존 생성 큐를 LVUI와 iiGUI로 표현하는
독립 화면이다. 디자인 기준은 Dreamscapes Figma의 `1467:5052`이며, 큐 상태·실행·대기·
실패 건수, 디바이스·모델·실행 상태와 큐 열기 동작을 제공한다. 상태 표시의 SVG는
Figma에서 추출한 `resources/design/Home/queue-ready.svg`이다. 화면은 LVUI의 컴포넌트와
공용 토큰을 사용한다.

macOS 제품 진입점은 이 화면을 기존 Home 큐 영역에 연결한다. 다른 화면이나 모달에
가려지거나 안전한 배치를 만들 수 없으면 기존 큐가 표시된다. 현재 제품의 시작 경로와
Home의 상위 레이아웃은 여전히 Qt와 LVRS의 QML 창이며, 다른 운영체제의 제품 연결과
전체 화면 전환은 남아 있다. 이 문서의 검사 결과는 앱 전체의 QML 제거를 뜻하지 않는다.

## 연결 계약

`HomeQueueModel`은 작업 상태 스냅샷과 큐 열기 핸들러를 받는다. `running`,
`connecting-host`, `downloading`은 실행 중, `queued`는 대기, `failed`는 실패로 집계한다.
완료·취소 및 알 수 없는 상태는 이 건수에 포함하지 않는다. 명시된 상태 문구가 없으면
실행·대기 여부에 따라 기본 문구를 표시한다. 모델 이름이 없으면 `Unavailable`로
표시하며 기기 이름은 호출자가 제공한다. 문자열과 건수 포매터는 호스트가 현지화된
값을 주입할 수 있다. 입력은 소유 스레드에서 전달하며 작업 배열이 아니거나 재진입한
갱신은 거부한다.

`buildHomeQueue`는 호스트가 소유한 뷰모델과 자원 저장소에서 읽은 SVG 바이트를 받는다.
화면 자체는 파일을 읽거나 생성 상태를 변경하지 않는다. 큐 열기는 주입된 핸들러를
호출하고 핸들러가 없으면 비활성화된다. `NativeHomeQueueBridge`는 기존
`GenerationProjection`의 작업과 상태를 화면에 전달하는 임시 Qt 어댑터이다. 모델·
디바이스 이름과 큐 열기 동작은 상위 화면에서 공급한다. 투영 객체가 파괴되거나
연결이 해제되면 동작을 비활성화한다. 네이티브 뷰와 모델은 Qt에 의존하지 않는다.
제품 연결은 Home의 실제 모델명과 장치명 변경을 구독하고, View queue를 기존 탐색
신호로 전달한다. 상태 아이콘은 제품 리소스에서 읽는다. 언어 변경 시 화면을 다시
만들고, 종료 시에는 공통 네이티브 표면의 닫힘을 비동기로 기다린다.

`NativeHomeQueueSession`은 QQuick 위젯을 요구하지 않고 기존 WorkspaceSession을
생성 투영 및 Home 뷰모델에 연결한다. 큐 열기 명령은 호스트가 제공하므로 현재
제품의 탐색 신호와 향후 독립 네이티브 창의 명령을 같은 세션에 연결할 수 있다.
세션이 종료되거나 원본이 파괴되면 뷰에 남은 큐 열기 핸들러도 비활성화한다.

## 검증

기존 `build/` 구성에서 다음을 실행한다. `DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR`은 선택된
iiGUI 바이너리와 일치하는 내부 테스트 헤더 경로여야 한다. 이 헤더가 없는 배포 SDK에서는
위젯·렌더러 경계 검사가 생성되지 않는다.

```sh
cmake --build build --target DreamscapesNativeHomeQueueTests DreamscapesNativeHomeQueueBridgeTests DreamscapesNativeHomeQueueWidgetTests DreamscapesNativeHomeQueueIntegrationTests --parallel 4
ctest --test-dir build -R '^Dreamscapes\.NativeHomeQueue' --output-on-failure
```

순수 모델 검사는 상태 집계·잘못된 입력·현지화·소유 스레드·동작 해제를 확인한다.
어댑터 검사는 실제 생성 투영과 기존 Home의 집계를 비교하고 투영 파괴 후 콜백이
호출되지 않는지 확인한다. 창 없는 iiGUI 위젯 검사는 SVG와 라벨 렌더링, 값 변경과
버튼 전달을 확인한다. 통합 검사는 생성 투영에서 LVUI 위젯으로의 갱신과 위젯의 큐 열기
동작이 전환용 어댑터를 통해 전달되는지 확인한다. macOS의 렌더러 경계 검사는 생성된 위젯과 테스트 실행 파일에
Qt·LVRS 링크가 없는지 확인한다. 실제 앱 실행·설치 및 다른 플랫폼의 시각적 일치는
이 검사에 포함되지 않는다.
`Dreamscapes.NativeHomeQueuePresenter`는 실제 QML 메타객체를 포함한 테스트에서
모델·장치 변경, 큐 열기, 모달, 언어 변경과 비동기 종료를 검사한다. 공통 표면은
`Dreamscapes.NativeEmbeddedSurfaceMac`의 숨긴 테스트 창에서 검사한다.
`Dreamscapes.NativeHomeQueueWindow`는 QML 창 없이 실제 생성·작업 세션을 네이티브
창에 연결한다. 테스트 생성 큐는 일시 중지되어 실제 모델 추론을 수행하지 않는다.
