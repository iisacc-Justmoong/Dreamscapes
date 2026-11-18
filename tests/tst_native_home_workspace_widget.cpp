#include "App/Views/Home/NativeHomeWorkspaceSources.h"
#include "App/Views/Home/NativeHomeWorkspaceView.h"
#include "iiGUI/WidgetState.h"
#include <iiGUI>
#include <iiSDKCore>
#include <cmath>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace {
using namespace dreamscapes;
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
    throw std::runtime_error("Missing workspace widget " + std::string(id));
}
void exercise(const char *assetPath) {
    using namespace ii::gui;
    std::ifstream asset(assetPath, std::ios::binary);
    check(bool(asset), "queue asset missing");
    std::string svg{std::istreambuf_iterator<char>(asset), {}};
    auto sidebar = home::HomeSidebarModel::create({{"home", "Home", "nodeshomeFolder"}, {"image", "Image", "unconditionalImageGeneration"}});
    auto files = home::HomeRecentFilesModel::create();
    auto selected = home::HomeSelectedFileModel::create();
    auto queue = std::make_shared<home::HomeQueueModel>();
    auto generations = home::HomeRecentGenerationsModel::create();
    const auto file = vm::Value(vm::Object{{"id", vm::Value("a")}, {"name", vm::Value("A.iisc")}, {"available", vm::Value(true)}});
    vm::Array fileList{file}; std::vector<std::string> previews{std::string{}};
    for (int i = 1; i < 60; ++i) {
        fileList.emplace_back(vm::Object{{"id", vm::Value(std::to_string(i))}, {"name", vm::Value(std::to_string(i)+".iisc")}});
        previews.emplace_back();
    }
    good(files->setFiles(vm::Value(std::move(fileList)), std::move(previews), false));
    (void)take(selected->setFile(file, ""));
    int opened = 0, canvases = 0, browsed = 0;
    files->setHandlers({.browse = [&] { ++browsed; }});
    auto composer = compiled_view_source([](WidgetBuilder &builder) -> ii::core::Result<void> {
        auto label = builder.label({}, "composerFixture", {.text = "Composer fixture"});
        if (!label) return std::unexpected(label.error());
        return {};
    });
    auto sources = home::homeWorkspaceSources({sidebar, composer, files, selected, queue, generations, {}, svg});
    auto model = home::HomeWorkspaceModel::create(std::move(sources), sidebar,
        {"Workspace", "Create and continue", "Open file", "New canvas", "Storage"},
        {[&] { ++opened; }, [&] { ++canvases; }});
    good(model->setComposerHeight(91)); good(model->setStatus("Session · Idle"));
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {1440,868}));
    good(root.set_content([&](WidgetBuilder &builder) { return home::buildHomeWorkspace(builder, {}, model); }));
    (void)take(state->drawing_snapshot({1440,868}));
    check(take(find(state,"recentHeading").snapshot()).text == "Recent files" &&
          take(find(state,"selectedFileName").snapshot()).text == "A.iisc" &&
          take(find(state,"queueHeading").snapshot()).text == "Generation queue" &&
          take(find(state,"composerFixture").snapshot()).text == "Composer fixture", "live native regions were not mounted");
    const auto filesBounds = take(find(state,"homeRecentFiles").snapshot()).bounds;
    const auto selectedBounds = take(find(state,"homeSelectedFile").snapshot()).bounds;
    const auto queueBounds = take(find(state,"homeQueue").snapshot()).bounds;
    const auto generationsBounds = take(find(state,"homeRecentGenerations").snapshot()).bounds;
    auto near = [](double a, double b) { return std::abs(a-b) < 0.1; };
    if (!near(filesBounds.width,model->fileWidth.get()-24) || !near(selectedBounds.width,model->selectedWidth.get()-24) || !near(filesBounds.height,469) ||
        !near(queueBounds.width,model->queueWidth.get()) || !near(generationsBounds.width,model->generationWidth.get()-24) || selectedBounds.x <= filesBounds.x ||
        generationsBounds.x <= queueBounds.x || queueBounds.y <= filesBounds.y)
        throw std::runtime_error("wide native bounds: files=" + std::to_string(filesBounds.width) + "x" + std::to_string(filesBounds.height) +
            " selected=" + std::to_string(selectedBounds.width) + " queue=" + std::to_string(queueBounds.width) +
            " generations=" + std::to_string(generationsBounds.width));
    good(find(state,"homeOpenFile").activate()); good(find(state,"homeNewCanvas").activate());
    good(find(state,"recentViewAll").activate());
    check(opened==1 && canvases==1 && browsed==1, "commands did not reach the supplied controllers");
    auto failedSources = model->sources;
    failedSources.composer = compiled_view_source([](WidgetBuilder &) -> ii::core::Result<void> {
        return ii::core::fail(ii::core::ErrorCode::failed, "Composer fixture failed");
    });
    auto failedModel = home::HomeWorkspaceModel::create(std::move(failedSources), sidebar, {}, {});
    auto failed = root.set_content([&](WidgetBuilder &builder) { return home::buildHomeWorkspace(builder, {}, failedModel); });
    check(!failed && take(find(state,"selectedFileName").snapshot()).text == "A.iisc",
          "failed replacement did not preserve the previous workspace");
    failedModel->close();
    auto originalList = find(state,"recentFiles"); good(originalList.focus());
    auto originalViewport = find(state,"recentFiles/viewport");
    good(originalViewport.set_scroll_offset(120));
    check(take(originalViewport.snapshot()).scroll_offset > 0, "file fixture failed to scroll");
    (void)take(state->drawing_snapshot({900,640}));
    const auto resizedName = take(find(state,"selectedFileName").snapshot()).text;
    if (model->wide.get() || resizedName != "A.iisc" || files->selectedKey.get() != "a")
        throw std::runtime_error("responsive layout: width=" + std::to_string(model->windowWidth.get()) +
            " wide=" + std::to_string(model->wide.get()) + " name=" + resizedName + " key=" + files->selectedKey.get());
    check(take(find(state,"homeSelectedFile").snapshot()).bounds.y > take(find(state,"homeRecentFiles").snapshot()).bounds.y,
          "narrow file panels did not stack");
    check(take(find(state,"recentFiles/viewport").snapshot()).scroll_offset > 0,
          "resizing across the column breakpoint reset the recent-files scroll position");
    check(take(originalList.snapshot()).focused && take(originalViewport.snapshot()).scroll_offset > 0,
          "resizing replaced the focused file list or its scroll viewport");
    (void)take(state->drawing_snapshot({600,600}));
    const auto queueTitle = take(find(state,"queueHeading").snapshot()).text;
    if (!model->compact.get() || !sidebar->compact.get() || queueTitle != "Generation queue")
        throw std::runtime_error("compact layout: width=" + std::to_string(model->windowWidth.get()) +
            " compact=" + std::to_string(model->compact.get()) + " sidebar=" + std::to_string(sidebar->compact.get()) + " queue=" + queueTitle);
    Widget imageAction;
    for (const auto &node : take(state->snapshots()))
        if (node.id.ends_with("/sidebarAction") && node.accessibility_name == "Image") imageAction = take(state->find(node.id));
    const auto action = take(imageAction.snapshot());
    const auto prefix = action.id.substr(0, action.id.size() - std::string("sidebarAction").size());
    auto tooltip = take(state->find(prefix + "sidebarTooltip"));
    good(state->input({.kind=WidgetInputKind::pointer_move,.x=action.bounds.x+8,.y=action.bounds.y+8}));
    std::this_thread::sleep_for(std::chrono::milliseconds(650)); (void)events.poll();
    const auto tooltipState = take(tooltip.snapshot());
    check(tooltipState.opened, "compact sidebar tooltip did not open inside the composed view");
    const auto tooltipText = take(take(state->find(prefix + "sidebarTooltip/text")).snapshot());
    if (tooltipState.bounds.x + tooltipState.bounds.width <= 48 || tooltipState.bounds.width < 1 ||
        (tooltipText.clip_bounds && tooltipText.clip_bounds->x + tooltipText.clip_bounds->width <= 48))
        throw std::runtime_error("compact tooltip was clipped by the sidebar: x=" +
            std::to_string(tooltipState.bounds.x) + " width=" + std::to_string(tooltipState.bounds.width));
    const std::weak_ptr<home::HomeWorkspaceModel> weakModel = model;
    state->close(); model->close();
    check(!model->openFile() && opened==1, "closed view delivered a command");
    model.reset(); check(weakModel.expired(), "retired native workspace retained its model");
}
}
int main(int argc, char **argv) {
    try { if (argc != 2) return 2; exercise(argv[1]); }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
