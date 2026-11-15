#include "App/Generation/GenerationComposerModel.h"
#include "App/Generation/GenerationController.h"
#include "App/Generation/NativeHomeReferenceInputs.h"
#include "App/ViewModels/WorkspaceSession.h"
#include "App/Views/Home/NativeHomeComposerFactory.h"
#include "App/Views/Home/NativeHomeComposerSession.h"
#include "App/Views/Home/NativeHomeComposerWorkspaceBinding.h"
#include "App/Views/Home/NativeHomeContentSession.h"
#include "App/Views/Home/NativeHomeReferenceBinding.h"
#include "App/Views/Home/NativeHomeReferencePicker.h"
#include "App/Views/Home/NativeHomeWorkspaceSources.h"
#include "App/Views/Home/NativeHomeWorkspaceView.h"
#include "iiGUI/WidgetState.h"
#include <Container.h>
#include <SocietyDrive.h>
#include <iiSDKCore/Runtime/FrameLoop.h>
#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QUrl>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace dreamscapes;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
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
    throw std::runtime_error("Missing Home widget " + std::string(id));
}
class Images final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    mutable std::vector<std::string> requests;
    std::shared_ptr<const ii::gui::RasterImage> pixels;
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view source, std::uint32_t, std::uint32_t) const override {
        requests.emplace_back(source); return pixels;
    }
    bool saw(std::string_view source) const { return std::ranges::find(requests, source) != requests.end(); }
};
home::HomeComposerLabels labels() {
    return {"Describe", "Prompt", "Clear", "Negative prompt", "Done", "Media", "Model", "Choose model",
        "Aspect ratio", "Resolution", "Batch size", "Duration", "Frame rate", "Preset", "Load preset",
        "Save preset", "Name", "Save", "Cancel", "Seed", "Random", "Apply", "Attach images", "Paint canvas",
        "Advanced parameters", "More controls", "Generate", [](int n, bool video) { return std::to_string(n) + (video ? " videos" : " images"); },
        [](const auto &name) { return "Remove " + name; }};
}
void exercise(const char *assetPath) {
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/home-content-widget-XXXXXX.societycontainer"));
    check(directory.isValid(), "isolated fixture unavailable");
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH", directory.filePath("settings.ini").toUtf8());
    check(iiSocietyContainer::SocietyDrive::create(directory.path()).has_value(), "isolated Society fixture unavailable");
    qputenv("SOCIETY_CONTAINER_PATH", directory.path().toUtf8());
    GenerationRuntime runtime; runtime.nativeInference = true;
    runtime.modelPreferencesFile = directory.filePath("models.conf"); runtime.failureReportsDirectory = directory.path();
    runtime.inspectImageModel = [](const auto &) { return iiLocalDiffusion::ImageModelInspection{}; };
    GenerationController generation(runtime); generation.setForeground(false);
    WorkspaceSession app; app.setGeneration(&generation);
    auto images = std::make_shared<Images>(); images->pixels = take(ii::gui::rgba_image(1, 1, {1, 2, 3, 255}));
    home::NativeHomeContentSession content(&app, {}, {}, {}, images);
    NativeHomeReferenceInputs inputs;
    GenerationComposerModel draft; draft.setCanvas(&inputs);
    home::NativeHomeComposerSession::Callbacks actions;
    home::NativeHomeReferencePicker *picker = nullptr;
    actions.chooseAttachments = [&]() -> ii::core::Result<void> {
        return picker ? picker->choose() : ii::core::fail(ii::core::ErrorCode::closed, "Reference picker is unavailable");
    };
    actions.removeAttachment = [&](std::string key) -> ii::core::Result<void> {
        return inputs.removeAttachment(QString::fromStdString(key)) ? ii::core::Result<void>{}
            : ii::core::fail(ii::core::ErrorCode::invalid_argument, inputs.inputError().toStdString());
    };
    home::NativeHomeComposerSession composer(&draft, {}, "Random", std::move(actions));
    home::NativeHomeReferenceBinding references(&inputs, &draft, &composer, [](qint64) { return "Image"; });
    ii::gui::FileDialogCompletion selectedFiles;
    home::NativeHomeReferencePicker referencePicker(&inputs,
        [&](ii::gui::FileDialogOptions, ii::gui::FileDialogCompletion completion) -> ii::core::Result<void> {
            selectedFiles=std::move(completion); return {};
        }, home::homeReferenceDialogOptions("Attach reference images", "Images and canvases"), {});
    picker = &referencePicker;
    int submitted = 0;
    QObject::connect(&draft, &GenerationComposerModel::generateRequested, [&](QString, QString, QString, int) { ++submitted; });
    const home::HomeComposerLayoutSpec spec{1000,600,400,{88,1,22,93,138,145},{327,88,144,96,120,228,152}};
    const home::HomeComposerLayoutMetrics metrics{8,12,22,37,44};
    auto panel = home::createHomeComposerPanel(composer.model(), labels(), spec, metrics, images);
    auto sidebar = home::HomeSidebarModel::create({{"home", "Home", "nodeshomeFolder"}});
    auto queue = std::make_shared<home::HomeQueueModel>();
    std::ifstream asset(assetPath, std::ios::binary); check(bool(asset), "queue asset unavailable");
    std::string svg{std::istreambuf_iterator<char>(asset), {}};
    home::HomeWorkspaceComponents parts{sidebar, home::homeComposerPanelSource(panel), content.recentFiles(),
        content.selectedFile(), queue, content.recentGenerations(), images, svg,
        content.recentFileImages(), content.recentGenerationImages()};
    auto workspace = home::HomeWorkspaceModel::create(home::homeWorkspaceSources(std::move(parts)), sidebar,
        {"Workspace", "", "Open", "New", "Storage"}, {});
    auto binding = home::HomeComposerWorkspaceBinding::create(panel, workspace);
    ii::runtime::EventLoop events; ii::gui::detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); }, ii::gui::WidgetAppearance{}, hooks);
    auto root = take(state->content_host({}, "root", {1440,868}));
    good(root.set_content([&](ii::gui::WidgetBuilder &builder) { return home::buildHomeWorkspace(builder, {}, workspace); }));
    QVariantMap a{{"id","a"},{"containerId","fixture"},{"key","files/a"},{"section","files"},{"name","A.iisc"},
        {"exists",true},{"available",false},{"previewSource",QUrl("https://example.invalid/a.png")}};
    QVariantMap b = a; b["id"]="b"; b["key"]="files/b"; b["name"]="B.iisc";
    b["previewSource"] = QUrl("https://example.invalid/b.png");
    QVariantMap r = a; r["id"]="result"; r["key"]="generation-history/result.png"; r["section"]="generation-history";
    r["name"]="result.png"; r["isImage"]=true; r["source"]=QUrl("https://example.invalid/result.png");
    r["previewSource"]=QUrl("https://example.invalid/result.png");
    generation.container()->synchronize("fixture", directory.path(), {a,b,r}, true);
    (void)take(state->drawing_snapshot({1440,868})); (void)take(state->snapshots());
    check(take(find(state,"selectedFileName").snapshot()).text == "A.iisc" && images->saw("https://example.invalid/a.png") &&
          images->saw("https://example.invalid/result.png"), "native Home did not render Society content and previews");
    auto list = find(state,"recentFiles");
    const auto bounds = take(take(list.keyed_content_item(content.recentFiles()->tokenForKey("b"),"recentRow")).snapshot()).bounds;
    good(state->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=bounds.x+100,.y=bounds.y+10,.timestamp=1,.buttons=1,.native_click_count=1}));
    good(state->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=bounds.x+100,.y=bounds.y+10,.timestamp=1.01}));
    if (take(find(state,"selectedFileName").snapshot()).text != "B.iisc") {
        const auto listBounds = take(list.snapshot()).bounds;
        throw std::runtime_error("native row selection did not update the Society-backed panel: selected=" +
            content.recentFiles()->selectedKey.get() + " name=" + content.selectedFile()->name.get() +
            " row=" + std::to_string(bounds.x) + "," + std::to_string(bounds.y) + "," + std::to_string(bounds.width) + "," + std::to_string(bounds.height) +
            " list=" + std::to_string(listBounds.x) + "," + std::to_string(listBounds.y) + "," + std::to_string(listBounds.width) + "," + std::to_string(listBounds.height));
    }
    good(find(state,"recentViewAll").activate()); check(app.storageBrowserSection() == "files", "native View all did not reach the workspace");
    ii::gui::Widget prompt;
    for (const auto &node : take(state->snapshots())) if (node.text_input && node.accessibility_name == "Prompt") { prompt = take(state->find(node.id)); break; }
    good(prompt.focus()); good(state->input({.kind=ii::gui::WidgetInputKind::text,.text="한글 prompt"}));
    check(draft.prompt() == QString::fromUtf8("한글 prompt"), "native Home did not edit the actual generation draft");
    const auto reference = QUrl::fromLocalFile(directory.filePath("reference.png"));
    QImage image(8,8,QImage::Format_ARGB32); image.fill(Qt::blue); check(image.save(reference.toLocalFile()), "reference fixture unavailable");
    good(find(state,"homeActionIcon").activate());
    check(bool(selectedFiles) && referencePicker.pending(), "native attach action did not request the host picker");
    selectedFiles(ii::gui::FileDialogSelection{.paths={reference.toLocalFile().toStdString()}});
    (void)take(state->drawing_snapshot({1440,868}));
    const auto attachment = composer.model()->snapshot.get().attachments;
    check(attachment.size() == 1, "actual reference did not reach the native attachment cards");
    auto attachments = find(state,"homeAttachments");
    good(take(attachments.keyed_content_item(attachment.front().key,"homeAttachmentRemove")).activate());
    check(inputs.attachments().isEmpty() && composer.model()->snapshot.get().attachments.empty(),
          "native attachment remove did not update the actual reference provider");
    good(find(state,"homeActionPrimary").activate()); check(submitted == 1, "native Home did not send the existing submission signal");
    (void)take(state->drawing_snapshot({900,640}));
    auto scroll = find(state,"homeWorkspaceScroll"); good(scroll.set_scroll_offset(80));
    check(take(scroll.snapshot()).scroll_offset > 0, "Home scroll fixture did not move");
    (void)take(state->drawing_snapshot({900,640}));
    const auto first = take(take(list.keyed_content_item(content.recentFiles()->tokenForKey("a"),"recentRow")).snapshot()).visual_bounds;
    good(state->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=first.x+100,.y=first.y+10,.timestamp=2,.buttons=1,.native_click_count=1}));
    good(state->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=first.x+100,.y=first.y+10,.timestamp=2.01}));
    check(content.recentFiles()->selectedKey.get() == "a" && content.selectedFile()->name.get() == "A.iisc",
          "nested file row did not select after resizing and scrolling");
    state->close(); binding->close(); workspace->close(); panel->close(); sidebar->close();
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try { if (argc != 2) return 2; exercise(argv[1]); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
