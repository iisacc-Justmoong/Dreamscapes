#include "App/Views/Home/NativeHomeComposerPromptView.h"
#include "iiGUI/WidgetState.h"
#include <iiSDKCore/Runtime/FrameLoop.h>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
template<class T> T take(ii::core::Result<T> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
    return std::move(*value);
}
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view id) {
    for (const auto &node : take(state->snapshots()))
        if (node.id == id || node.id.ends_with(std::string("/") + std::string(id))) return take(state->find(node.id));
    throw std::runtime_error("Missing composer prompt widget " + std::string(id));
}
ii::gui::Widget input(const std::shared_ptr<ii::gui::detail::WidgetState> &state) {
    for (const auto &node : take(state->snapshots())) if (node.text_input) return take(state->find(node.id));
    throw std::runtime_error("Missing composer text input");
}
void exercise() {
    using namespace ii::gui;
    auto composer = HomeComposerModel::create(); int edits = 0, submissions = 0;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        if (command.kind == HomeComposerCommandKind::submit) { ++submissions; return {}; }
        if (command.kind != HomeComposerCommandKind::editPrompt) return {};
        ++edits; auto snapshot = composer->snapshot.get(); snapshot.prompt = command.value;
        return composer->setSnapshot(std::move(snapshot));
    }));
    auto model = HomeComposerPromptModel::create(composer, HomeComposerTextRole::prompt, "Describe what to create…", "Prompt", "Clear prompt", true);
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {400,60}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerPrompt(builder, {}, model); }));
    (void)take(state->drawing_snapshot({400,60}));
    auto editor = input(state); good(editor.focus());
    good(state->input({.kind=WidgetInputKind::text,.text="한글\nsecond"}));
    check(composer->snapshot.get().prompt == "한글\nsecond" && model->text.get() == "한글\nsecond" && edits == 1 && submissions == 0,
          "native editor did not deliver plain multiline UTF-8");
    good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter,.meta=true}));
    good(state->input({.kind=WidgetInputKind::key_up,.key=WidgetKey::enter,.meta=true}));
    check(submissions == 1 && composer->snapshot.get().prompt == "한글\nsecond" && edits == 1,
          "Command-Return did not submit once or altered the prompt");
    good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter,.control=true}));
    good(state->input({.kind=WidgetInputKind::key_up,.key=WidgetKey::enter,.control=true}));
    check(submissions == 2 && edits == 1, "Control-Return did not submit once");
    good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter}));
    good(state->input({.kind=WidgetInputKind::key_up,.key=WidgetKey::enter}));
    check(submissions == 2 && edits == 2 && composer->snapshot.get().prompt == "한글\nsecond\n",
          "plain Return did not remain a multiline edit");
    (void)take(state->drawing_snapshot({400,60}));
    check(model->requestedHeight.get() >= 22 && take(find(state,"homeClearPrompt").snapshot()).accessibility_name == "Clear prompt",
          "prompt measurement or accessible clear action is absent");
    good(find(state,"homeClearPrompt").activate());
    check(composer->snapshot.get().prompt.empty() && model->text.get().empty() && edits == 3 && submissions == 2,
          "clear action did not edit the authoritative draft");
    auto external = composer->snapshot.get(); external.prompt = "outside\nupdate"; good(composer->setSnapshot(external));
    (void)take(state->drawing_snapshot({400,60}));
    const auto nativeInput = input(state);
    const auto actual = take(nativeInput.text_content({0,take(nativeInput.snapshot()).text_length})).text;
    if (actual != "outside\nupdate" || edits != 3) {
        throw std::runtime_error("external update: widget=" + actual + " model=" + model->text.get() + " edits=" + std::to_string(edits));
    }
    good(composer->setHandler({}));
    check(!take(input(state).snapshot()).enabled, "retired controller left the prompt editable");
    state->close(); model->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
