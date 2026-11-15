#include "App/Views/Home/NativeHomeComposerChoiceView.h"
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
    throw std::runtime_error("Missing composer choice widget " + std::string(id));
}
ii::gui::Widget item(const std::shared_ptr<ii::gui::detail::WidgetState> &state, const std::string &label) {
    for (const auto &node : take(state->snapshots()))
        if (node.id.ends_with("/homeChoiceEntry") && node.text == label) return take(state->find(node.id));
    throw std::runtime_error("Missing composer choice " + label);
}
void exercise() {
    using namespace ii::gui;
    auto composer = HomeComposerModel::create(); HomeComposerSnapshot snapshot;
    snapshot.modelId = "a"; snapshot.modelLabel = "Model A";
    snapshot.choices.models = {{"a", "Model A"}, {"b", "Model B"}, {"c", "Unavailable", false}};
    good(composer->setSnapshot(snapshot));
    int selected = 0; std::string key;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        ++selected; key = command.value; return {};
    }));
    auto model = HomeComposerChoiceModel::create(composer, HomeComposerChoiceRole::model, "Model", "Choose model");
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {320,60}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerChoice(builder, {}, model); }));
    (void)take(state->drawing_snapshot({320,60}));
    check(take(find(state,"homeChoiceHeading").snapshot()).text == "Model" &&
          take(find(state,"homeChoiceButton").snapshot()).text == "Model A", "choice labels were not projected");
    good(find(state,"homeChoiceButton").activate());
    check(take(find(state,"homeChoiceMenu").snapshot()).opened, "choice menu did not open");
    auto before = item(state,"Model B");
    auto next = composer->snapshot.get(); next.choices.models = {{"b", "Model B"}, {"a", "Model A"}};
    good(composer->setSnapshot(next));
    check(take(before.snapshot()).text == "Model B", "reordered menu item was unnecessarily replaced");
    good(before.activate());
    check(selected == 1 && key == "b", "menu activated a stale position instead of the stable model key");
    next.modelId = "b"; next.modelLabel = "Model B"; good(composer->setSnapshot(next));
    check(take(find(state,"homeChoiceButton").snapshot()).text == "Model B", "controller selection was not reflected");
    good(composer->setHandler({}));
    check(!take(find(state,"homeChoiceButton").snapshot()).enabled, "detached controller left model menu active");
    state->close(); model->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
