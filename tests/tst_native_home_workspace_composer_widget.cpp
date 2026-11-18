#include "App/Views/Home/NativeHomeComposerFactory.h"
#include "App/Views/Home/NativeHomeComposerWorkspaceBinding.h"
#include "App/Views/Home/NativeHomeWorkspaceSources.h"
#include "App/Views/Home/NativeHomeWorkspaceView.h"
#include "iiGUI/WidgetState.h"
#include <iiSDKCore>
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
ii::gui::Widget prompt(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view name = "Prompt") {
    for (const auto &node : take(state->snapshots()))
        if (node.visible && node.text_input && node.accessibility_name == name) return take(state->find(node.id));
    throw std::runtime_error("Missing visible Home text editor " + std::string(name));
}
home::HomeComposerLabels labels() {
    return {"Prompt", "Prompt", "Clear", "Negative prompt", "Done", "Media", "Model", "Choose model",
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
    draft.choices.media = {{"Image","Image"},{"Video","Video"}};
    draft.choices.ratios = {{"1:1","1:1"},{"16:9","16:9"}};
    draft.choices.counts = {{"1","1 image"}}; draft.choices.resolutions = {{"512","512 × 512"},{"768","768 × 768"}};
    draft.shortSide = 512; draft.resolutionLabel = "512 × 512";
    draft.choices.durations = {{"3","3 s"}}; draft.choices.frameRates = {{"24","24 FPS"}};
    good(composer->setSnapshot(draft)); int edits = 0, submissions = 0;
    good(composer->setHandler([&](const home::HomeComposerCommand &command) -> ii::core::Result<void> {
        if (command.kind == home::HomeComposerCommandKind::editPrompt) {
            ++edits; auto next = composer->snapshot.get(); next.prompt = command.value; return composer->setSnapshot(std::move(next));
        }
        if (command.kind == home::HomeComposerCommandKind::editNegativePrompt) {
            auto next = composer->snapshot.get();
            next.negativePrompt = command.value;
            return composer->setSnapshot(std::move(next));
        }
        if (command.kind == home::HomeComposerCommandKind::chooseResolution) {
            auto next = composer->snapshot.get();
            next.shortSide = std::stoi(command.value);
            next.resolutionLabel = command.value + " × " + command.value;
            return composer->setSnapshot(std::move(next));
        }
        if (command.kind == home::HomeComposerCommandKind::submit) ++submissions;
        return {};
    }));
    const home::HomeComposerLayoutSpec spec{1000,600,400,{65,1,22,93,138,71},{327,88,144,96,120,228,152},separateResolvers};
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
    (void)take(state->drawing_snapshot({1440,868}));
    (void)take(state->snapshots());
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
    if (separateResolvers) {
        auto negative = prompt(state, "Negative prompt");
        const auto panelBounds = take(find(state,"homeComposer").snapshot()).bounds;
        const auto negativeEditor = take(negative.snapshot());
        const auto negativeBounds = negativeEditor.visual_bounds;
        const auto attach = take(find(state,"homeActionIcon").snapshot());
        const auto generate = take(find(state,"homeActionPrimary").snapshot());
        check(panel->requestedHeight.get()==106, "mounted QuickGenerate height did not match Figma");
        if (negativeBounds.y!=panelBounds.y+42 || negativeBounds.x!=panelBounds.x+12 ||
            negativeBounds.width!=panelBounds.width-24 || negativeBounds.height!=22) {
            std::cerr << "Composer [" << panelBounds.x << ',' << panelBounds.y << ',' << panelBounds.width
                      << "] with Negative prompt [" << negativeBounds.x << ',' << negativeBounds.y
                      << ',' << negativeBounds.width << ',' << negativeBounds.height << "]\n";
            throw std::runtime_error("mounted Negative prompt did not fill the second row");
        }
        const auto drawsNegativePlaceholder = [](const auto &layer) {
            return std::ranges::any_of(layer.texts, [](const auto &run) {return run.text == "Negative prompt";});
        };
        const auto initialFrame = take(state->drawing_snapshot({1440,868}));
        check(drawsNegativePlaceholder(initialFrame) || std::ranges::any_of(initialFrame.overlays, drawsNegativePlaceholder),
              "the inline negative prompt did not display the Figma placeholder");
        check(attach.visual_bounds.width==22 && generate.visual_bounds.width==71 &&
              generate.visual_bounds.y==attach.visual_bounds.y && generate.text=="Generate" &&
              generate.visual_bounds.x+generate.visual_bounds.width==panelBounds.x+panelBounds.width-12,
              "mounted Add and Generate did not match their fixed layout");
        const auto drawsAttachIcon = [&](const auto &layer) {
            return std::ranges::any_of(layer.images, [&](const auto &image) {
                const auto expected = Rect{attach.visual_bounds.x+2,attach.visual_bounds.y+2,18,18};
                return image.image && image.bounds==expected;
            });
        };
        check(drawsAttachIcon(initialFrame) || std::ranges::any_of(initialFrame.overlays, drawsAttachIcon),
              "the LVUI Add icon was not rendered in its 18-point slot");
        for (const auto &node : take(state->snapshots()))
            check(!node.visible || (node.text!="Paint canvas" && node.text!="Load preset" &&
                  node.text!="Advanced parameters"), "QuickGenerate showed a control outside the design");

        auto button = [&](std::string_view name) {
            for (const auto &node : take(state->snapshots())) {
                if (node.id.ends_with("/homeChoiceButton") && node.accessibility_name == name)
                    return take(state->find(node.id));
            }
            throw std::runtime_error("Missing QuickGenerate button " + std::string(name));
        };
        const auto mediaBounds = take(button("Media").snapshot()).visual_bounds;
        const auto promptBounds = take(prompt(state).snapshot()).visual_bounds;
        check(mediaBounds.x==panelBounds.x+12 && mediaBounds.y==panelBounds.y+12 &&
              mediaBounds.width==65 && mediaBounds.height==22 &&
              promptBounds.x==mediaBounds.x+73 && promptBounds.y==mediaBounds.y &&
              promptBounds.width+73==negativeBounds.width && promptBounds.height==22,
              "mounted media and Prompt did not match the first row");
        auto click = [&](Widget widget) {
            const auto bounds = take(widget.snapshot()).visual_bounds;
            const auto x = bounds.x + bounds.width/2, y = bounds.y + bounds.height/2;
            good(state->input({.kind=WidgetInputKind::pointer_down,.x=x,.y=y,.buttons=1}));
            good(state->input({.kind=WidgetInputKind::pointer_up,.x=x,.y=y}));
        };
        auto openPopups = [&] {
            std::vector<WidgetSnapshot> opened;
            for (const auto &node : take(state->snapshots())) if (node.opened) opened.push_back(node);
            return opened;
        };
        const auto ratio = button("Aspect ratio"), resolution = button("Resolution");
        click(ratio);
        check(openPopups().size() == 1, "aspect-ratio click did not open one menu");
        click(resolution);
        auto opened = openPopups();
        check(opened.size() == 1, "switching QuickGenerate controls left two menus open");
        Widget option;
        for (const auto &node : take(state->snapshots())) {
            if (node.id.ends_with("/homeChoiceEntry") && node.visible && node.text == "768 × 768")
                option = take(state->find(node.id));
        }
        check(!option.is_null(), "resolution menu did not receive the first click");
        click(option);
        check(composer->snapshot.get().shortSide == 768, "the visible resolution option did not receive the click");
        check(openPopups().empty(), "choosing a resolution did not close its menu");
        click(ratio);
        click(find(state,"homeSeedButton"));
        opened = openPopups();
        check(opened.size() == 1 && opened.front().id.ends_with("/homeSeedPopover"),
              "seed popover did not replace the active menu");
        click(find(state,"homeSeedButton"));
        check(openPopups().empty(), "a second click did not dismiss the seed popover");
        click(ratio);
        good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::escape}));
        check(openPopups().empty(), "Escape did not dismiss the active QuickGenerate menu");
        click(ratio);
        click(prompt(state));
        check(openPopups().empty() && take(prompt(state).snapshot()).focused,
              "the prompt did not regain input when clicking outside the menu");
        click(negative);
        good(state->input({.kind=WidgetInputKind::text,.text="흐림"}));
        check(composer->snapshot.get().negativePrompt=="흐림" && composer->snapshot.get().prompt.empty(),
              "inline negative prompt did not edit the shared draft independently");
        const auto initialHeight = panel->requestedHeight.get();
        good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter}));
        good(state->input({.kind=WidgetInputKind::text,.text="잡음"}));
        (void)take(state->drawing_snapshot({1440,868}));
        check(composer->snapshot.get().negativePrompt=="흐림\n잡음" && panel->requestedHeight.get()>initialHeight &&
              take(negative.snapshot()).focused, "negative prompt did not expand with multiline input");
        check(submissions == 0, "QuickGenerate popup interactions submitted a generation");
    }
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
