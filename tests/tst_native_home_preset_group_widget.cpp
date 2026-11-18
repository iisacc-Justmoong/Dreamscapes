#include "App/Views/Home/NativeHomePresetGroupView.h"
#include "App/Views/Home/NativeHomeComposerChoiceView.h"
#include "App/Views/Home/NativeHomePresetSaveView.h"
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
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view id) {
    for (const auto &node : take(state->snapshots()))
        if (node.id == id || node.id.ends_with(std::string("/") + std::string(id))) return take(state->find(node.id));
    throw std::runtime_error("Missing preset group widget " + std::string(id));
}
ii::gui::Widget input(const std::shared_ptr<ii::gui::detail::WidgetState> &state) {
    for (const auto &node : take(state->snapshots())) if (node.text_input) return take(state->find(node.id));
    throw std::runtime_error("Missing preset name editor");
}
void exercise() {
    using namespace ii::gui;
    auto composer = HomeComposerModel::create(); HomeComposerSnapshot snapshot;
    snapshot.choices.presets = {{"Studio", "Studio"}}; good(composer->setSnapshot(snapshot));
    int loads = 0, saves = 0;
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        if (command.kind == HomeComposerCommandKind::loadPreset) { check(command.value == "Studio", "wrong preset loaded"); ++loads; }
        if (command.kind == HomeComposerCommandKind::savePreset) { check(command.value == "New", "wrong preset saved"); ++saves; }
        return {};
    }));
    auto choice = HomeComposerChoiceModel::create(composer, HomeComposerChoiceRole::preset, "Load preset", "Load preset", false);
    auto save = HomePresetSaveModel::create(composer, "Save preset", "Name", "Save", "Cancel");
    auto choiceSource = ii::gui::compiled_view_source([choice](WidgetBuilder &builder) { return buildHomeComposerChoice(builder, {}, choice); });
    auto saveSource = ii::gui::compiled_view_source([save](WidgetBuilder &builder) { return buildHomePresetSave(builder, {}, save); });
    auto model = HomePresetGroupModel::create("Preset", choiceSource, saveSource);
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {228,300}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomePresetGroup(builder, {}, model); }));
    (void)take(state->drawing_snapshot({228,300})); (void)take(state->snapshots());
    check(model->choiceWidth.get() == 172 && model->saveWidth.get() == 52, "preset controls did not match the Figma row");
    good(find(state,"homeChoiceButton").activate()); good(find(state,"homeChoiceEntry").activate());
    check(loads == 1, "preset load menu did not dispatch through the group");
    good(find(state,"homeSavePreset").activate()); good(input(state).focus());
    good(state->input({.kind=WidgetInputKind::text,.text="New"}));
    good(root.set_extent({180,300})); (void)take(state->drawing_snapshot({180,300})); (void)take(state->snapshots());
    check(save->draft.get() == "New" && take(input(state).snapshot()).focused, "preset resize lost the open draft or focus");
    good(find(state,"homePresetSave").activate()); check(saves == 1 && save->draft.get().empty(), "preset save did not dispatch through the group");
    state->close(); model->close(); choice->close(); save->close(); composer->close();
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
