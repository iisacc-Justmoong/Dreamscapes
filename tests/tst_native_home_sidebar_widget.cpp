#include "App/Views/Home/NativeHomeSidebarView.h"
#include "iiGUI/WidgetState.h"
#include <iiSDKCore/Runtime/FrameLoop.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace dreamscapes;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) { if (!result) { if (result.error().cause) std::rethrow_exception(result.error().cause); throw std::runtime_error(result.error().message); } }
template<class T> T take(ii::core::Result<T> result) { if (!result) { if (result.error().cause) std::rethrow_exception(result.error().cause); throw std::runtime_error(result.error().message); } return std::move(*result); }
ii::core::Result<ii::gui::Widget> find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, const std::string &id) {
    auto nodes = state->snapshots(); if (!nodes) return std::unexpected(nodes.error());
    for (const auto &node : *nodes) if (node.id == id || node.id.ends_with("/" + id)) return state->find(node.id);
    return ii::core::fail(ii::core::ErrorCode::not_found, "Missing widget " + id);
}
ii::core::Result<ii::gui::Widget> findAction(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view name) {
    auto nodes = state->snapshots(); if (!nodes) return std::unexpected(nodes.error());
    for (const auto &node : *nodes) if (node.id.ends_with("/sidebarAction") && node.accessibility_name == name) return state->find(node.id);
    return ii::core::fail(ii::core::ErrorCode::not_found, "Missing sidebar action");
}
void exercise() {
    using namespace ii::gui;
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {181, 300}));
    auto model = home::HomeSidebarModel::create({{"home", "Home", "nodeshomeFolder"}, {"divider1", "", "", true},
        {"image", "Image", "unconditionalImageGeneration"}, {"history", "Generation History", "profileCPU"}});
    good(model->setSelected("home")); std::string activated; int calls = 0;
    model->setHandler([&](std::string key) { activated = std::move(key); ++calls; });
    good(root.set_content([&](WidgetBuilder &builder) { return home::buildHomeSidebar(builder, {}, model); }));
    (void)take(state->drawing_snapshot({181, 300}));
    auto home = take(findAction(state, "Home"));
    auto image = take(findAction(state, "Image"));
    check(take(home.snapshot()).checked && take(image.snapshot()).accessibility_name == "Image" && take(image.snapshot()).text == "Image",
        "sidebar selection or accessibility name is wrong");
    good(image.activate()); check(activated == "image" && calls == 1 && model->selected.get() == "home", "action did not reach controller");
    good(model->setSelected("image")); check(take(image.snapshot()).checked, "external selection did not update widget");
    good(model->setCompact(true));
    (void)take(state->drawing_snapshot({48, 300})); (void)take(take(find(state, "sidebarSurface")).snapshot());
    check(take(image.snapshot()).accessibility_name == "Image" && take(image.snapshot()).text.empty() && model->rows.get()[2].label.empty(),
        "compact sidebar lost its accessible name or retained its label");
    const auto imageSnapshot = take(image.snapshot());
    const auto prefix = imageSnapshot.id.substr(0, imageSnapshot.id.size() - std::string("sidebarAction").size());
    auto tooltip = take(state->find(prefix + "sidebarTooltip"));
    good(state->input({.kind=WidgetInputKind::pointer_move,.x=imageSnapshot.bounds.x+8,.y=imageSnapshot.bounds.y+8}));
    std::this_thread::sleep_for(std::chrono::milliseconds(650)); (void)events.poll();
    check(take(tooltip.snapshot()).opened, "compact action tooltip did not open on hover");
    good(state->input({.kind=WidgetInputKind::pointer_move,.x=170,.y=280}));
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); (void)events.poll();
    good(image.focus()); std::this_thread::sleep_for(std::chrono::milliseconds(650)); (void)events.poll();
    check(take(tooltip.snapshot()).opened, "compact action tooltip did not open on keyboard focus");
    good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter}));
    good(state->input({.kind=WidgetInputKind::key_up,.key=WidgetKey::enter}));
    check(calls == 2 && activated == "image", "keyboard did not activate the focused action");
    (void)take(state->drawing_snapshot({48, 48})); (void)take(take(find(state, "sidebarSurface")).snapshot());
    auto history = take(findAction(state, "Generation History"));
    good(history.focus());
    check(take(take(find(state, "sidebarScroll")).snapshot()).scroll_offset > 0, "focusing an offscreen action did not reveal it");
    state->close(); check(!image.snapshot(), "closed sidebar retained a widget");
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
