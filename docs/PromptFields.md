# Generation prompt fields

`PromptField.qml` extends the installed `LV.InputField` for all editable generation
prompts: the shared QuickGenerate prompt on desktop Home, mobile Home and results,
and both Prompt and Negative Prompt in AdvancedGenerate. Search, preset names and
numeric settings keep their existing input behavior.

Text wraps to the available input width using `TextInput.Wrap`: word boundaries
are preferred, with character wrapping for a word or token wider than the field.
Wrapping changes presentation only; the literal prompt does not gain newline
characters. Window resizing reflows the same draft.

An empty or single-line prompt retains the LVRS 22px control size. FontMetrics
distinguishes one native text line from wrapped content; multiple lines use the
actual `contentHeight` plus the existing vertical padding. There is no line cap.
Removing text shrinks the field again. Its parent layout moves following controls
down by the resulting height change.

The component retains LVRS colors, material, focus ring, selection, clipboard,
undo/redo and native input-method handling. QuickGenerate still submits with Enter;
automatic wrapping itself never submits or changes the stored prompt. Advanced
prompt edits keep their existing parameter-store and preset bindings.

The nearest vertical Flickable follows the focused caret after text layout or
viewport changes. Manual scrolling remains available. The result composer has its
own vertical viewport when its natural height exceeds the available window, with
space reserved for the result toolbar and status. The field continues to grow
inside that viewport, and scrolling reaches both earlier text and Generate.

The implementation uses Qt 6.8
[TextInput.wrapMode](https://doc.qt.io/qt-6.8/qml-qtquick-textinput.html#wrapMode-prop)
and [contentHeight](https://doc.qt.io/qt-6.8/qml-qtquick-textinput.html#contentHeight-prop)
through LVRS's existing public aliases; it does not require an SDK change.

Validation is registered as `Dreamscapes.PromptFields`. GUI cases cover all three
fields at 248px, 402px and 1280px, English, Korean and unbroken tokens, reflow and
shrinking, downstream control placement, Korean preedit/commit, clipboard,
undo/redo, and caret visibility in AdvancedGenerate and desktop/mobile Home and
results. Existing submission and result-layout tests check Enter,
selection, retained drafts and the compact one-line result composer.

```sh
cmake --build build --target Dreamscapes DreamscapesGuiTests Dreamscapes_qmllint -j4
ctest --test-dir build -R 'Dreamscapes\.(PromptFields|QuickGenerate|DesktopHome)$' --output-on-failure
```
