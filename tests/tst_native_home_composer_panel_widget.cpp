#include "App/Views/Home/NativeHomeComposerPanelView.h"
#include "App/Views/Home/NativeHomeComposerPromptView.h"
#include "iiGUI/WidgetState.h"
#include <iiGUI>
#include <iiSDKCore>
#include <cmath>
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
    throw std::runtime_error("Missing composer panel prompt");
}
bool exists(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view id) {
    for (const auto &node : take(state->snapshots()))
        if (node.id == id || node.id.ends_with(std::string("/") + std::string(id))) return true;
    return false;
}
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view id) {
    for (const auto &node : take(state->snapshots()))
        if (node.id == id || node.id.ends_with(std::string("/") + std::string(id))) return take(state->find(node.id));
    throw std::runtime_error("Missing composer panel widget " + std::string(id));
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
    const HomeComposerLayoutMetrics metrics{10,5,24,40,50}; // LVUI tokens replace these initial values during layout.
    auto model = HomeComposerPanelModel::create(composer,prompt,sources,spec,metrics);
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {1227,300}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerPanel(builder, {}, model); }));
    (void)take(state->drawing_snapshot({1227,300})); (void)take(state->snapshots());
    if (std::abs(model->contentWidth.get()-1203) >= .01 || model->hasAttachments.get())
        throw std::runtime_error("native composer initial: content=" + std::to_string(model->contentWidth.get()) +
                                 " attachments=" + std::to_string(model->hasAttachments.get()));
    auto editor = input(state); good(editor.focus()); good(state->input({.kind=WidgetInputKind::text,.text="한글"}));
    good(root.set_extent({360,600})); (void)take(state->drawing_snapshot({360,600})); (void)take(state->snapshots());
    check(take(editor.snapshot()).focused && composer->snapshot.get().prompt == "한글" && edits == 1 &&
          std::abs(model->contentWidth.get()-336) < .01, "native composer resizing lost its prompt or focus");
    auto next = composer->snapshot.get(); next.video = true; next.attachments = {{"a","one.png","",""}};
    next.errorText = "This is a long generation error explaining why the submitted settings were rejected and how the user can change them safely.";
    good(composer->setSnapshot(next)); (void)take(state->drawing_snapshot({360,600})); (void)take(state->snapshots());
    check(exists(state,"homeComposerReferences") && exists(state,"homeComposerError") && model->layout.get().settings[8].visible,
          "native composer did not show its attachment, error and video areas");
    const auto error = take(find(state,"homeComposerErrorText").snapshot());
    check(error.text == next.errorText && error.bounds.height > 11 && model->errorHeight.get() > 11,
          "native composer error did not wrap or report its measured height");
    next.errorText.clear(); good(composer->setSnapshot(next)); (void)take(state->drawing_snapshot({360,600}));
    check(!model->hasError.get() && !exists(state,"homeComposerErrorText"), "cleared composer error remained visible");
    state->close(); model->close(); prompt->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
