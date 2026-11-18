#include "App/Views/Home/NativeHomePresetSaveView.h"
#include "iiGUI/WidgetState.h"
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
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view id) {
    for (const auto &node : take(state->snapshots()))
        if (node.id == id || node.id.ends_with(std::string("/") + std::string(id))) return take(state->find(node.id));
    throw std::runtime_error("Missing preset widget " + std::string(id));
}
ii::gui::Widget input(const std::shared_ptr<ii::gui::detail::WidgetState> &state) {
    for (const auto &node : take(state->snapshots())) if (node.text_input) return take(state->find(node.id));
    throw std::runtime_error("Missing preset input");
}
void exercise() {
    using namespace ii::gui;
    auto composer = HomeComposerModel::create(); int attempts = 0;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        ++attempts; if (command.value == "retry") return ii::core::fail(ii::core::ErrorCode::failed, "Unavailable");
        check(command.kind == HomeComposerCommandKind::savePreset && command.value == "Studio", "wrong preset command"); return {};
    }));
    auto model = HomePresetSaveModel::create(composer, "Save preset", "Name", "Save", "Cancel");
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {400,400}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomePresetSave(builder, {}, model); }));
    (void)take(state->drawing_snapshot({400,400})); good(find(state,"homeSavePreset").activate());
    check(take(find(state,"homePresetSavePopover").snapshot()).opened && !take(find(state,"homePresetSave").snapshot()).enabled,
          "empty preset editor did not open disabled");
    good(input(state).focus()); good(state->input({.kind=WidgetInputKind::text,.text="retry"}));
    good(find(state,"homePresetSave").activate());
    check(attempts == 1 && model->draft.get() == "retry" && take(find(state,"homePresetSavePopover").snapshot()).opened,
          "failed save lost its draft or closed the editor");
    good(find(state,"homePresetCancel").activate()); good(find(state,"homeSavePreset").activate());
    check(model->draft.get() == "retry" && attempts == 1, "cancel mutated the draft or saved a preset");
    good(input(state).focus()); good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::select_all}));
    good(state->input({.kind=WidgetInputKind::text,.text="Studio"})); good(find(state,"homePresetSave").activate());
    check(attempts == 2 && model->draft.get().empty() && !take(find(state,"homePresetSavePopover").snapshot()).opened,
          "successful preset save failed to clear and close");
    good(composer->setHandler({})); check(!take(find(state,"homeSavePreset").snapshot()).enabled, "detached preset remained active");
    state->close(); model->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
