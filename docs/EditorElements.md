# Editor Elements panels

The Elements tool implements the ten supplied Figma frames from the Dreamscapes design file. The former all-in-one 18-field panel is replaced by the same icon and state navigation used by the editor's Text, Layer and Graphic panels.

| View | Figma node | Visible controls |
|---|---|---|
| Rectangle | [585:94650](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=585-94650&m=dev) | Size, constrain proportions, corner radius, corner smoothing |
| Ellipse | [650:15792](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=650-15792&m=dev) | Size, constrain proportions, arc sweep, inner radius |
| Polygon | [650:15793](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=650-15793&m=dev) | Size, constrain proportions, sides, star mode, corner smoothing |
| Line / Arrow | [650:15794](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=650-15794&m=dev) | Size, constrain proportions, stroke summary and stroke navigation |
| Fill | [650:15795](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=650-15795&m=dev) | Fill type, color, opacity, blend, application scope |
| Stroke | [650:15796](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=650-15796&m=dev) | Width, color, location, cap and start/end markers |
| Selection | [650:15797](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=650-15797&m=dev) | Live selection count, Select all, Inspect/Multiple/Clear navigation |
| Inspect | [662:16150](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=662-16150&m=dev) | Active shape, size, shape-specific geometry, fill color and stroke width |
| Multiple | [662:16151](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=662-16151&m=dev) | Multiple-selection switch, scope, live rows and checkboxes, select all/invert/clear |
| Clear | [662:16152](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=662-16152&m=dev) | Empty selection, live unchecked rows and recovery actions |

## Presentation and assets

`EditorElementsPanel.qml` is mounted by both `EditorDesktopPanel.qml` and `EditorToolSheet.qml`. The four header tabs are Selection, Shape, Fill and Stroke. The Shape tab opens the view corresponding to the current shape mode in the tool's `selector` setting. The Selection tab exposes Inspect, Multiple and Clear. View navigation is stored independently from the tool's editable values, so changing a view, folding the panel or switching tools preserves its draft and does not rewrite document geometry.

During a tool transition, the outgoing Elements panel remains bound to its own draft and rejects edits once Elements is no longer active. Native accepted-selection settings synchronize after the current QML binding evaluation completes, and an unchanged draft is not reassigned; focus-loss commits and selection notifications therefore cannot write into another tool or synchronously re-enter the values binding.

Header tabs, shape and selection palettes, and parameter choices bind the actual LVRS button `checkable` and `checked` properties to their authoritative view or accepted field value. Header tabs keep the Borderless tone; shape, selection and parameter choices keep Default. Hover, press, click and persistent selection never change that tone. Selection holds the same tone's pressed background, with disabled state taking precedence. Repeating a selected navigation button does not clear it, and an Image choice remains at its accepted fill while the source picker is open. Accessibility toggle actions use the ordinary button activation path, preserving the native checked state. The shared editor contract and regression coverage are documented in [EditorButtonStates.md](EditorButtonStates.md).

The original reference frame is 432×559px, with a 398px content column and 17px inset including the border. The panel composes installed LVRS controls: a 30px icon header, 18px icons, the original 16px reset icon, 72px function palettes with 34px icon buttons, 22px inputs and mini sliders, 28px color pickers and 36px selection rows separated by 8px. Widths adapt to the editor's existing resizable dock and mobile sheet; overflow stays in the existing vertical viewport.

All exported Elements SVGs are local files under `Assets/Elements`. Their bytes and SVG root dimensions are retained. Slider thumb, optional ticks/focus-ring and dropdown variants remain separate assets; the Figma screenshots are verification references and are never used as implementation textures. The independent `fixtures/ElementsPanelsFigma.json` records each supplied frame, its visible controls, defaults, measured bounds, source hashes and exported asset metadata.

## Real document state and editing

The selection count and rows come from the current native document. Candidates are read from the active artboard's actual layers in top-to-bottom order. They include Elements created in the current editing session, including their gradient/image bitmap fills, and other native vector layers. Ordinary bitmap/photo layers, session-created text, other artboards and the editor background are excluded. A blank canvas has zero candidates; the three Rectangle/Ellipse/Polygon rows in Figma are represented only when such real shapes exist. Selecting, adding/removing selection, Select all, Invert and Clear preserve the document bytes and undo history. Inspect follows the active candidate and uses its actual shape controls. Foreign or locked candidates can be identified and selected but cannot be edited through unsupported geometry controls.

Multiple manages the selected IDs and the active element. Geometry, fill, stroke, opacity and blend edits apply only to the active editable element while retaining the other selected IDs. Multiple does not apply edits to every selected layer; the scope selector controls which candidates selection actions operate on.

Parametric Elements editing relies on authoring metadata retained in the current editing session and its Undo/Redo snapshots. Saving preserves the actual native paths, pixels, transforms, opacity and blend. Reopening does not restore the original Rectangle/Ellipse/Polygon/Line parameters or image-fill sources. Reopened solid shapes remain selectable as general `Vector` candidates with parametric editing disabled, even when their layer names still say Rectangle or Ellipse. Reopened gradient/image fills remain ordinary bitmap layers and are excluded from Elements candidates. See [ElementsSelection.md](ElementsSelection.md) for the complete selection, session metadata, history and image-source contract.

The controls retain the existing Elements field IDs and native editing pipeline. Rectangle/Ellipse/Polygon/Line geometry, fill, stroke, arc/star settings, opacity and blend update the selected editable element. A shape-palette click chooses its actual shape mode; changing only a view does not change the shape. Numeric validation restores the last accepted value on invalid input. Both size axes accept 1–65536px; proportional editing rejects an otherwise valid entered axis if the derived other axis exceeds that range. Color picker acceptance commits the color and cancellation preserves it. Image fill retains the requested Image settings while the source picker is open; cancel preserves the last accepted fill, draft and document. Reset restores the appropriate view controls. See [EditorToolBehavior.md](EditorToolBehavior.md) and [EditorNativeCanvas.md](EditorNativeCanvas.md) for document and render semantics.

Numeric and hexadecimal inputs become modified only after a real `textEdited` event. Focus loss accepts a value only when that input was modified; `accepted` and explicit `commit()` retain their validation path. An initial binding or an unedited focus change does not emit an edit. Rounded display strings are presentation: a native shape measuring 24.5×16.5 px can display 25 px for its first axis without changing its actual geometry when the user selects Color.

## Focused verification

`tests/test_elements_panels.py` checks all ten requested node IDs, independently saved field lists and reference geometry, the high-fidelity Figma evidence when available, every exported SVG's bytes/root dimensions, and desktop/mobile routing to the dedicated view.

`desktopEditorElementsMatchesFigma` uses a real three-shape native document. It checks all ten visible field sets, reference geometry and exported icon render sizes, captures each 432×559px state, and exercises view navigation, actual selection actions, numeric and slider input, both-axis size-limit validation, Image fill source settings and cancellation, color acceptance/cancellation, Reset and retained state. The existing dock-resize and mobile-sheet tests have a dedicated Elements branch; the obsolete unified Elements row is excluded from the historical generic-panel fixture.

`editorColorToolSelectionDoesNotEditDocument` adds `color-fractional-elements-desktop` and `color-fractional-elements-mobile`. Each creates an actual 24.5×16.5 px native vector through the backend, focuses the first size input without typing, then selects Color and traverses its panel states. Complete IISC bytes, rendered pixels, exact size, revision, selection and the existing Undo/Redo branch must remain unchanged; the retained Redo is executed at the end. These cases are host regressions, and their pass status comes from the execution log.

```sh
cmake --build build --target DreamscapesGuiTests --parallel 4
ctest --test-dir build -R '^Dreamscapes\.(EditorElementsCatalog|EditorElements|EditorLayout|EditorPanelResize|EditorToolPanels)$' --output-on-failure
ctest --test-dir build -R '^Dreamscapes.EditorColorPassiveSelection$' --output-on-failure
```

Set `DREAMSCAPES_CAPTURE_DIR` to a directory inside `build/` to retain `elements-<view>.png` and responsive/mobile captures. Native Cocoa captures and installed-application checks are separate from offscreen GUI regressions. A macOS window using the Android layout verifies responsive presentation; it does not prove physical iOS or Android input/runtime behavior.

## 2026-10-09 verification

The current backend run passed all five focused CTest targets: the six-case
Elements catalog, thirteen Elements selection/history/gesture behavior cases,
EditorTools, EditorCanvas and EditorProject. The Elements behavior target reports
15 QtTest passes including initialization and cleanup, with no failures or skips.
The GUI run passed seven of eight CTest targets. `Dreamscapes.EditorElements`
passed all 15 QtTest cases including setup/cleanup; Layout, PanelResize, Layers,
Text, CanvasGui and MultiCanvasGui also passed. The only failed target was
`Dreamscapes.EditorToolPanels`: ten generic preview-disclosure icon positions
were `(344, 22.5)` instead of `(250, 13)`. That path is distinct from the dedicated
Elements view. No pre-change runtime baseline is claimed for this failure.

Native Cocoa/Metal captures completed all ten frame assertions, including the
actual control accessibility checked state. A fresh pixel/asset review confirmed
the original selected/default button surfaces and icon placements. The subsequent
native interaction test did not complete: the common click helper failed to
activate its test window twice, followed by the dependent Image source-dialog
visibility assertion. The offscreen run did complete Image selection and cancel,
color acceptance/cancel, selection actions, numeric/slider edits and state
restoration. Physical iOS/Android input is not covered.

The raw backend, GUI, native activation and visual evidence is preserved in
`build/elements-figma-20261009/`. The final bundle fingerprint, startup and
installation evidence is recorded separately in `final-verification.json`;
source/offscreen results alone do not prove an installed artifact.

## Historical unified-panel evidence

The following evidence refers to the former `353:22614` unified sample and is retained as history, not as the current ten-view baseline.

2026-10-03 macOS·Qt 6.8.3 검증에서 앱과 GUI 테스트 빌드가 완료되었다. `Dreamscapes.EditorElements`는 CTest 1/1 통과, 내부 QtTest 집계는 초기화·정리를 포함해 15 passed / 0 failed이다. `packagedApplicationStarts`는 canonical 앱의 실제 진입점·QML 로드·SDK 발견을 확인했으며 3 passed / 0 failed이다. `codesign --verify --deep --strict`와 공통 정책의 앱 감사도 통과했다. `build/bin/Dreamscapes.app`만 존재하며 중첩 `.app`은 없다.

GUI 회귀 검사는 `offscreen`·소프트웨어 렌더러 환경에서 수행했다. 기존 Qt Labs Platform 메뉴의 native Menu 경고는 이 환경에서도 출력되며, 네이티브 메뉴 동작 자체는 이 검증의 대상이 아니다. 실행 중인 사용자 앱을 재시작하지 않고 별도 테스트 프로세스로 검증했다.

## Selection transform

The canvas now presents a free-transform grid with eight resize handles and a
rotation handle for the active Element. Pointer selection uses actual painted
geometry, and the inspector reads that shape's transformed dimensions rather than
creation defaults. See [ElementTransform.md](ElementTransform.md) for gestures,
keyboard input, imported-vector behavior and validation.
