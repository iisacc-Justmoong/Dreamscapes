#include "App/Views/Home/NativeHomeRecentGenerationsView.h"
#include "iiGUI/WidgetState.h"
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace dreamscapes;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) { if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); } }
template<class T> T take(ii::core::Result<T> value) { if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); } return std::move(*value); }
ii::core::Result<ii::gui::Widget> find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, const std::string &id) {
    auto nodes = state->snapshots(); if (!nodes) return std::unexpected(nodes.error());
    for (const auto &node : *nodes) if (node.id == id || node.id.ends_with("/" + id)) return state->find(node.id);
    return ii::core::fail(ii::core::ErrorCode::not_found, "Missing widget " + id);
}
vm::Value file(const char *id, const char *source, const char *type) {
    return vm::Value(vm::Object{{"id", vm::Value(id)}, {"name", vm::Value(id)}, {"source", vm::Value(source)}, {"mediaType", vm::Value(type)}});
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
    auto root = take(state->content_host({}, "root", {700, 160}));
    auto model = home::HomeRecentGenerationsModel::create(); auto images = std::make_shared<Images>();
    std::string focus, opened; std::vector<std::string> edited; int browsed = 0;
    model->setHandlers({.focus = [&](std::string key) { focus = std::move(key); },
        .open = [&](std::string token, vm::Value) { opened = std::move(token); },
        .edit = [&](std::vector<std::string> tokens) { edited = std::move(tokens); }, .browse = [&] { ++browsed; }});
    good(root.set_content([&](WidgetBuilder &builder) { return home::buildHomeRecentGenerations(builder, {}, model, images); }));
    (void)take(state->drawing_snapshot({700, 160}));
    check(take(take(find(state, "recentGenerationEmpty")).snapshot()).text == "No generated images yet", "empty generation view is missing");
    const auto a = file("a", "file:///a.png", "Image"), b = file("b", "file:///b.png", "Image"), v = file("v", "file:///v.mp4", "Video");
    good(model->setFiles(vm::Value(vm::Array{a, b, v}), {"a", "b", "v"}, false));
    (void)take(state->drawing_snapshot({700, 160}));
    auto list = take(find(state, "recentGenerations"));
    (void)take(take(find(state, "recentGenerations/viewport")).snapshot());
    const auto aToken = model->tokenForKey("a"), bToken = model->tokenForKey("b"), vToken = model->tokenForKey("v");
    const auto click = [&](const std::string &token, double time) {
        const auto bounds = take(take(list.keyed_content_item(token, "generationRow")).snapshot()).bounds;
        good(state->input({.kind=WidgetInputKind::pointer_down,.x=bounds.x+10,.y=bounds.y+10,.timestamp=time,.buttons=1,.native_click_count=1}));
        good(state->input({.kind=WidgetInputKind::pointer_up,.x=bounds.x+10,.y=bounds.y+10,.timestamp=time+.01}));
    };
    click(bToken, 1); check(opened == bToken && focus == "b", "single click did not focus and open result");
    good(take(find(state, "recentGenerationSelect")).activate());
    click(aToken, 2); click(bToken, 3); click(vToken, 4);
    check(model->selectedCount.get() == 2 && model->rows.get()[0].selected && !model->rows.get()[2].selected,
        "selection mode did not toggle only images");
    good(take(find(state, "recentGenerationEdit")).activate());
    check(edited == std::vector<std::string>({aToken, bToken}), "edit did not receive selected results");
    good(take(find(state, "recentGenerationSelect")).activate());
    good(model->setFocusedKey("a")); good(list.focus());
    good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::down}));
    check(model->focusedKey.get() == "v", "two-column keyboard navigation skipped a row");
    good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter}));
    check(opened == vToken, "Enter did not open focused result");
    good(take(find(state, "recentGenerationViewAll")).activate()); check(browsed == 1, "View all did not navigate");
    (void)take(state->drawing_snapshot({400, 160})); (void)take(take(find(state, "recentGenerationSurface")).snapshot());
    check(model->columns.get() == 1, "narrow generation view did not become one column");
    vm::Array many; std::vector<std::string> previews;
    for (int i = 0; i < 1000; ++i) {
        const auto id = std::to_string(i), source = "file:///" + id + ".png";
        many.push_back(vm::Value(vm::Object{{"id", vm::Value(id)}, {"name", vm::Value(id)}, {"source", vm::Value(source)}}));
        previews.push_back(source);
    }
    images->requests.clear(); good(model->setFiles(vm::Value(std::move(many)), std::move(previews), false));
    (void)take(state->drawing_snapshot({700, 160}));
    check(take(state->snapshots()).size() < 250 && std::set(images->requests.begin(), images->requests.end()).size() < 30,
        "generation grid requested or instantiated offscreen results");
    state->close();
}
}
int main() { try { exercise(); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; } }
