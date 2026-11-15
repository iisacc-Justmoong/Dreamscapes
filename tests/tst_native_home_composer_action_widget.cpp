#include "App/Views/Home/NativeHomeComposerActionView.h"
#include "iiGUI/WidgetState.h"
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
    throw std::runtime_error("Missing action widget " + std::string(id));
}
void exercise() {
    using namespace ii::gui;
    auto composer = HomeComposerModel::create(); HomeComposerSnapshot snapshot; snapshot.outputCount = 1;
    good(composer->setSnapshot(snapshot)); int calls = 0; HomeComposerCommandKind last{};
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> { ++calls; last = command.kind; return {}; }));
    auto attach = HomeComposerActionModel::create(composer, HomeComposerActionRole::attach, {"", "", "Attach images", {}});
    auto paint = HomeComposerActionModel::create(composer, HomeComposerActionRole::paint, {"Paint canvas", "", "Paint canvas", {}});
    auto advanced = HomeComposerActionModel::create(composer, HomeComposerActionRole::advanced,
        {"Advanced parameters", "More controls", "Advanced parameters", {}});
    auto generate = HomeComposerActionModel::create(composer, HomeComposerActionRole::generate,
        {"", "", "Generate", [](int count, bool video) { return std::to_string(count) + (video ? " videos" : " images"); }});
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {180,44}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerAction(builder, {}, attach); }));
    (void)take(state->drawing_snapshot({180,44}));
    auto icon = find(state,"homeActionIcon"); check(take(icon.snapshot()).accessibility_name == "Attach images", "attach action lacks an accessible label");
    good(icon.activate()); check(calls == 1 && last == HomeComposerCommandKind::chooseAttachments, "attach button did not dispatch");
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerAction(builder, {}, paint); }));
    (void)take(state->drawing_snapshot({180,44})); good(find(state,"homeActionDefault").activate());
    check(calls == 2 && last == HomeComposerCommandKind::paint, "paint button did not dispatch");
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerAction(builder, {}, advanced); }));
    (void)take(state->drawing_snapshot({180,44})); good(find(state,"homeActionDefault").activate());
    check(calls == 3 && last == HomeComposerCommandKind::advanced, "advanced button did not dispatch");
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerAction(builder, {}, generate); }));
    (void)take(state->drawing_snapshot({180,44}));
    auto button = find(state,"homeActionPrimary"); check(take(button.snapshot()).text == "1 images", "generate label did not reflect the draft");
    snapshot.video = true; snapshot.outputCount = 4; good(composer->setSnapshot(snapshot));
    check(take(button.snapshot()).text == "4 videos", "generate label did not update");
    good(button.activate()); check(calls == 4 && last == HomeComposerCommandKind::submit, "generate button did not dispatch");
    snapshot.generationPending = true; good(composer->setSnapshot(snapshot));
    check(!take(button.snapshot()).enabled, "busy button remained enabled");
    (void)button.activate();
    check(calls == 4, "busy button allowed duplicate generation");
    state->close(); attach->close(); paint->close(); advanced->close(); generate->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
