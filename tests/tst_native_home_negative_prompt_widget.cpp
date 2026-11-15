#include "App/Views/Home/NativeHomeNegativePromptView.h"
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
    throw std::runtime_error("Missing negative prompt widget " + std::string(id));
}
ii::gui::Widget input(const std::shared_ptr<ii::gui::detail::WidgetState> &state) {
    for (const auto &node : take(state->snapshots())) if (node.text_input) return take(state->find(node.id));
    throw std::runtime_error("Missing negative prompt input");
}
void exercise() {
    using namespace ii::gui;
    auto composer = HomeComposerModel::create(); int edits = 0;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        check(command.kind == HomeComposerCommandKind::editNegativePrompt, "negative editor sent another command");
        ++edits; auto snapshot = composer->snapshot.get(); snapshot.negativePrompt = command.value;
        return composer->setSnapshot(std::move(snapshot));
    }));
    auto prompt = HomeComposerPromptModel::create(composer, HomeComposerTextRole::negativePrompt, "", "Negative prompt", "", false);
    auto model = HomeNegativePromptModel::create(prompt, "Negative prompt", "Done");
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {500,300}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeNegativePrompt(builder, {}, model); }));
    (void)take(state->drawing_snapshot({500,300})); good(find(state,"homeNegativeButton").activate());
    check(take(find(state,"homeNegativePopover").snapshot()).opened, "negative prompt popover did not open");
    good(input(state).focus()); good(state->input({.kind=WidgetInputKind::text,.text="blur\nnoise"}));
    good(find(state,"homeNegativeDone").activate());
    check(composer->snapshot.get().negativePrompt == "blur\nnoise" && edits == 1 &&
          !take(find(state,"homeNegativePopover").snapshot()).opened, "negative edit was lost or submitted");
    good(root.set_extent({240,200})); (void)take(state->drawing_snapshot({240,200}));
    good(find(state,"homeNegativeButton").activate()); (void)take(state->drawing_snapshot({240,200}));
    const auto popup = take(find(state,"homeNegativePopover").snapshot()).bounds;
    const auto field = take(input(state).snapshot()).bounds;
    check(field.width >= 100 && field.height >= 22 && popup.x >= 0 && popup.x + popup.width <= 240 && field.x >= popup.x &&
          field.x + field.width <= popup.x + popup.width, "negative editor escaped the narrow popover");
    check(composer->snapshot.get().negativePrompt == "blur\nnoise" && edits == 1, "opening the popup changed the draft");
    good(find(state,"homeNegativeDone").activate()); model->close();
    check(!take(find(state,"homeNegativeButton").snapshot()).enabled, "closed negative popup remained active");
    state->close(); prompt->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
