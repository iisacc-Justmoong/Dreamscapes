#include "App/Views/Home/NativeHomeComposerChoiceView.h"
#include "iiGUI/WidgetState.h"
#include <iiGUI>
#include <iiSDKCore>
#include <LVUI>
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
void selectionClosesMenuAfterSynchronousUpdate() {
    using namespace ii::gui;
    auto composer = HomeComposerModel::create(); HomeComposerSnapshot snapshot;
    snapshot.modelId = "a"; snapshot.modelLabel = "Model A";
    snapshot.choices.models = {{"a", "Model A"}, {"b", "A longer model B"}};
    good(composer->setSnapshot(snapshot));
    good(composer->setHandler([&](const HomeComposerCommand &command) -> ii::core::Result<void> {
        auto next = composer->snapshot.get();
        next.modelId = command.value; next.modelLabel = command.value == "a" ? "Model A" : "A longer model B";
        return composer->setSnapshot(next);
    }));
    auto model = HomeComposerChoiceModel::create(composer, HomeComposerChoiceRole::model, "Model", "Choose model");
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {640,480}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerChoice(builder, {}, model); }));
    (void)take(state->drawing_snapshot({640,480}));
    good(find(state,"homeChoiceButton").activate());
    check(take(find(state,"homeChoiceMenu").snapshot()).opened, "choice menu did not open");
    good(item(state,"A longer model B").activate());
    check(take(find(state,"homeChoiceButton").snapshot()).text == "A longer model B", "selection was not applied");
    check(!take(find(state,"homeChoiceMenu").snapshot()).opened, "choice menu remained open after selection updated synchronously");
    good(find(state,"homeChoiceButton").activate());
    (void)take(state->drawing_snapshot({640,480}));
    const auto bounds = take(item(state,"Model A").snapshot()).bounds;
    good(state->input({.kind=WidgetInputKind::pointer_down,.x=bounds.x+bounds.width/2,.y=bounds.y+bounds.height/2,.buttons=1}));
    good(state->input({.kind=WidgetInputKind::pointer_up,.x=bounds.x+bounds.width/2,.y=bounds.y+bounds.height/2}));
    check(take(find(state,"homeChoiceButton").snapshot()).text == "Model A", "pointer selection did not reach the menu item");
    check(!take(find(state,"homeChoiceMenu").snapshot()).opened, "choice menu remained open after pointer selection");
    state->close(); model->close(); composer->close();
}
void menuFitsLongestVisibleOption() {
    using namespace ii::gui;
    const std::string longLabel = "iisacc Standard Photo Stock Corrected.safetensors";
    auto composer = HomeComposerModel::create(); HomeComposerSnapshot snapshot;
    snapshot.modelId = "a"; snapshot.modelLabel = "Short";
    snapshot.choices.models = {{"a", "Short"}, {"b", longLabel, false}, {"c", "긴 이름 모델.safetensors"}};
    good(composer->setSnapshot(snapshot));
    good(composer->setHandler([](const HomeComposerCommand &) -> ii::core::Result<void> { return {}; }));
    auto model = HomeComposerChoiceModel::create(composer, HomeComposerChoiceRole::model, "Model", "Choose model");
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {1000,600}));
    good(root.set_content([&](WidgetBuilder &builder) { return buildHomeComposerChoice(builder, {}, model); }));
    (void)take(state->drawing_snapshot({1000,600}));
    good(find(state,"homeChoiceButton").activate());
    (void)take(state->drawing_snapshot({1000,600}));
    const lvui::MenuItemConfiguration style;
    const auto text = take(measure_text_layout({.text=longLabel,.font_size=float(style.font_size),
                                               .font_weight=style.font_weight,.font_family=take(state->appearance()).font_family,
                                               .line_height=float(style.line_height)}));
    const auto wide = take(item(state,longLabel).snapshot()).bounds.width;
    if (wide < text.width + 2 * style.horizontal_padding)
        throw std::runtime_error("menu truncated its longest option: row=" + std::to_string(wide) +
            " text=" + std::to_string(text.width) + " desired=" + std::to_string(model->menuItemWidth.get()) +
            " menu=" + std::to_string(take(find(state,"homeChoiceMenu").snapshot()).bounds.width));
    auto next=composer->snapshot.get(); next.choices.models={{"a","Short"},{"c","긴 이름"}};
    good(composer->setSnapshot(next)); (void)take(state->drawing_snapshot({1000,600}));
    const auto narrow=take(item(state,"긴 이름").snapshot()).bounds.width;
    check(narrow < wide, "menu did not shrink after the longest option was removed");
    next.choices.models = {{"a","Short"},{"c","긴 이름"},{"b",longLabel,false}};
    good(composer->setSnapshot(next)); (void)take(state->drawing_snapshot({1000,600}));
    check(take(item(state,longLabel).snapshot()).bounds.width >= text.width + 2 * style.horizontal_padding,
          "menu did not expand when a long option was added");
    state->close(); model->close(); composer->close();
}
}
int main() { try { exercise(); selectionClosesMenuAfterSynchronousUpdate(); menuFitsLongestVisibleOption(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
