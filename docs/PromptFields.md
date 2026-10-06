<a id="generation-prompt-fields"></a>

# 생성 프롬프트 필드

`PromptField.qml`는 편집 가능한 모든 생성 프롬프트(데스크톱 홈, 모바일 홈 및 결과의 공유 QuickGenerate 프롬프트, AdvancedGenerate의 프롬프트 및 부정적인 프롬프트 모두)에 대해 설치된 `LV.InputField`를 확장합니다. 검색, 사전 설정 이름 및 숫자 설정은 기존 입력 동작을 유지합니다.

텍스트는 `TextInput.Wrap`를 사용하여 사용 가능한 입력 너비로 줄 바꿈됩니다. 단어 경계가 선호되며, 단어 또는 토큰에 대한 문자 줄 바꿈은 필드보다 넓습니다. 래핑 변경 내용은 프레젠테이션에만 해당됩니다. 리터럴 프롬프트는 개행 문자를 얻지 않습니다. 창 크기 조정은 동일한 초안을 리플로우합니다.

빈 또는 단일 줄 프롬프트는 LVRS 22px 컨트롤 크기를 유지합니다. FontMetrics 는 하나의 네이티브 텍스트 줄을 감싸진 콘텐츠와 구별하며, 여러 줄은 실제 `contentHeight` 와 기존 수직 패딩을 사용합니다. 줄 제한이 없습니다. 텍스트를 제거하면 필드가 다시 줄어듭니다. 부모 레이아웃은 결과 높이 변경에 따라 다음 컨트롤들을 아래로 이동시킵니다.

컴포넌트는 LVRS 색, 재료, 포커스 링, 선택, 클립보드, 취소/재실행 및 네이티브 입력 방법 처리를 유지합니다. QuickGenerate 는 여전히 엔터로 제출하며, 자동 감싸기 자체는 절대 제출하거나 저장된 프롬프트를 변경하지 않습니다. 고급 프롬프트 편집은 기존 매개변수 저장소 및 프리셋 바인딩을 유지합니다.

가장 가까운 세로 Flickable은 텍스트 배치나 뷰포트가 바뀐 뒤 포커스된 caret을 따라간다. 수동 스크롤도 계속 사용할 수 있다. 결과 작성기는 자연 높이가 사용 가능한 창 높이를 넘으면 자체 세로 뷰포트를 가지며, 결과 도구 모음과 상태 표시 공간은 확보한다. 필드는 해당 뷰포트 안에서 계속 늘어나고 스크롤을 통해 이전 텍스트와 Generate 모두에 접근할 수 있다.

구현에서는 Qt 6.8를 사용합니다.
[TextInput.wrapMode](https://doc.qt.io/qt-6.8/qml-qtquick-textinput.html#wrapMode-prop) 및 [contentHeight](https://doc.qt.io/qt-6.8/qml-qtquick-textinput.html#contentHeight-prop) ~ LVRS의 기존 공개 별칭 SDK 변경이 필요하지 않습니다.

검증이 `Dreamscapes.PromptFields` 로 등록됩니다. GUI 사례는 248px에서 모든 3 필드, 402px 와 1280px, 영어, 한국어 및 끊김 없는 토큰, 리플로우 및 축소, 하위 소비 측 컨트롤 배치, 한국어 프리이디트/커밋, 클립보드, 취소/재실행, AdvancedGenerate 와 데스크톱/모바일 홈 및 결과에서의 커서 가시성을 모두 포함합니다. 기존 제출 및 결과 레이아웃 테스트는 엔터, 선택, 유지된 초안 및 컴팩트한 한 줄 결과 컴포저를 확인합니다.

```sh
cmake --build build --target Dreamscapes DreamscapesGuiTests Dreamscapes_qmllint -j4
ctest --test-dir build -R 'Dreamscapes\.(PromptFields|QuickGenerate|DesktopHome)$' --output-on-failure
```
