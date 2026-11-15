#include "App/Views/Home/NativeHomeRecentFilesView.h"
#include "iiGUI/WidgetState.h"
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace dreamscapes;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) {
    if (!result) { if (result.error().cause) std::rethrow_exception(result.error().cause); throw std::runtime_error(result.error().message); }
}
template<class T> T take(ii::core::Result<T> result) {
    if (!result) { if (result.error().cause) std::rethrow_exception(result.error().cause); throw std::runtime_error(result.error().message); }
    return std::move(*result);
}
ii::core::Result<ii::gui::Widget> find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, const std::string &id) {
    auto nodes = state->snapshots(); if (!nodes) return std::unexpected(nodes.error());
    for (const auto &node : *nodes) if (node.id == id || node.id.ends_with("/" + id)) return state->find(node.id);
    return ii::core::fail(ii::core::ErrorCode::not_found, "Missing widget " + id);
}
vm::Value file(std::string key, std::string name, bool local = true) {
    return vm::Value(vm::Object{{"id", vm::Value(std::move(key))}, {"name", vm::Value(std::move(name))},
        {"available", vm::Value(local)}, {"opaque", vm::Value(vm::Object{{"id", vm::Value(42)}})}});
}
class Images final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    mutable std::vector<std::string> requests;
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view source, std::uint32_t, std::uint32_t) const override {
        requests.emplace_back(source); return {};
    }
};
void exercise() {
    using namespace ii::gui;
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {700, 440}));
    auto model = home::HomeRecentFilesModel::create(); auto images = std::make_shared<Images>();
    std::string selected; vm::Value opened(vm::Object{}); int browsed = 0;
    model->setHandlers({.select = [&](std::string key) { selected = std::move(key); },
        .open = [&](std::string, vm::Value value) { opened = std::move(value); }, .browse = [&] { ++browsed; }});
    good(root.set_content([&](WidgetBuilder &builder) { return home::buildHomeRecentFiles(builder, {}, model, images); }));
    (void)take(state->drawing_snapshot({700, 440}));
    check(take(take(find(state, "recentEmpty")).snapshot()).text == "No recent files", "empty state is missing");
    const auto first = file("a", "A.iisc"), second = file("b", "B.iisc", false);
    good(model->setFiles(vm::Value(vm::Array{first, second}), {"file:///a", "file:///b"}, false));
    (void)take(state->drawing_snapshot({700, 440}));
    auto list = take(find(state, "recentFiles")); auto viewport = take(find(state, "recentFiles/viewport"));
    (void)take(viewport.snapshot());
    const auto a = model->tokenForKey("a"), b = model->tokenForKey("b");
    auto firstRow = take(list.keyed_content_item(a, "recentRow"));
    auto secondRow = take(list.keyed_content_item(b, "recentRow"));
    check(take(take(list.keyed_content_item(a, "recentName")).snapshot()).text == "A.iisc" && !images->requests.empty(),
        "list did not render the supplied file or preview");
    const auto bounds = take(secondRow.snapshot()).bounds;
    const auto click = [&](double time, std::uint32_t count) {
        good(state->input({.kind=WidgetInputKind::pointer_down,.x=bounds.x+10,.y=bounds.y+10,.timestamp=time,.buttons=1,.native_click_count=count}));
        good(state->input({.kind=WidgetInputKind::pointer_up,.x=bounds.x+10,.y=bounds.y+10,.timestamp=time+.01}));
    };
    click(1, 1); check(selected == "b" && model->rows.get()[1].selected, "row click did not select the file");
    const auto nameBounds = take(take(list.keyed_content_item(a, "recentName")).snapshot()).bounds;
    good(state->input({.kind=WidgetInputKind::pointer_down,.x=nameBounds.x+2,.y=nameBounds.y+2,.timestamp=1.05,.buttons=1,.native_click_count=1}));
    good(state->input({.kind=WidgetInputKind::pointer_up,.x=nameBounds.x+2,.y=nameBounds.y+2,.timestamp=1.06}));
    check(selected == "a", "clicking the file name did not select its row");
    click(1.1, 1); click(1.2, 2); check(opened == second, "double click did not open the original remote file");
    good(list.focus());
    good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::up}));
    check(selected == "a", "keyboard navigation did not update the selection");
    good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter}));
    check(opened == first, "Enter did not open the selected file");
    good(model->setSelectedKey("b"));
    good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter}));
    check(opened == second && model->rows.get()[1].selected, "external selection did not synchronize keyboard navigation");
    good(model->setSelectedKey("a"));
    good(take(find(state, "recentFilter")).activate());
    check(take(take(find(state, "recentFilterMenu")).snapshot()).opened, "filter menu did not open from the button");
    good(take(find(state, "recentFilterVideo")).activate());
    check(model->filter.get() == "Video" && model->selectedKey.get() == "a" && model->showEmpty.get(),
        "filter changed the selection or failed to show its empty state");
    good(model->setFilter("All types"));
    good(take(find(state, "recentViewAll")).activate()); check(browsed == 1, "View all did not navigate");
    (void)take(state->drawing_snapshot({400, 440}));
    (void)take(take(find(state, "recentSurface")).snapshot());
    if (model->showFilter.get() || model->showSpecifications.get() || model->showUpdated.get()) {
        const auto host = take(take(find(state, "recentFilesHost")).snapshot()).bounds;
        const auto surface = take(take(find(state, "recentSurface")).snapshot()).bounds;
        const auto content = take(take(find(state, "recentContent")).snapshot()).bounds;
        const auto outer = take(root.snapshot()).bounds;
        throw std::runtime_error("narrow panel widths: outer=" + std::to_string(outer.width) + " host=" + std::to_string(host.width) +
            " surface=" + std::to_string(surface.width) + " content=" + std::to_string(content.width) + " vm=" + std::to_string(model->viewportWidth.get()));
    }
    vm::Array many; std::vector<std::string> previews;
    for (int i = 0; i < 1000; ++i) {
        many.push_back(file(std::to_string(i), std::to_string(i) + ".iisc"));
        previews.push_back("file:///" + std::to_string(i));
    }
    images->requests.clear();
    good(model->setFiles(vm::Value(std::move(many)), std::move(previews), false));
    (void)take(state->drawing_snapshot({700, 440}));
    check(take(state->snapshots()).size() < 250, "file list instantiated offscreen rows");
    check(std::set(images->requests.begin(), images->requests.end()).size() < 30, "offscreen file previews were requested");
    state->close();
}
}
int main() { try { exercise(); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; } }
