#include "App/Views/Home/NativeHomeComposerFactory.h"
#include "App/Views/Home/NativeHomeComposerWorkspaceBinding.h"
#include "App/Views/Home/NativeHomeWorkspaceSources.h"
#include "App/Views/Home/NativeHomeWorkspaceView.h"
#include "iiGUI/WidgetState.h"
#include <iiSDKCore/Runtime/FrameLoop.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <stdexcept>

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
    throw std::runtime_error("Missing full Home widget " + std::string(id));
}
ii::gui::Widget prompt(const std::shared_ptr<ii::gui::detail::WidgetState> &state) {
    for (const auto &node : take(state->snapshots()))
        if (node.text_input && node.accessibility_name == "Prompt") return take(state->find(node.id));
    throw std::runtime_error("Missing full Home prompt");
}
home::HomeComposerLabels labels() {
    return {"Describe", "Prompt", "Clear", "Negative prompt", "Done", "Media", "Model", "Choose model",
            "Aspect ratio", "Resolution", "Batch size", "Duration", "Frame rate", "Preset", "Load preset",
            "Save preset", "Name", "Save", "Cancel", "Seed", "Random", "Apply", "Attach images", "Paint canvas",
            "Advanced parameters", "More controls", "Generate", [](int n, bool video) { return std::to_string(n)+(video?" videos":" images"); },
            [](const auto &name) { return "Remove " + name; }};
}
class Images final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    mutable std::vector<std::string> requests;
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view source, std::uint32_t, std::uint32_t) const override {
        requests.emplace_back(source);
        return {};
    }
    bool saw(std::string_view source) const { return std::ranges::find(requests, source) != requests.end(); }
};
void exercise(const char *assetPath, bool separateResolvers) {
    using namespace ii::gui;
    std::ifstream asset(assetPath, std::ios::binary); check(bool(asset), "queue asset missing");
    std::string svg{std::istreambuf_iterator<char>(asset), {}};
    auto composer = home::HomeComposerModel::create(); home::HomeComposerSnapshot draft;
    draft.mediaType = "Image"; draft.aspectRatio = "1:1"; draft.outputCount = 1; draft.seed = -1; draft.seedLabel = "Random";
    draft.choices.media = {{"Image","Image"},{"Video","Video"}}; draft.choices.ratios = {{"1:1","1:1"}};
    draft.choices.counts = {{"1","1 image"}}; draft.choices.resolutions = {{"512","512 × 512"}};
    draft.choices.durations = {{"3","3 s"}}; draft.choices.frameRates = {{"24","24 FPS"}};
    good(composer->setSnapshot(draft)); int edits = 0, submissions = 0;
    good(composer->setHandler([&](const home::HomeComposerCommand &command) -> ii::core::Result<void> {
        if (command.kind == home::HomeComposerCommandKind::editPrompt) {
            ++edits; auto next = composer->snapshot.get(); next.prompt = command.value; return composer->setSnapshot(std::move(next));
        }
        if (command.kind == home::HomeComposerCommandKind::submit) ++submissions;
        return {};
    }));
    const home::HomeComposerLayoutSpec spec{1000,600,400,{88,1,22,93,138,145},{327,88,144,96,120,228,152}};
    const home::HomeComposerLayoutMetrics metrics{8,12,22,37,44};
    auto panel = home::createHomeComposerPanel(composer,labels(),spec,metrics);
    auto sidebar = home::HomeSidebarModel::create({{"home","Home","nodeshomeFolder"}});
    auto files = home::HomeRecentFilesModel::create(); auto selected = home::HomeSelectedFileModel::create();
    auto queue = std::make_shared<home::HomeQueueModel>(); auto generations = home::HomeRecentGenerationsModel::create();
    const auto file = vm::Value(vm::Object{{"id", vm::Value("canvas")}, {"name", vm::Value("canvas.iisc")}});
    const auto image = vm::Value(vm::Object{{"id", vm::Value("image")}, {"name", vm::Value("image.png")},
        {"source", vm::Value("file:///image.png")}, {"mediaType", vm::Value("Image")}});
    good(files->setFiles(vm::Value(vm::Array{file}), {"recent-preview://file"}, false));
    (void)take(selected->setFile(file, "file:///selected.png"));
    good(generations->setFiles(vm::Value(vm::Array{image}), {"recent-generation-preview://image"}, false));
    auto baseImages = std::make_shared<Images>(), fileImages = std::make_shared<Images>(), generationImages = std::make_shared<Images>();
    home::HomeWorkspaceComponents components{sidebar,home::homeComposerPanelSource(panel),files,selected,queue,generations,baseImages,svg};
    if (separateResolvers) { components.recentFileImages = fileImages; components.recentGenerationImages = generationImages; }
    auto sources = home::homeWorkspaceSources(std::move(components));
    auto workspace = home::HomeWorkspaceModel::create(std::move(sources),sidebar,{"Workspace","","Open","New","Storage"},{});
    auto binding = home::HomeComposerWorkspaceBinding::create(panel,workspace);
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto root = take(state->content_host({},"root",{1440,868}));
    good(root.set_content([&](WidgetBuilder &builder) { return home::buildHomeWorkspace(builder,{},workspace); }));
    (void)take(state->drawing_snapshot({1440,868})); (void)take(state->snapshots());
    check(baseImages->saw("file:///selected.png"), "selected file did not use the base preview resolver");
    if (separateResolvers) {
        check(fileImages->saw("recent-preview://file") && !baseImages->saw("recent-preview://file"),
              "recent file preview did not use its session resolver");
        check(generationImages->saw("recent-generation-preview://image") && !baseImages->saw("recent-generation-preview://image"),
              "recent generation preview did not use its session resolver");
    } else {
        check(baseImages->saw("recent-preview://file") && baseImages->saw("recent-generation-preview://image"),
              "missing session resolvers did not fall back to the base resolver");
    }
    check(std::abs(workspace->composerHeight.get()-panel->requestedHeight.get()) < .01 && panel->requestedHeight.get() >= 91,
          "Home did not receive the real composer's measured height");
    auto editor = prompt(state); good(editor.focus()); good(state->input({.kind=WidgetInputKind::text,.text="한글"}));
    check(edits == 1 && composer->snapshot.get().prompt == "한글", "full Home did not update its generation draft");
    (void)take(state->drawing_snapshot({900,640})); (void)take(state->snapshots());
    check(take(editor.snapshot()).focused && composer->snapshot.get().prompt == "한글" &&
          std::abs(workspace->composerHeight.get()-panel->requestedHeight.get()) < .01 && panel->requestedHeight.get() > 91,
          "Home resize lost the prompt or its measured height");
    const auto composerBounds = take(find(state,"homeComposer").snapshot()).bounds;
    const auto filesBounds = take(find(state,"homeFileWorkspace").snapshot()).bounds;
    check(filesBounds.y + .1 >= composerBounds.y + composerBounds.height, "composer overlapped the file workspace");
    good(find(state,"homeActionPrimary").activate()); check(submissions == 1, "full Home generate action did not reach the draft");
    state->close(); binding->close(); workspace->close(); panel->close(); composer->close(); sidebar->close();
}
}
int main(int argc, char **argv) {
    try { if (argc != 2) return 2; exercise(argv[1], true); exercise(argv[1], false); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
