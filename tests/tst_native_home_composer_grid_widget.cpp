#include "App/Views/Home/NativeHomeComposerGrid.h"
#include "App/Views/Home/NativeHomeComposerPromptView.h"
#include "iiGUI/WidgetState.h"
#include <iiGUI>
#include <iiSDKCore>
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
ii::gui::Widget input(const std::shared_ptr<ii::gui::detail::WidgetState> &state) {
    for (const auto &node : take(state->snapshots())) if (node.text_input) return take(state->find(node.id));
    throw std::runtime_error("Missing composer grid prompt");
}
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view id) {
    for (const auto &node : take(state->snapshots()))
        if (node.id == id || node.id.ends_with(std::string("/") + std::string(id))) return take(state->find(node.id));
    throw std::runtime_error("Missing composer grid widget " + std::string(id));
}
void exercise() {
    using namespace ii::gui;
    auto composer = HomeComposerModel::create(); int edits = 0;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        if (command.kind == HomeComposerCommandKind::editPrompt) {
            ++edits; auto snapshot = composer->snapshot.get(); snapshot.prompt = command.value;
            return composer->setSnapshot(std::move(snapshot));
        }
        return {};
    }));
    auto prompt = HomeComposerPromptModel::create(composer, HomeComposerTextRole::prompt, "Prompt", "Prompt", "Clear", true);
    auto fixture = ii::gui::compiled_view_source([](WidgetBuilder &builder) -> ii::core::Result<void> {
        auto label = builder.label({}, "fixture", {.text="Control"});
        return label ? ii::core::Result<void>{} : std::unexpected(label.error());
    });
    auto promptSource = ii::gui::compiled_view_source([prompt](WidgetBuilder &builder) { return buildHomeComposerPrompt(builder, {}, prompt); });
    HomeComposerPanelSources sources; sources.top.fill(fixture); sources.top[1] = promptSource;
    sources.settings.fill(fixture); sources.attachments = fixture;
    const HomeComposerLayoutSpec spec{1000,600,400,{88,1,22,93,138,145},{327,88,144,96,120,228,152}};
    const HomeComposerLayoutMetrics metrics{8,12,22,37,44};
    auto model = HomeComposerPanelModel::create(composer,prompt,sources,spec,metrics); good(model->setViewport(1227,400));
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {1203,180}));
    auto top = homeComposerGridSource(model, HomeComposerGrid::top);
    good(root.set_content([&](WidgetBuilder &builder) { return top->build(builder); }));
    (void)take(state->drawing_snapshot({1203,180})); auto editor = input(state); good(editor.focus());
    good(state->input({.kind=WidgetInputKind::text,.text="한글"}));
    check(composer->snapshot.get().prompt == "한글" && edits == 1, "native prompt failed in composer grid");
    const auto before = take(editor.snapshot());
    good(model->setViewport(360,400)); good(root.set_extent({336,300})); (void)take(state->drawing_snapshot({336,300}));
    const auto after = take(editor.snapshot());
    check(after.focused && after.text_length == before.text_length && edits == 1,
          "composer breakpoint replaced its prompt or lost editing state");
    auto settings = homeComposerGridSource(model, HomeComposerGrid::settings);
    good(root.set_extent({336,400}));
    good(root.set_content([&](WidgetBuilder &builder) { return settings->build(builder); }));
    (void)take(state->drawing_snapshot({336,400}));
    check(!take(find(state,"homeComposerSetting7").snapshot()).visible, "image composer showed a video control");
    auto next = composer->snapshot.get(); next.video = true; good(composer->setSnapshot(next));
    (void)take(state->drawing_snapshot({336,400}));
    const auto seventh = take(find(state,"homeComposerSetting7").snapshot());
    const auto eighth = take(find(state,"homeComposerSetting8").snapshot());
    if (!seventh.visible || !eighth.visible)
        throw std::runtime_error("video settings visibility: layout=" + std::to_string(model->layout.get().settings[7].visible) +
            " widgets=" + std::to_string(seventh.visible) + "," + std::to_string(eighth.visible));
    state->close(); model->close(); prompt->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
