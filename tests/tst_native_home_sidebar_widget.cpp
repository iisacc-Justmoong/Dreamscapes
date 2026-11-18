#include "App/Views/Home/NativeHomeSidebarView.h"
#include "iiGUI/WidgetState.h"
#include <LVUI>
#include <iiSDKCore>
#include <chrono>
#include <cmath>
#include <iostream>
#include <optional>
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
bool hasIcon(const ii::gui::CanvasFrame &frame, ii::gui::Rect bounds) {
    const auto inRow=[&](const auto &images) {
        for (const auto &image:images)
            if (image.image && std::abs(image.bounds.width-18)<.01 && std::abs(image.bounds.height-18)<.01 &&
                image.bounds.x>=bounds.x && image.bounds.x+image.bounds.width<=bounds.x+bounds.width &&
                image.bounds.y>=bounds.y && image.bounds.y+image.bounds.height<=bounds.y+bounds.height) return true;
        return false;
    };
    if (inRow(frame.images)) return true;
    for (const auto &layer:frame.overlays) if (inRow(layer.images)) return true;
    return false;
}
bool hasFill(const ii::gui::CanvasFrame &frame, ii::gui::Rect bounds, std::optional<lvui::StyleColor> expected = {}) {
    const auto linear=[](double x) { return x<=.04045 ? x/12.92 : std::pow((x+.055)/1.055,2.4); };
    const auto inRow=[&](const auto &panels) {
        for (const auto &panel:panels) {
            const bool matchesRow = std::abs(panel.bounds.x-bounds.x)<.01 && std::abs(panel.bounds.y-bounds.y)<.01 &&
                std::abs(panel.bounds.width-bounds.width)<.01 && std::abs(panel.bounds.height-bounds.height)<.01;
            if (!matchesRow || panel.stroke_width!=0 || panel.top.alpha<=.001) continue;
            if (!expected) return true;
            const auto &fill = panel.top;
            if (std::abs(fill.red-linear(expected->red))<.001 && std::abs(fill.green-linear(expected->green))<.001 &&
                std::abs(fill.blue-linear(expected->blue))<.001 && std::abs(fill.alpha-expected->alpha)<.001) return true;
        }
        return false;
    };
    if (inRow(frame.panels)) return true;
    for (const auto &layer:frame.overlays) if (inRow(layer.panels)) return true;
    return false;
}
bool hasBodyText(const ii::gui::CanvasFrame &frame, std::string_view label) {
    const auto matches=[&](const auto &texts) {
        for (const auto &text:texts)
            if (text.text==label && text.font_weight==500 && std::abs(text.font_size-13)<.01 &&
                std::abs(text.color.alpha-lvui::figma_defaults::text_title_header_color.alpha)<.001) return true;
        return false;
    };
    if (matches(frame.texts)) return true;
    for (const auto &layer:frame.overlays) if (matches(layer.texts)) return true;
    return false;
}
void exercise() {
    using namespace ii::gui;
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {197, 300}));
    auto model = home::HomeSidebarModel::create({{"home", "Home", "nodeshomeFolder"}, {"divider1", "", "", true},
        {"image", "Image", "unconditionalImageGeneration"}, {"board", "Board", "pattern"},
        {"tools", "Tools", "collection-asset"}, {"divider2", "", "", true}, {"history", "Generation History", "profileCPU"}});
    good(model->setSelected("home")); std::string activated; int calls = 0;
    model->setHandler([&](std::string key) { activated = std::move(key); ++calls; });
    good(root.set_content([&](WidgetBuilder &builder) { return home::buildHomeSidebar(builder, {}, model); }));
    (void)take(state->drawing_snapshot({197, 300}));
    const auto frame=take(state->drawing_snapshot({197, 300}));
    auto home = take(findAction(state, "Home"));
    auto image = take(findAction(state, "Image"));
    const auto homeBounds=take(home.snapshot()).bounds, imageBounds=take(image.snapshot()).bounds;
    check(std::abs(homeBounds.width-181)<.01 && std::abs(homeBounds.height-24)<.01,
        "expanded sidebar row does not match the Figma frame geometry");
    for (const auto *label:{"Home", "Image", "Board", "Tools", "Generation History"}) {
        const auto bounds=take(take(findAction(state,label)).snapshot()).bounds;
        if (!hasIcon(frame,bounds)) throw std::runtime_error(std::string(label)+" has no authored 18-point icon");
    }
    check(hasBodyText(frame,"Image"), "sidebar label does not use the Figma body typography");
    check(!hasFill(frame,imageBounds), "unselected sidebar row paints a panel over the window background");
    check(hasFill(frame,homeBounds,lvui::figma_defaults::accent_blue_selected), "selected sidebar row uses the wrong LVUI fill");
    const auto x=imageBounds.x+imageBounds.width/2, y=imageBounds.y+imageBounds.height/2;
    good(state->input({.kind=WidgetInputKind::pointer_move,.x=x,.y=y}));
    check(hasFill(take(state->drawing_snapshot({197,300})),imageBounds), "unselected sidebar row lost its hover feedback");
    good(state->input({.kind=WidgetInputKind::pointer_down,.x=x,.y=y,.buttons=1}));
    check(take(image.snapshot()).pressed && hasFill(take(state->drawing_snapshot({197,300})),imageBounds),
        "unselected sidebar row lost its pressed feedback");
    const auto paddingX=imageBounds.x/2;
    good(state->input({.kind=WidgetInputKind::pointer_move,.x=paddingX,.y=y,.buttons=1}));
    good(state->input({.kind=WidgetInputKind::pointer_up,.x=paddingX,.y=y}));
    check(!hasFill(take(state->drawing_snapshot({197,300})),imageBounds), "sidebar row kept its fill after interaction");
    int dividers=0;
    for (const auto &node:take(state->snapshots())) if (node.id.ends_with("/sidebarDivider")) {
        ++dividers;
        check(std::abs(node.bounds.width-181)<.01 && std::abs(node.bounds.height-3)<.01,
              "sidebar divider does not match the Figma row width and height");
    }
    check(dividers==2,"sidebar groups do not have their two dividers");
    check(take(home.snapshot()).checked && take(image.snapshot()).accessibility_name == "Image" && take(image.snapshot()).text == "Image",
        "sidebar selection or accessibility name is wrong");
    good(image.activate()); check(activated == "image" && calls == 1 && model->selected.get() == "home", "action did not reach controller");
    good(model->setSelected("image")); check(take(image.snapshot()).checked, "external selection did not update widget");
    good(model->setCompact(true)); good(root.set_extent({48,300})); model->sizeChanged(48,300);
    (void)take(state->drawing_snapshot({48, 300}));
    const auto compactFrame=take(state->drawing_snapshot({48, 300}));
    (void)take(take(find(state, "sidebarSurface")).snapshot());
    check(std::abs(take(image.snapshot()).bounds.width-32)<.01 && hasIcon(compactFrame,take(image.snapshot()).bounds),
        "compact sidebar did not resize the icon row");
    check(!hasFill(compactFrame,take(home.snapshot()).bounds), "compact default row paints a panel over the window background");
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
    good(root.set_extent({48,48}));
    (void)take(state->drawing_snapshot({48, 48})); (void)take(take(find(state, "sidebarSurface")).snapshot());
    auto history = take(findAction(state, "Generation History"));
    good(history.focus());
    check(take(take(find(state, "sidebarScroll")).snapshot()).scroll_offset > 0, "focusing an offscreen action did not reveal it");
    good(model->setCompact(false)); good(root.set_extent({197,300})); model->sizeChanged(197,300);
    (void)take(state->drawing_snapshot({197,300}));
    const auto expandedFrame=take(state->drawing_snapshot({197,300}));
    const auto expandedBounds=take(image.snapshot()).bounds;
    check(std::abs(expandedBounds.width-181)<.01 && hasIcon(expandedFrame,expandedBounds),
          "expanded sidebar did not restore the full Figma row");
    state->close(); check(!image.snapshot(), "closed sidebar retained a widget");
}
}
int main() { try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
