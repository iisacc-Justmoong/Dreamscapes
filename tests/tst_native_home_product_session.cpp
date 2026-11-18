#include "App/Generation/AdvancedImageParameters.h"
#include "App/Generation/GenerationComposerModel.h"
#include "App/Generation/GenerationController.h"
#include "App/Views/Home/NativeHomeProductSession.h"
#include "App/ViewModels/WorkspaceSession.h"
#include <iiSocietyContainer>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#ifdef DREAMSCAPES_HOME_PRODUCT_WIDGET_TEST
#include "iiGUI/WidgetState.h"
#include <iiSDKCore>
#endif

namespace {
using namespace dreamscapes;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) { if (!value) throw std::runtime_error(value.error().message); }
class Images final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public: std::shared_ptr<const ii::gui::RasterImage> image(std::string_view, std::uint32_t, std::uint32_t) const override { return {}; }
};
#ifdef DREAMSCAPES_HOME_PRODUCT_WIDGET_TEST
template<class T> T take(ii::core::Result<T> value) {
    if (!value) throw std::runtime_error(value.error().message);
    return std::move(*value);
}
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view id) {
    for (const auto &node : take(state->snapshots()))
        if (node.id == id || node.id.ends_with(std::string("/") + std::string(id))) return take(state->find(node.id));
    throw std::runtime_error("Missing native Home widget " + std::string(id));
}
#endif
home::HomeComposerLabels composerLabels() {
    return {"Describe", "Prompt", "Clear", "Negative prompt", "Done", "Media", "Model", "Choose model",
        "Aspect ratio", "Resolution", "Batch size", "Duration", "Frame rate", "Preset", "Load preset", "Save preset",
        "Name", "Save", "Cancel", "Seed", "Random", "Apply", "Attach images", "Paint canvas", "Advanced parameters",
        "More controls", "Generate", [](int n, bool video) { return std::to_string(n)+(video?" videos":" images"); },
        [](const auto &name) { return "Remove " + name; }};
}
void exercise() {
    QTemporaryDir root(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/native-home-product-XXXXXX.societycontainer"));
    check(root.isValid() && iiSocietyContainer::SocietyDrive::create(root.path()).has_value(), "isolated storage unavailable");
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH", root.filePath("settings.ini").toUtf8()); qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
    GenerationRuntime runtime; runtime.nativeInference=true; runtime.modelPreferencesFile=root.filePath("models.conf");
    runtime.failureReportsDirectory=root.path(); runtime.inspectImageModel=[](const auto &) { return iiLocalDiffusion::ImageModelInspection{}; };
    GenerationController generation(runtime); generation.setForeground(false);
    WorkspaceSession workspace; workspace.setGeneration(&generation);
    AdvancedImageParameters parameters(root.filePath("presets.json"));
    GenerationComposerModel draft; draft.setGeneration(&generation); draft.setParameterStore(&parameters);
    workspace.setImageParameters(&parameters);
    int opened=0, newCanvas=0, advanced=0; std::string navigation;
    QObject::connect(&workspace,&WorkspaceSession::openFileDialogRequested,[&] { ++opened; });
    QObject::connect(&workspace,&WorkspaceSession::newCanvasRequested,[&] { ++newCanvas; });
    QObject::connect(&workspace,&WorkspaceSession::imageWorkspaceRequested,[&] { ++advanced; });
    ii::gui::FileDialogCompletion selection; bool chooserUnavailable=false;
    home::NativeHomeProductOptions options;
    options.sidebar = {{"home","Home","nodeshomeFolder"},{"files","Files","database"}};
    options.initialSidebarKey="home"; options.navigate=[&](std::string value) { navigation=std::move(value); };
    options.workspaceLabels={"Workspace","Create and continue","Open file","New canvas","Storage"};
    options.composerLabels=composerLabels();
    options.choices.media={{"Image","Image"},{"Video","Video"}}; options.choices.ratios={{"1:1","1:1"}};
    options.choices.counts={{"1","1 image"}}; options.choices.resolutions={{"1024","1024 × 1024"}};
    options.layout={1000,600,400,{88,1,22,93,138,145},{327,88,144,96,120,228,152}};
    options.metrics={8,12,22,37,44}; options.queueReadySvg="<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"1\"></svg>";
    options.images=std::make_shared<Images>(); options.deviceName="Automatic";
    options.referenceDetail=[](qint64 bytes) { return std::to_string(bytes)+" bytes"; };
    options.dialog=home::homeReferenceDialogOptions("Attach reference images","Images and canvases");
    options.chooseFile=[&](ii::gui::FileDialogOptions,ii::gui::FileDialogCompletion callback) -> ii::core::Result<void> {
        if (chooserUnavailable) return ii::core::fail(ii::core::ErrorCode::unsupported,"File dialog unavailable");
        selection=std::move(callback); return {};
    };
    auto product=std::make_unique<home::NativeHomeProductSession>(&workspace,&draft,std::move(options));
    auto model=product->workspaceModel(); auto sidebar=product->sidebar(); auto composer=product->composer();
    check(model && product->source() && draft.canvas()==product->inputs(), "native Home did not own its complete input and workspace boundary");
#ifdef DREAMSCAPES_HOME_PRODUCT_WIDGET_TEST
    ii::runtime::EventLoop events; ii::gui::detail::WidgetFrameHooks hooks;
    hooks.events=[&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); },ii::gui::WidgetAppearance{},hooks);
    auto host=take(state->content_host({},"root",{1440,868}));
    good(host.set_content([&](ii::gui::WidgetBuilder &builder) { return product->source()->build(builder); }));
    (void)take(state->drawing_snapshot({1440,868})); (void)take(state->snapshots());
#endif
    const QStringList firstSubmission{"job-1", "job-2"};
    check(QMetaObject::invokeMethod(&generation, "submissionQueued", Q_ARG(QStringList, firstSubmission)),
          "cannot deliver generation submission");
    check(workspace.resultVisible() && workspace.resultJobIds()==firstSubmission,
          "native Home did not navigate to the submitted generation batch");
    workspace.dismissResult();
    GenerationController replacement(runtime); replacement.setForeground(false);
    workspace.setGeneration(&replacement);
    check(QMetaObject::invokeMethod(&generation, "submissionQueued", Q_ARG(QStringList, firstSubmission)),
          "cannot deliver retired generation submission");
    check(!workspace.resultVisible(), "retired generation controller still navigated the workspace");
    const QStringList secondSubmission{"job-3"};
    check(QMetaObject::invokeMethod(&replacement, "submissionQueued", Q_ARG(QStringList, secondSubmission)),
          "cannot deliver replacement generation submission");
    check(workspace.resultVisible() && workspace.resultJobIds()==secondSubmission,
          "replacement generation controller did not navigate the workspace");
    workspace.dismissResult(); workspace.setGeneration(&generation);
    good(sidebar->activate("files")); check(navigation=="files" && sidebar->selected.get()=="files", "sidebar command did not reach navigation");
    good(model->openFile()); good(model->newCanvas()); check(opened==1 && newCanvas==1, "Home header did not reach workspace commands");
    good(composer->dispatch(home::HomeComposerCommandKind::editPrompt,"  a scene  "));
#ifdef DREAMSCAPES_HOME_PRODUCT_WIDGET_TEST
    good(find(state,"homeActionIcon").activate());
#else
    good(composer->dispatch(home::HomeComposerCommandKind::chooseAttachments));
#endif
    check(bool(selection), "native attachment command did not open the host picker");
    const auto path=root.filePath("reference.png"); QImage image(8,8,QImage::Format_ARGB32); image.fill(Qt::blue);
    check(image.save(path), "reference fixture unavailable");
    selection(ii::gui::FileDialogSelection{.paths={path.toStdString()}});
    const auto attachments=composer->snapshot.get().attachments;
    check(attachments.size()==1 && product->inputs()->attachments().size()==1, "native picker did not update the real draft");
    good(composer->dispatch(home::HomeComposerCommandKind::removeAttachment,attachments.front().key));
    check(composer->snapshot.get().attachments.empty(), "native removal did not update the provider");
    chooserUnavailable=true;
    check(!composer->dispatch(home::HomeComposerCommandKind::chooseAttachments) && model->error.get()=="File dialog unavailable",
          "unavailable native picker did not display its failure");
    chooserUnavailable=false; workspace.setGenerationRequestError({});
    good(composer->dispatch(home::HomeComposerCommandKind::paint)); check(newCanvas==2, "Paint did not open the existing new-canvas route");
    good(composer->dispatch(home::HomeComposerCommandKind::advanced));
    check(advanced==1 && parameters.parameters().value("prompt").toString()=="a scene", "advanced settings lost the actual draft");
#ifdef DREAMSCAPES_HOME_PRODUCT_WIDGET_TEST
    (void)take(state->drawing_snapshot({1440,868})); good(find(state,"openQueue").activate());
#else
    product->queue()->openQueue();
#endif
    check(workspace.resultVisible(), "queue did not open the real review route");
    const QVariantMap file{{"id","a"},{"containerId","fixture"},{"key","files/a"},{"section","files"},
        {"name","A.iisc"},{"exists",true},{"available",false},{"previewSource",QUrl("https://example.invalid/a.png")}};
    generation.container()->synchronize("fixture",root.path(),{file},true);
    check(product->content()->selectedFile()->name.get()=="A.iisc", "real Society content did not reach the native product session");
    workspace.setGenerationRequestError("Action failed"); check(model->error.get()=="Action failed", "workspace error did not reach Home");
    good(model->setViewport(1440,868,12,16));
    check(generation.jobs().isEmpty(), "Home layout started a generation job");
#ifdef DREAMSCAPES_HOME_PRODUCT_WIDGET_TEST
    (void)take(state->drawing_snapshot({1440,868}));
    check(take(find(state,"selectedFileName").snapshot()).text=="A.iisc", "native product Home did not render Society selection");
    state->close();
#endif
    QFile modelFixture(root.filePath("Models/fixture.safetensors"));
    check(modelFixture.open(QIODevice::WriteOnly) && modelFixture.write("fake model")>0, "model fixture unavailable");
    modelFixture.close();
    check(generation.connectStorage(root.path()), "isolated generation storage did not connect");
    check(!generation.models().isEmpty(), "isolated generation model was not discovered");
    workspace.dismissResult(); draft.setGenerationCount(2);
    good(composer->dispatch(home::HomeComposerCommandKind::submit));
    check(generation.jobs().size()==2 && workspace.resultVisible() && workspace.resultJobIds().size()==2,
          qPrintable(QString("native submit did not open its queued batch: %1").arg(workspace.generationRequestError())));
    for (const auto &job : generation.jobs())
        check(job.toMap().value("state").toString()=="queued", "paused test unexpectedly executed generation");
    product.reset();
    check(!draft.canvas() && !composer->commandsAvailable.get(), "retired Home session retained inputs or commands");
    navigation.clear(); (void)sidebar->activate("home"); check(navigation.empty(), "retired Home session retained sidebar navigation");
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
