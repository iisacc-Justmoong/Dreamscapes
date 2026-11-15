#include "App/Native/NativeApplicationShellView.h"
#include "App/Native/NativeApplicationCommand.h"
#include "App/Views/Result/NativeReviewSession.h"
#include "App/Views/Result/NativeReviewGallery.h"
#include "App/Native/NativeDocumentSession.h"
#include "App/Views/Editor/NativeTimelinePanelView.h"
#include "App/Generation/ModelPreferences.h"
#include "NativeAssets.h"
#include "iiGUI/WidgetState.h"
#include <iiVisualCodec/ImageCodec.h>
#include <iiFileProviderNative.h>
#include <iiSDKCore/Runtime/FrameLoop.h>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

class ManualFrameExecutor final : public iiSharedCanvas::FrameRenderExecutor {
  public:
    void post(std::function<void()> task) override {tasks.push_back(std::move(task));}
    std::size_t maxConcurrency() const noexcept override {return 1;}
    void drain() {
        for (int count=0;!tasks.empty();++count) {
            if (count>100) throw std::runtime_error("frame work did not settle");
            auto task=std::move(tasks.front()); tasks.erase(tasks.begin()); task();
        }
    }
    std::vector<std::function<void()>> tasks;
};

template<class T> T take(ii::core::Result<T> value) {
    if (!value) {if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message);}
    return std::move(*value);
}
void good(ii::core::Result<void> value) {
    if (!value) {if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message);}
}
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
class TrackingImages final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view source,std::uint32_t,std::uint32_t) const override {
        if (source!="fixture-preview") return {};
        ++calls;
        auto pixels=ii::gui::rgba_image(1,1,{40,80,120,255});
        return pixels?*pixels:nullptr;
    }
    mutable int calls=0;
};
class TestLicenseProbe final : public dreamscapes::native::LicenseProbe {
  public:
    ii::core::Result<void> start(Completion value) override {completion=std::move(value);return {};}
    void stop() noexcept override {++stops;}
    Completion completion;
    int stops=0;
};
int main() {
    try {
        using namespace dreamscapes::native;
        using namespace ii::gui;
        {
            iiFileProvider::native::TemporaryDirectory temporary(std::filesystem::canonical(DREAMSCAPES_TEST_RESOURCES));
            check(temporary.isValid(),"cannot create review import fixture");
            const auto path=temporary.path()/"review.png";
            const std::vector<std::uint8_t> rgba{0x12,0x34,0x56,0xff,0x12,0x34,0x56,0xff,
                                                 0x12,0x34,0x56,0xff,0x12,0x34,0x56,0xff};
            const auto encoded=ii::visual::image::encode_png({2,2,ii::visual::image::PixelFormat::rgba8,0,rgba});
            check(bool(encoded),"cannot encode review import fixture");
            iiFileProvider::native::File::write(path,
                std::string(reinterpret_cast<const char *>(encoded->data()),encoded->size()));
            ii::runtime::EventLoop events;
            std::vector<ii::runtime::Task> jobs;
            auto shell=NativeApplicationShell::create(queueReadySvg);
            good(shell->connectCanvasFiles(events,[&](ii::runtime::Task task){jobs.push_back(std::move(task));return true;}));
            ii::gui::FileDialogCompletion saveCompletion;
            shell->setCanvasFilePicker([&](auto,auto completion) {saveCompletion=std::move(completion);return ii::core::Result<void>{};});
            using dreamscapes::vm::Value;using dreamscapes::vm::Object;
            auto review=shell->reviewSession();
            review->replaceEntries({Value(Object{{"id",Value("one")},{"path",Value(path.string())},{"mediaType",Value("Image")}}),
                Value(Object{{"id",Value("two")},{"path",Value((temporary.path()/"other.png").string())},{"mediaType",Value("Image")}})});
            detail::WidgetFrameHooks hooks;
            hooks.events=[&]() -> ii::core::Result<ii::runtime::EventLoop *> {return &events;};
            auto tree=std::make_shared<detail::WidgetState>([](ii::runtime::Task task){task();},WidgetAppearance{},hooks);
            good(tree->construct([&](WidgetBuilder &builder){return buildNativeApplicationShell(builder,{},shell);}));
            good(tree->activate_paint_view());good(shell->setViewport({1200,800}));good(shell->navigate("history"));
            (void)take(tree->drawing_snapshot({1200,800}));
            auto gallery=shell->reviewGallery();check(bool(gallery),"review gallery did not attach to the application");
            auto menu=gallery->contextMenu();
            check(menu->open(review->keyAt(0)) && menu->context.get().canSaveFile &&
                  menu->invoke(dreamscapes::review::ReviewContextAction::saveFile),"review original save is not connected");
            review->focusAt(1);
            const auto saved=temporary.path()/"saved.png";
            saveCompletion(ii::gui::FileDialogSelection{.paths={saved.string()}});
            check(jobs.size()==1 && !std::filesystem::exists(saved),"review save did not use a background worker");
            auto saveTask=std::move(jobs.front());jobs.clear();saveTask();(void)events.poll();
            check(iiFileProvider::native::File::read(saved)==iiFileProvider::native::File::read(path) &&
                  shell->pageStatus.get()=="Result saved" && !shell->documentSession()->hasDocument.get(),
                  "review save did not preserve the clicked original and its status");
            review->selectAt(0);review->selectAt(1,false,true);
            (void)take(tree->drawing_snapshot({1200,800}));
            Widget edit;bool found=false;
            for (const auto &node:take(tree->snapshots())) if (node.id.ends_with("/edit")) {edit=take(tree->find(node.id));found=true;}
            check(found && !take(edit.snapshot()).enabled,"review enabled an unsupported multi-image edit");
            review->clearSelection();review->selectAt(1);
            (void)take(tree->drawing_snapshot({1200,800}));
            good(edit.activate());
            check(jobs.size()==1,"missing review image did not reach the file worker");
            auto missing=std::move(jobs.front());jobs.clear();missing();(void)events.poll();
            check(shell->hasPageStatus.get() && !shell->canvasFiles()->error.get().empty() &&
                  shell->pageStatus.get()==shell->canvasFiles()->error.get() &&
                  !shell->documentSession()->hasDocument.get(),"a failed review import hid its error or opened a document");
            review->clearSelection();review->selectAt(0);
            (void)take(tree->drawing_snapshot({1200,800}));
            check(take(edit.snapshot()).enabled,"review did not enable the local image edit action");
            good(edit.activate());
            check(jobs.size()==1 && shell->canvasFiles()->busy.get(),"review edit did not dispatch image import");
            auto task=std::move(jobs.front());jobs.clear();task();(void)events.poll();
            check(shell->documentSession()->hasDocument.get() && shell->documentSession()->dirty.get() &&
                  shell->documentSession()->title.get()=="review.png" && !shell->atHome.get(),
                  "review edit did not open the selected image in the editor");
            tree->close();shell->disconnectCanvasFiles();
        }
        {
            ii::runtime::EventLoop licenseLoop;
            auto probe=std::make_shared<TestLicenseProbe>();
            auto license=LaunchLicense::create(licenseLoop,probe);
            auto shell=NativeApplicationShell::create(queueReadySvg);
            shell->setLaunchLicense(license);
            auto settings=shell->preferencesWorkspace();
            good(settings->select("Account"));
            auto value=[&](std::string_view id) {
                for (const auto &field:settings->fields.get()) if (field->id==id) return field->value.get();
                throw std::runtime_error("Account settings field is missing");
            };
            check(value("license")=="Not checked" && value("account")=="Unavailable",
                  "Account settings invented a launch license");
            good(license->start());
            check(value("license")=="Checking", "Account settings missed the pending license check");
            probe->completion({true,true,"licensed","fixture-account"});(void)licenseLoop.poll();
            check(value("license")=="Licensed" && value("connection")=="Connected" &&
                  value("account")=="fixture-account", "Account settings missed the verified session");
            for (const auto &field:settings->fields.get()) check(!field->editable.get(),"license status is editable");
            auto replacementProbe=std::make_shared<TestLicenseProbe>();
            auto replacement=LaunchLicense::create(licenseLoop,replacementProbe);
            shell->setLaunchLicense(replacement);
            check(value("license")=="Not checked" && value("account")=="Unavailable" && probe->stops>0,
                  "replacing a license kept stale account state");
            good(replacement->start());
            replacementProbe->completion({true,false,"license_not_owned","other-account"});(void)licenseLoop.poll();
            check(value("license")=="Not licensed" && value("connection")=="Connected" && value("account")=="other-account",
                  "a connected denial was confused with an unavailable verifier");
            shell->setLaunchLicense({});
            check(!settings->available.get() && settings->fields.get().empty(),
                  "disconnecting a license kept stale account state");
        }
        {
            ii::runtime::EventLoop licenseLoop;
            auto probe=std::make_shared<TestLicenseProbe>();
            auto license=LaunchLicense::create(licenseLoop,probe);
            auto licensedShell=NativeApplicationShell::create(queueReadySvg);
            licensedShell->setLaunchLicense(license);
            detail::WidgetFrameHooks licenseHooks;
            licenseHooks.events=[&]() -> ii::core::Result<ii::runtime::EventLoop *> {return &licenseLoop;};
            auto tree=std::make_shared<detail::WidgetState>([](ii::runtime::Task task){task();},WidgetAppearance{},licenseHooks);
            good(tree->construct([&](WidgetBuilder &builder){return buildNativeApplicationShell(builder,{},licensedShell);}));
            good(tree->activate_paint_view());good(licensedShell->setViewport({1000,700}));
            good(license->start());
            good(licensedShell->navigate("editor"));
            iiSharedCanvas::Document pending;pending.extent={4,4};
            good(licensedShell->documentSession()->attach(pending,"Pending.iisc",true));
            (void)take(tree->drawing_snapshot({1000,700}));
            check(!license->gateVisible.get() && licensedShell->documentSession()->dirty.get(),
                  "the workspace was blocked while the license was pending");
            probe->completion({false,false,"society_unavailable",{}});(void)licenseLoop.poll();
            (void)take(tree->drawing_snapshot({1000,700}));
            bool gateTitle=false;
            for (const auto &node:take(tree->snapshots())) if (node.id.ends_with("/licenseTitle") && node.text=="Connect to Society") gateTitle=true;
            check(gateTitle && license->gateVisible.get() && !licensedShell->navigate("home") &&
                  licensedShell->documentSession()->title.get()=="Pending.iisc" && licensedShell->documentSession()->dirty.get(),
                  "license denial failed to mount the SDK gate or lost the working document");
            tree->close();license->close();
        }
        {
            using namespace iiSharedCanvas;
            Document dynamic; dynamic.extent={2,2};dynamic.timeline={{24,1},24};
            dynamic.assets.emplace_back(RasterAsset{"one",makeRasterLayer(2,2,0xff000000)});
            dynamic.assets.emplace_back(RasterAsset{"two",makeRasterLayer(2,2,0xffffffff)});
            dynamic.layers.emplace_back(DynamicBitmapLayer{{"dynamic","Dynamic",true,1,{},RasterBlendMode::SourceOver,
                                                             LayerFrameRange{3,14}},KeyframedSource{{0,4,12,16}}});
            layerProperties(dynamic.layers[0]).motion={{2,{}},{8,{}},{18,{}}};
            dynamic.frames={{0,{{"dynamic","one"}}},{4,{{"dynamic","two"}}},
                            {12,{{"dynamic","one"}}},{16,{{"dynamic","two"}}}};
            dynamic.audioAssets.push_back(AudioAsset{"audio-source",24000,1,std::vector<std::int16_t>(6000)});
            dynamic.audioTracks.push_back(AudioTrackLayer{.id="audio-track",.name="Narration",.clips={
                AudioClip{.id="voice",.name="Voice",.assetId="audio-source",.startFrame=3,.durationFrames=6}}});
            auto nativeSession=DocumentSession::create();good(nativeSession->attach(dynamic,"Native.iisc"));
            auto nativeTimeline=dreamscapes::editor::TimelinePanelModel::create(nativeSession,22);
            nativeTimeline->sizeChanged(500,120);
            auto nativeOptions=nativeTimeline->options.get();nativeOptions.pixels_per_frame=10;
            nativeTimeline->viewportChanged(nativeOptions);
            auto tree=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();});
            auto host=take(tree->content_host({},"timeline-test",{500,120}));
            const auto source=dreamscapes::editor::timelinePanelSource(nativeTimeline);
            good(host.set_content([source](WidgetBuilder &builder){return source->build(builder);}));
            (void)take(tree->drawing_snapshot({500,120}));
            const auto audioRows=take(nativeTimeline->project({.frames={0,24},.top=22,.bottom=44}));
            check(audioRows.tracks.size()==1 && audioRows.clips.size()==1 && audioRows.clips[0].locked,
                  "the mounted timeline did not provide the document's audio row");
            good(tree->input({.kind=WidgetInputKind::pointer_down,.x=252,.y=55}));
            good(tree->input({.kind=WidgetInputKind::pointer_move,.x=282,.y=55}));
            good(tree->input({.kind=WidgetInputKind::pointer_up,.x=282,.y=55}));
            check(layerProperties(nativeSession->document()->layers[0]).frameRange==std::optional<LayerFrameRange>{{6,17}} &&
                  keyframedLayerSource(nativeSession->document()->layers[0])->frameIndices==std::vector<FrameIndex>({0,7,15,19}),
                  "native timeline pointer drag did not update the dynamic document");
            tree->close();
        }
        const std::string_view listCommand[]{"--list-views"};
        const auto listed=applicationCommand(listCommand);
        check(listed && listed->code==0 && listed->text.find("src/App/Views/Editor/CanvasEditor.lvui")!=std::string::npos,
              "native agent command omitted the migrated editor view");
        const std::string_view unknownCommand[]{"--unknown"};
        check(applicationCommand(unknownCommand)->code==2,"native agent command accepted an unknown option");
        const std::string_view finderLaunch[]{"-psn_0_12345"};
        check(!applicationCommand(finderLaunch),"Finder launch was mistaken for an agent command");
        const std::string_view viewCommand[]{"--view","image"};
        const auto view=applicationCommand(viewCommand);
        check(view && view->initialView=="image","native view launch command did not retain its route");
        check(!migratedViews().empty(),"compiled view inventory is empty");
        auto canvasShell=NativeApplicationShell::create(queueReadySvg);
        check(!canvasShell->canvasWorkspace(),"canvas catalogue was eagerly parsed at startup");
        good(canvasShell->navigate("canvas"));
        good(canvasShell->canvasWorkspace()->customSize());
        good(canvasShell->canvasWorkspace()->edit("draftWidth","640"));
        good(canvasShell->canvasWorkspace()->edit("draftHeight","480"));
        good(canvasShell->canvasWorkspace()->createCanvas());
        check(canvasShell->documentSession()->hasDocument.get() && canvasShell->documentSession()->dirty.get() &&
              canvasShell->documentSession()->dimensions.get()=="640 × 480 px",
              "new canvas did not reach the shared unsaved editor document");
        good(canvasShell->canvasWorkspace()->edit("draftWidth","800"));
        check(!canvasShell->canvasWorkspace()->createCanvas() && canvasShell->documentSession()->dimensions.get()=="640 × 480 px",
              "creating another canvas replaced unsaved work");
        for (const auto &view : migratedViews()) {
            auto pageState=std::make_shared<detail::WidgetState>([](ii::runtime::Task task){task();});
            auto pageModel=std::make_shared<MigratedViewModel>();
            auto mounted=pageState->construct([&](WidgetBuilder &builder){return buildMigratedView(builder,{},view.source,pageModel);});
            if (!mounted) throw std::runtime_error(std::string(view.replacement)+": "+mounted.error().message);
            good(pageState->activate_paint_view());
            (void)take(pageState->drawing_snapshot({640,480}));
            pageState->close();
        }
        auto shell=NativeApplicationShell::create(queueReadySvg);
        good(shell->preferencesWorkspace()->select("About"));
        check(shell->preferencesWorkspace()->available.get() && shell->preferencesWorkspace()->fields.get().size()==1 &&
              shell->preferencesWorkspace()->fields.get()[0]->value.get()==applicationVersion &&
              !shell->preferencesWorkspace()->fields.get()[0]->editable.get(),
              "About did not expose the actual read-only build version");
        good(shell->preferencesWorkspace()->select("General"));
        auto reviewImages=std::make_shared<TrackingImages>();
        shell->setReviewImageResolver(reviewImages);
        ii::runtime::EventLoop events;
        auto frameExecutor=std::make_shared<ManualFrameExecutor>();
        good(shell->connectFrameRenderer(events,frameExecutor));
        std::vector<ii::runtime::Task> fileJobs;
        good(shell->connectCanvasFiles(events,[&](ii::runtime::Task task){fileJobs.push_back(std::move(task)); return true;}));
        std::optional<ii::gui::FileDialogMode> dialogMode;
        ii::gui::FileDialogCompletion dialogCompletion;
        shell->setCanvasFilePicker([&](ii::gui::FileDialogOptions options,ii::gui::FileDialogCompletion completion) {
            dialogMode=options.mode; dialogCompletion=std::move(completion); return ii::core::Result<void>{};
        });
        detail::WidgetFrameHooks hooks;
        hooks.events=[&]() -> ii::core::Result<ii::runtime::EventLoop *> {return &events;};
        auto state=std::make_shared<detail::WidgetState>([](ii::runtime::Task task){task();},WidgetAppearance{},hooks);
        good(state->construct([&](WidgetBuilder &builder){return buildNativeApplicationShell(builder,{},shell);}));
        good(state->activate_paint_view());
        good(shell->setViewport({1440,868}));
        auto draw=[&] { (void)take(state->drawing_snapshot({1440,868})); return take(state->snapshots()); };
        auto hasText=[&](std::string_view text) {
            for (const auto &node : draw()) if (node.text==text) return true;
            return false;
        };
        check(hasText("Recent files"),"native Home did not mount");
        check(hasText("Generation unavailable"),"unconnected generation was not reported");
        good(shell->navigate("tools"));
        check(hasText("No canvas open") && hasText("Layers"),"Tools did not open the live editor workspace");
        good(shell->navigate("home"));
        good(shell->recentGenerations()->setFiles(dreamscapes::vm::Value(dreamscapes::vm::Array{
            dreamscapes::vm::Value(dreamscapes::vm::Object{{"id",dreamscapes::vm::Value("home-image")},
                {"name",dreamscapes::vm::Value("Recent fixture")},{"source",dreamscapes::vm::Value("file:///fixture.png")},
                {"mediaType",dreamscapes::vm::Value("Image")}})}),{"fixture-preview"},false));
        check(hasText("Recent fixture") && reviewImages->calls>0,"Home did not use the app's dynamic image resolver");
        good(shell->recentGenerations()->setFiles(dreamscapes::vm::Value(dreamscapes::vm::Array{}),{},false));
        reviewImages->calls=0;
        Widget homeOpen;
        for (const auto &node:draw()) if (node.id.ends_with("/homeOpenFile")) homeOpen=take(state->find(node.id));
        good(homeOpen.activate());
        check(dialogMode==ii::gui::FileDialogMode::open && bool(dialogCompletion),"Home did not invoke the file picker");
        dialogCompletion(ii::gui::FileDialogSelection{.canceled=true});
        Widget recentFilesAll;
        for (const auto &node:draw()) if (node.id.ends_with("/recentViewAll")) recentFilesAll=take(state->find(node.id));
        good(recentFilesAll.activate());
        check(!shell->atHome.get() && hasText("Files"),"recent files View all did not open the file browser");
        good(shell->navigate("home"));
        Widget generationsAll;
        for (const auto &node:draw()) if (node.id.ends_with("/recentGenerationViewAll")) generationsAll=take(state->find(node.id));
        good(generationsAll.activate());
        check(!shell->atHome.get() && hasText("Generation history unavailable"),"recent generations View all did not open history");
        good(shell->navigate("home"));
        good(shell->navigate("canvas"));
        check(hasText("Canvas details"),"new canvas workspace did not mount");
        Widget canvasWidth;
        for (const auto &node : draw())
            if (node.text_input && node.accessibility_name=="Canvas width") canvasWidth=take(state->find(node.id));
        good(canvasWidth.focus());
        good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::select_all}));
        good(state->input({.kind=WidgetInputKind::text,.text="777"}));
        check(shell->canvasWorkspace()->widthText.get()=="777","canvas size input did not reach the view model");
        good(shell->navigate("home"));
        Widget imageAction;
        for (const auto &node : draw())
            if (node.id.ends_with("/sidebarAction") && node.accessibility_name=="Image")
                imageAction=take(state->find(node.id));
        good(imageAction.activate());
        check(hasText("Create image"),"Image LVUI view was not mounted");
        good(shell->setGenerationModelCatalog({{"image-a","Image A",true,false},{"image-b","Image B",true,false},
                                               {"ltx","LTX",false,true}}));
        Widget modelControl;bool foundModel=false;
        for (const auto &node:draw()) if (node.id.ends_with("/control")) {
            auto candidate=take(state->find(node.id));auto options=candidate.action_options();
            if (options && options->text=="Image A") {modelControl=candidate;foundModel=true;break;}
        }
        if (!foundModel) {
            std::string nodes;
            for (const auto &node:draw()) if (node.id.find("generationChoice")!=std::string::npos || node.id.ends_with("/control")) {
                nodes+=node.id+" text="+node.text+" accessible="+node.accessibility_name+" ";
                if (auto widget=state->find(node.id); widget) if (auto options=widget->action_options(); options) nodes+="action="+options->text+" ";
            }
            throw std::runtime_error("mounted Advanced Image did not display the supplied model catalogue: "+nodes);
        }
        good(modelControl.focus());
        good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::down}));
        check(shell->composer()->snapshot.get().modelId=="image-b" &&
              dreamscapes::vm::text(dreamscapes::vm::get(shell->generation(false)->parameters(),"model"))=="image-b",
              "model selector keyboard input did not update the shared generation model");
        Widget prompt;
        for (const auto &node : draw())
            if (node.text_input && node.accessibility_name=="Prompt") prompt=take(state->find(node.id));
        const auto before = take(prompt.snapshot());
        check(before.visible,"image prompt was clipped by the application viewport");
        good(prompt.focus());
        const auto focused = take(prompt.snapshot());
        good(state->input({.kind=WidgetInputKind::text,.text="dynamic prompt"}));
        if (shell->composer()->snapshot.get().prompt!="dynamic prompt")
            throw std::runtime_error("image input did not update the Home draft: id="+before.id+" accessible="+before.accessibility_name+
                                     " enabled="+std::to_string(before.enabled)+" visible="+std::to_string(before.visible)+
                                     " bounds="+std::to_string(before.bounds.width)+"x"+std::to_string(before.bounds.height)+
                                     " focused="+std::to_string(focused.focused)+" input="+take(prompt.snapshot()).text+
                                     " draft="+shell->composer()->snapshot.get().prompt);
        good(shell->generation(false)->edit("width","2048"));
        bool widthUpdated=false;
        Widget widthInput;
        for (const auto &node : draw()) {
            if (node.text_input && node.accessibility_name=="Width") {
                auto widget=take(state->find(node.id));
                widthUpdated=take(widget.text_content({0,node.text_length})).text=="2048";
                widthInput=widget;
            }
        }
        check(widthUpdated,"image width edit did not update the mounted view");
        good(widthInput.focus());
        good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::select_all}));
        good(state->input({.kind=WidgetInputKind::text,.text="33"}));
        check(shell->generation(false)->field("width")->invalid.get() &&
              dreamscapes::vm::number(dreamscapes::vm::get(shell->generation(false)->parameters(),"width"))==2048,
              "invalid native input overwrote the accepted width");
        good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::select_all}));
        good(state->input({.kind=WidgetInputKind::text,.text="1024"}));
        check(!shell->generation(false)->field("width")->invalid.get() &&
              dreamscapes::vm::number(dreamscapes::vm::get(shell->generation(false)->parameters(),"width"))==1024,
              "native input did not recover from an invalid width");
        good(shell->setViewport({320,480}));
        (void)take(state->drawing_snapshot({320,480}));
        auto root=take(take(state->find("applicationRoot")).snapshot());
        check(root.bounds.width==320 && root.bounds.height==480,"native application viewport did not resize");
        good(shell->setViewport({1440,868}));
        good(shell->navigate("video"));
        check(hasText("Create video"),"native route did not replace the preceding view");
        check(shell->generation(true)->field("prompt")->text.get()=="dynamic prompt", "video view lost the shared prompt");
        bool ltxVisible=false;
        for (const auto &node:draw()) if (node.id.ends_with("/control")) {
            auto candidate=take(state->find(node.id));auto options=candidate.action_options();
            if (options && options->text=="LTX") {ltxVisible=true;break;}
        }
        check(ltxVisible,"mounted Advanced Video did not display its model catalogue");
        good(shell->navigate("history"));
        check(hasText("RESULTS") && hasText("Generation history unavailable"),"native review did not report its disconnected source");
        auto session=shell->reviewSession();
        dreamscapes::vm::Value item(dreamscapes::vm::Object{{"id",dreamscapes::vm::Value("fixture")},
            {"documentSource",dreamscapes::vm::Value("file:///fixture.iisc")},
            {"thumbnailSource",dreamscapes::vm::Value("fixture-preview")},{"mediaType",dreamscapes::vm::Value("Image")}});
        session->replaceEntries({item});
        check(hasText("1 results") && reviewImages->calls>0,
              "review did not project a session update through the application's image resolver");
        session->selectAt(0);
        Widget shortlist;
        for (const auto &node : draw()) if (node.id.ends_with("/shortlist")) shortlist=take(state->find(node.id));
        good(shortlist.activate());
        check(session->shortlistedCount()==1,"review action did not update its session");
        good(shell->navigate("image"));
        good(shell->navigate("history"));
        check(session->shortlistedCount()==1 && hasText("1 results"),"review session was lost during navigation");
        good(shell->navigate("editor"));
        check(hasText("No canvas open"),"editor did not expose the empty document state");
        iiSharedCanvas::Document canvas; canvas.extent={8,6}; canvas.timeline={{24,1},12};
        canvas.assets.emplace_back(iiSharedCanvas::RasterAsset{"pixels",makeRasterLayer(8,6,0xff123456)});
        canvas.layers.emplace_back(iiSharedCanvas::StaticBitmapLayer{{"paint","Paint"},iiSharedCanvas::StaticSource{"pixels"}});
        good(shell->documentSession()->attach(canvas,"Canvas.iisc"));
        const bool canvasNameVisible=hasText("Canvas.iisc"),dimensionsVisible=hasText("8 × 6 px"),paintVisible=hasText("Paint");
        if (!canvasNameVisible || !dimensionsVisible || !paintVisible)
            throw std::runtime_error("editor did not project document state: name="+std::to_string(canvasNameVisible)+
                " dimensions="+std::to_string(dimensionsVisible)+" layer="+std::to_string(paintVisible)+
                " file="+std::to_string(shell->editorWorkspace()->filePanelActive.get()));
        Widget timelineCanvas; bool foundTimeline=false;
        for (const auto &node:draw()) if (node.id.ends_with("/editorTimelineCanvas")) {
            timelineCanvas=take(state->find(node.id)); foundTimeline=true;
        }
        check(foundTimeline,"editor did not mount the native timeline");
        auto timelineBounds=take(timelineCanvas.snapshot()).bounds;
        check(timelineBounds.width>200 && timelineBounds.height>44,"timeline viewport was not laid out");
        good(state->input({.kind=WidgetInputKind::pointer_down,.x=timelineBounds.x+197,.y=timelineBounds.y+20}));
        good(state->input({.kind=WidgetInputKind::pointer_up,.x=timelineBounds.x+197,.y=timelineBounds.y+20}));
        check(shell->documentSession()->frame.get()==5 && !shell->documentSession()->dirty.get(),
              "timeline ruler input did not scrub the real document without editing it");
        good(shell->documentSession()->seek(0));
        auto trackY=timelineBounds.y+55;
        good(state->input({.kind=WidgetInputKind::pointer_down,.x=timelineBounds.x+203,.y=trackY}));
        good(state->input({.kind=WidgetInputKind::pointer_move,.x=timelineBounds.x+199,.y=trackY}));
        auto preview=take(shell->editorWorkspace()->timelinePanel()->project({.frames={0,12},.top=0,.bottom=22}));
        check(preview.clips.size()==1 && preview.clips[0].end==8 && !shell->documentSession()->dirty.get(),
              "timeline widget did not preview a trim without editing the canvas");
        good(state->input({.kind=WidgetInputKind::pointer_cancel}));
        preview=take(shell->editorWorkspace()->timelinePanel()->project({.frames={0,12},.top=0,.bottom=22}));
        check(preview.clips[0].end==12 && !shell->documentSession()->dirty.get(),
              "canceling the timeline widget drag did not restore the range");
        good(state->input({.kind=WidgetInputKind::pointer_down,.x=timelineBounds.x+203,.y=trackY}));
        good(state->input({.kind=WidgetInputKind::pointer_move,.x=timelineBounds.x+199,.y=trackY}));
        good(shell->navigate("home"));
        check(!shell->documentSession()->dirty.get() && shell->editorWorkspace()->timelinePanel()->status.get().empty(),
              "leaving the editor while dragging changed the document or reported a retired widget as an error");
        good(shell->navigate("editor"));
        for (const auto &node:draw()) if (node.id.ends_with("/editorTimelineCanvas"))
            timelineCanvas=take(state->find(node.id));
        timelineBounds=take(timelineCanvas.snapshot()).bounds;
        trackY=timelineBounds.y+55;
        good(state->input({.kind=WidgetInputKind::pointer_down,.x=timelineBounds.x+203,.y=trackY}));
        good(state->input({.kind=WidgetInputKind::pointer_up,.x=timelineBounds.x+199,.y=trackY}));
        const auto &trimmed=iiSharedCanvas::layerProperties(shell->documentSession()->document()->layers[0]).frameRange;
        check(shell->documentSession()->dirty.get() && trimmed && trimmed->lastFrame==7,
              "timeline widget did not commit a trim to the canvas");
        auto keyedCanvas=canvas;
        keyedCanvas.assets.emplace_back(iiSharedCanvas::RasterAsset{"pixels2",makeRasterLayer(8,6,0xffabcdef)});
        keyedCanvas.layers.clear();
        keyedCanvas.layers.emplace_back(iiSharedCanvas::DynamicBitmapLayer{{"paint","Paint"},iiSharedCanvas::KeyframedSource{{0,5}}});
        keyedCanvas.frames={{0,{{"paint","pixels"}}},{5,{{"paint","pixels2"}}}};
        good(shell->documentSession()->attach(keyedCanvas,"Keyed.iisc"));
        auto keyOptions=shell->editorWorkspace()->timelinePanel()->options.get();keyOptions.pixels_per_frame=10;
        shell->editorWorkspace()->timelinePanel()->viewportChanged(keyOptions);
        (void)draw();timelineBounds=take(timelineCanvas.snapshot()).bounds;trackY=timelineBounds.y+55;
        good(state->input({.kind=WidgetInputKind::pointer_down,.x=timelineBounds.x+242,.y=trackY}));
        good(state->input({.kind=WidgetInputKind::pointer_move,.x=timelineBounds.x+272,.y=trackY}));
        check(!shell->documentSession()->dirty.get(),"timeline key preview changed the document");
        good(state->input({.kind=WidgetInputKind::pointer_up,.x=timelineBounds.x+272,.y=trackY}));
        check(iiSharedCanvas::keyframedLayerSource(shell->documentSession()->document()->layers[0])->frameIndices==
              std::vector<iiSharedCanvas::FrameIndex>({0,8}),
              "native timeline key drag did not update the actual source frame");
        keyOptions.pixels_per_frame=1;shell->editorWorkspace()->timelinePanel()->viewportChanged(keyOptions);
        good(shell->documentSession()->attach(canvas,"Canvas.iisc"));
        frameExecutor->drain(); (void)events.poll();
        const auto painted=take(state->drawing_snapshot({1440,868}));
        std::size_t imageCount=painted.images.size();
        for (const auto &layer:painted.overlays) imageCount+=layer.images.size();
        for (const auto &scene:painted.scenes) for (const auto &layer:scene.layers) imageCount+=layer.images.size();
        if (!imageCount) {
            std::string nodes;
            for (const auto &node:take(state->snapshots()))
                if (node.id.find("editorCanvas")!=std::string::npos)
                    nodes+=node.id+":"+std::to_string(node.bounds.width)+"x"+std::to_string(node.bounds.height)+" ";
            throw std::runtime_error("editor did not paint the document frame: hasFrame="+
                std::to_string(shell->editorWorkspace()->hasFrame.get())+" status="+shell->editorWorkspace()->renderStatus.get()+
                " queued="+std::to_string(frameExecutor->tasks.size())+" nodes="+nodes);
        }
        Widget layer;
        for (const auto &node : draw()) if (node.id.ends_with("/editorLayerName") && node.text=="Paint") layer=take(state->find(node.id));
        good(layer.activate());
        check(shell->documentSession()->selectedLayer.get()=="paint" && !shell->documentSession()->dirty.get(),
              "editor layer selection changed document content or did not reach the model");
        check(hasText("Selected"),"editor layer selection was not reflected in the view");
        Widget fileTool;
        for (const auto &node:draw()) if (node.id.ends_with("/editorFileTool")) fileTool=take(state->find(node.id));
        good(fileTool.activate());
        check(shell->editorWorkspace()->filePanel()->informationMode.get() && hasText("Information") &&
              !shell->documentSession()->dirty.get(),"File tool did not mount its document-backed panel");
        bool fileValueVisible=false;
        for (const auto &node:draw()) if (node.id.find("fileInspectorValue")!=std::string::npos &&
            node.bounds.width>0 && node.bounds.height>0) fileValueVisible=true;
        check(fileValueVisible,"file inspector values had no visible widget geometry");
        Widget contentsTab;
        for (const auto &node:draw()) if (node.id.ends_with("/fileContentsTab")) contentsTab=take(state->find(node.id));
        good(contentsTab.activate());
        check(shell->editorWorkspace()->filePanel()->contentsMode.get() && hasText("Contents") &&
              !shell->documentSession()->dirty.get() && frameExecutor->tasks.empty(),
              "file segment navigation changed the canvas, requested a render or failed to mount");
        Widget layerTool;
        for (const auto &node:draw()) if (node.id.ends_with("/editorLayerTool")) layerTool=take(state->find(node.id));
        good(layerTool.activate());
        Widget opacityInput;
        for (const auto &node:draw()) if (node.id.ends_with("/layerOpacityInput")) opacityInput=take(state->find(node.id));
        good(opacityInput.focus());
        good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::select_all}));
        good(state->input({.kind=WidgetInputKind::text,.text="50"}));
        good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter}));
        check(std::abs(iiSharedCanvas::layerProperties(shell->documentSession()->document()->layers[0]).opacity-0.5)<0.0001,
              "opacity input did not change the selected canvas layer");
        Widget slider,blend;
        for (const auto &node:draw()) {
            if (node.id.ends_with("/layerOpacitySlider")) slider=take(state->find(node.id));
            if (node.id.ends_with("/layerBlend")) blend=take(state->find(node.id));
        }
        const auto sliderBounds=take(slider.snapshot()).bounds;
        check(sliderBounds.width>20 && sliderBounds.height>0,"layer opacity slider has no usable layout");
        good(state->input({.kind=WidgetInputKind::pointer_down,.x=sliderBounds.x+sliderBounds.width*.75,
                           .y=sliderBounds.y+sliderBounds.height/2}));
        good(state->input({.kind=WidgetInputKind::pointer_up,.x=sliderBounds.x+sliderBounds.width*.75,
                           .y=sliderBounds.y+sliderBounds.height/2}));
        const auto opacity=iiSharedCanvas::layerProperties(shell->documentSession()->document()->layers[0]).opacity;
        check(opacity>0.65 && opacity<0.85,"opacity slider did not change the selected canvas layer");
        good(blend.activate());
        Widget multiply;
        for (const auto &node:draw()) if (node.id.find("layerBlendChoice")!=std::string::npos && node.text=="Multiply")
            multiply=take(state->find(node.id));
        good(multiply.activate());
        check(iiSharedCanvas::layerProperties(shell->documentSession()->document()->layers[0]).blendMode==RasterBlendMode::Multiply,
              "blend menu did not change the selected canvas layer");
        Widget information,layerTab;
        for (const auto &node:draw()) {
            if (node.id.ends_with("/layerInformationTab")) information=take(state->find(node.id));
            if (node.id.ends_with("/layerPropertiesTab")) layerTab=take(state->find(node.id));
        }
        good(information.activate());
        check(hasText("8 × 6 px") && hasText("pixels"),"information tab did not display the selected source metadata");
        good(layerTab.activate());
        check(hasText("Opacity"),"layer tab did not restore the editing controls");
        Widget duplicateLayer;
        for (const auto &node:draw()) if (node.id.ends_with("/editorDuplicateLayer")) duplicateLayer=take(state->find(node.id));
        good(duplicateLayer.activate());
        check(shell->documentSession()->document()->layers.size()==2 &&
              shell->documentSession()->selectedLayer.get()!="paint" && hasText("Paint copy"),
              "native layer duplicate action did not update the canvas and selection");
        Widget deleteLayer;
        for (const auto &node:draw()) if (node.id.ends_with("/editorDeleteLayer")) deleteLayer=take(state->find(node.id));
        good(deleteLayer.activate());
        check(shell->documentSession()->document()->layers.size()==1 &&
              shell->documentSession()->selectedLayer.get()=="paint",
              "native layer delete action did not update the canvas and selection");
        Widget newLayerButton;
        for (const auto &node:draw()) if (node.id.ends_with("/editorNewLayer")) newLayerButton=take(state->find(node.id));
        good(newLayerButton.activate());
        Widget newDynamicBitmap;
        for (const auto &node:draw()) if (node.id.ends_with("/newDynamicBitmap")) newDynamicBitmap=take(state->find(node.id));
        good(newDynamicBitmap.activate());
        check(shell->documentSession()->document()->layers.size()==2 &&
              iiSharedCanvas::keyframedLayerSource(shell->documentSession()->document()->layers[1]) &&
              shell->documentSession()->selectedLayer.get()!="paint",
              "native new-layer menu did not create and select a dynamic bitmap layer");
        for (const auto &node:draw()) if (node.id.ends_with("/editorDeleteLayer")) deleteLayer=take(state->find(node.id));
        good(deleteLayer.activate());
        check(shell->documentSession()->document()->layers.size()==1,"new layer could not be removed");
        Widget play;
        for (const auto &node:draw()) if (node.id.ends_with("/editorPlay")) play=take(state->find(node.id));
        good(play.activate());
        check(shell->editorWorkspace()->playText.get()=="Pause","timeline play did not reach its native model");
        Widget publish;
        for (const auto &node : draw()) if (node.id.ends_with("/editorPublish")) publish=take(state->find(node.id));
        good(publish.activate());
        check(shell->editorWorkspace()->playText.get()=="Play","timeline did not pause on editor exit");
        check(hasText("Publishing targets"),"editor did not navigate to the publish workspace");
        (void)draw(); frameExecutor->drain(); (void)events.poll();
        bool publishPreviewMounted=false;
        for (const auto &node:draw()) if (node.id.ends_with("/publishCanvasPreview") &&
            node.bounds.width>0 && node.bounds.height>0) publishPreviewMounted=true;
        check(shell->publishingWorkspace()->hasFrame.get() && shell->publishingWorkspace()->renderedFrame.get() && publishPreviewMounted,
              "publishing did not display the active canvas through the native frame presenter");
        Widget title;
        for (const auto &node : draw()) if (node.text_input && node.accessibility_name=="Title") title=take(state->find(node.id));
        good(title.focus());
        good(state->input({.kind=WidgetInputKind::text,.text="A post"}));
        check(shell->documentSession()->publicationTitle.get()=="A post","publish title did not update the shared document");
        Widget saveDraft;bool foundSaveDraft=false;
        for (const auto &node:draw()) if (node.id.ends_with("/publishSaveDraft")) {saveDraft=take(state->find(node.id));foundSaveDraft=true;}
        check(foundSaveDraft && take(saveDraft.snapshot()).enabled,"publish draft save did not use the active file service");
        dialogMode.reset();dialogCompletion={};
        good(saveDraft.activate());
        check(dialogMode==ii::gui::FileDialogMode::save && bool(dialogCompletion),
              "publish draft button did not invoke the native Save As service");
        dialogCompletion(ii::gui::FileDialogSelection{.canceled=true});
        Widget targetX,includeX,threadSwitch;std::string targetPrefix;bool foundInclude=false,foundReview=false;
        for (const auto &node:draw()) {
            if (node.id.ends_with("/publishInclude") && node.text=="X") {
                includeX=take(state->find(node.id));foundInclude=true;
                targetPrefix=node.id.substr(0,node.id.size()-std::string("publishInclude").size());
            }
            if (node.id.ends_with("/publishThread")) threadSwitch=take(state->find(node.id));
        }
        for (const auto &node:draw()) if (node.id==targetPrefix+"publishTarget") {targetX=take(state->find(node.id));foundReview=true;}
        check(foundInclude && foundReview,"X destination controls were not mounted");
        good(includeX.activate());
        check(std::get<bool>(shell->documentSession()->document()->publicationMetadata->settings.at("publish.destination.x.selected")) &&
              shell->publishingWorkspace()->platformName.get().empty(),
              "destination checkbox did not independently update IISC settings");
        good(targetX.activate());
        check(take(threadSwitch.snapshot()).enabled && take(threadSwitch.snapshot()).checked,
              "the native publication target did not restore its automatic thread setting");
        good(threadSwitch.activate());
        check(!shell->publishingWorkspace()->threaded.get() &&
              std::get<std::string>(shell->documentSession()->document()->publicationMetadata->settings.at("publish.destination.x.sequence"))=="Separate posts",
              "the native publication switch did not update the shared document");
        good(shell->navigate("editor"));
        Widget canvasTool;
        for (const auto &node:draw()) if (node.id.ends_with("/editorCanvasTool")) canvasTool=take(state->find(node.id));
        good(canvasTool.activate());
        check(shell->editorWorkspace()->canvasPanelActive.get() && hasText("Size & Resolution"),
              "Canvas tool did not mount its native inspector");
        Widget canvasWidthInput,canvasHeightInput;
        for (const auto &node:draw()) {
            if (node.id.ends_with("/canvasWidthInput")) canvasWidthInput=take(state->find(node.id));
            if (node.id.ends_with("/canvasHeightInput")) canvasHeightInput=take(state->find(node.id));
        }
        good(canvasWidthInput.focus());good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::select_all}));
        good(state->input({.kind=WidgetInputKind::text,.text="10"}));
        good(canvasHeightInput.focus());good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::select_all}));
        good(state->input({.kind=WidgetInputKind::text,.text="8"}));
        good(state->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter}));
        check(shell->documentSession()->document()->extent.width==10 && shell->documentSession()->document()->extent.height==8 &&
              shell->documentSession()->dimensions.get()=="10 × 8 px",
              "native canvas dimension controls did not update the document");
        Widget mirrorTab;
        for (const auto &node:draw()) if (node.id.ends_with("/canvasMirrorTab")) mirrorTab=take(state->find(node.id));
        good(mirrorTab.activate());
        Widget mirrorAction;
        for (const auto &node:draw()) if (node.id.ends_with("/canvasMirrorApply")) mirrorAction=take(state->find(node.id));
        good(mirrorAction.activate());
        check(iiSharedCanvas::layerProperties(shell->documentSession()->document()->layers[0]).transform.m11==-1,
              "native mirror controls did not change the actual canvas layer");
        Widget rotateTab;
        for (const auto &node:draw()) if (node.id.ends_with("/canvasRotateTab")) rotateTab=take(state->find(node.id));
        good(rotateTab.activate());
        Widget rotateAction;
        for (const auto &node:draw()) if (node.id.ends_with("/canvasRotateApply")) rotateAction=take(state->find(node.id));
        good(rotateAction.activate());
        check(shell->documentSession()->document()->extent.width==8 && shell->documentSession()->document()->extent.height==10,
              "native rotation controls did not change the canvas geometry");
        Widget save;
        for (const auto &node:draw()) if (node.id.ends_with("/editorSaveFile")) save=take(state->find(node.id));
        good(save.activate());
        check(dialogMode==ii::gui::FileDialogMode::save && bool(dialogCompletion),"unsaved canvas did not invoke Save As");
        dialogCompletion(ii::gui::FileDialogSelection{.canceled=true});
        check(shell->documentSession()->dirty.get() && fileJobs.empty(),"canceling Save As changed the document");
        good(shell->navigate("files"));
        check(hasText("Storage unavailable"),"unconnected file source did not report unavailable");
        using dreamscapes::vm::Value; using dreamscapes::vm::Array; using dreamscapes::vm::Object;
        good(shell->storageWorkspace()->setSnapshot(Value(Array{Value(Object{{"section",Value("files")},{"exists",Value(true)},
            {"key",Value("fixture")},{"name",Value("Example.iisc")},{"relativePath",Value("Example.iisc")}})})));
        check(hasText("Example.iisc"),"storage snapshot did not update the mounted view");
        Widget fileRow;
        for (const auto &node : draw()) if (node.id.ends_with("/storageRow") && node.text=="Example.iisc") fileRow=take(state->find(node.id));
        good(fileRow.activate());
        check(shell->storageWorkspace()->selectedKey.get()=="fixture" && !shell->storageWorkspace()->canOpen.get(),
              "file selection failed or unavailable open action was enabled");
        check(hasText("Selected"),"storage selection was not reflected in the view");
        good(shell->navigate("preferences"));
        check(hasText("Settings unavailable"),"unconnected preferences did not report unavailable");
        good(shell->preferencesWorkspace()->setSnapshot("General",{{"example","Example value","initial",true}}));
        check(hasText("Example value"),"preference snapshot did not update the mounted view");
        good(shell->preferencesWorkspace()->select("Storage"));
        check(hasText("Settings unavailable"),"preferences displayed values for the wrong category");
        good(shell->navigate("src/App/Views/Editor/EditorToolbar.lvui"));
        check(hasText("Editor Toolbar"),"an inventory view could not be opened by its LVUI path");
        Widget back;
        for (const auto &node : draw()) if (node.id.ends_with("applicationBack")) back=take(state->find(node.id));
        good(back.activate());
        check(hasText("Recent files"),"native Home did not return");
        check(!shell->navigate("unknown"),"an unknown route was accepted");
        using Kind=dreamscapes::home::HomeComposerCommandKind;
        good(shell->composer()->dispatch(Kind::chooseMedia,"Image"));
        good(shell->composer()->dispatch(Kind::chooseResolution,"512"));
        good(shell->composer()->dispatch(Kind::chooseCount,"3"));
        auto openAdvanced=[&] {
            Widget action;
            for (const auto &node : draw())
                if (node.id.ends_with("/homeActionDefault") && node.text=="Advanced parameters") action=take(state->find(node.id));
            good(action.activate());
        };
        openAdvanced();
        check(hasText("Create image") && dreamscapes::vm::number(dreamscapes::vm::get(shell->generation(false)->parameters(),"width"))==512 &&
              dreamscapes::vm::number(dreamscapes::vm::get(shell->generation(false)->parameters(),"outputCount"))==3,
              "Home Advanced did not carry its generation settings");
        good(shell->generation(false)->edit("width","768"));
        good(shell->navigate("home"));
        openAdvanced();
        check(dreamscapes::vm::number(dreamscapes::vm::get(shell->generation(false)->parameters(),"width"))==768,
              "reopening Advanced replaced its local draft");
        iiFileProvider::native::TemporaryDirectory temporary(std::filesystem::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temporary.isValid(),"cannot create settings widget fixture");
        const auto preferencesFile=temporary.path()/"generation-models.conf";
        dreamscapes::ModelPreferences defaults; defaults.prompts[0].prompt="stored prompt";
        std::string preferenceError;
        check(dreamscapes::writeModelPreferences(preferencesFile,defaults,preferenceError),"cannot seed settings widget fixture");
        auto settingsShell=NativeApplicationShell::create(queueReadySvg);
        ii::runtime::EventLoop settingEvents;
        std::vector<ii::runtime::Task> settingJobs;
        good(settingsShell->connectGenerationPreferences(settingEvents,
            [&](ii::runtime::Task task){settingJobs.push_back(std::move(task));return true;},preferencesFile));
        auto runSetting=[&] {check(settingJobs.size()==1,"expected one settings worker");
            auto task=std::move(settingJobs.front()); settingJobs.clear(); task(); (void)settingEvents.poll();};
        runSetting();
        good(settingsShell->setGenerationModelCatalog({{"image-a","Image A",true,false},{"image-b","Image B",true,false},
                                                        {"ltx","LTX",false,true}}));
        good(settingsShell->navigate("preferences")); good(settingsShell->preferencesWorkspace()->select("Image"));
        auto settingsState=std::make_shared<detail::WidgetState>([](ii::runtime::Task task){task();});
        good(settingsState->construct([&](WidgetBuilder &builder){return buildNativeApplicationShell(builder,{},settingsShell);}));
        good(settingsState->activate_paint_view()); good(settingsShell->setViewport({1440,868}));
        (void)take(settingsState->drawing_snapshot({1440,868}));
        Widget preferenceInput; bool inputFound=false; std::string preferenceNodes;
        for (const auto &node:take(settingsState->snapshots())) {
            if (node.id.find("preference")!=std::string::npos)
                preferenceNodes+=node.id+"="+node.text+"; ";
            if (node.text_input && node.accessibility_name=="Default prompt") {
                preferenceInput=take(settingsState->find(node.id)); inputFound=true;
            }
        }
        if (!inputFound) throw std::runtime_error("preference prompt input was not mounted: "+preferenceNodes);
        good(preferenceInput.focus());
        good(settingsState->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::select_all}));
        good(settingsState->input({.kind=WidgetInputKind::text,.text="updated in view"}));
        runSetting();
        dreamscapes::ModelPreferences saved;
        check(dreamscapes::readModelPreferences(preferencesFile,saved,preferenceError) &&
              saved.prompts[0].prompt=="updated in view" && settingsShell->composer()->snapshot.get().prompt=="updated in view",
              "preferences widget did not persist its edit or update the generation draft");
        Widget defaultModel;
        for (const auto &node:take(settingsState->snapshots())) if (node.id.ends_with("/control")) {
            auto candidate=take(settingsState->find(node.id)); auto choices=candidate.action_options();
            if (choices && choices->text=="No default") defaultModel=candidate;
        }
        good(defaultModel.focus());
        good(settingsState->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::down}));
        good(settingsState->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::down}));
        runSetting();
        check(dreamscapes::readModelPreferences(preferencesFile,saved,preferenceError) && saved.imageModel=="image-b" &&
              settingsShell->composer()->snapshot.get().modelId=="image-b",
              "the mounted default-model selector did not save the chosen model");
        settingsShell->disconnectSocietyStorage();
        check(!settingsShell->preferencesWorkspace()->fields.get()[0]->editable.get() &&
              settingsShell->preferencesWorkspace()->fields.get()[0]->value.get()=="image-b",
              "disconnecting Society left stale preference choices enabled or discarded the saved model");
        settingsShell->disconnectGenerationPreferences(); settingsState->close();
        auto elementShell=NativeApplicationShell::create(queueReadySvg);
        iiSharedCanvas::Document elementDocument;elementDocument.extent={120,100};
        iiSharedCanvas::VectorPath elementPath;
        elementPath.fill=iiSharedCanvas::SolidPaint{0xff112233};
        elementPath.commands={iiSharedCanvas::MoveTo{{10,20}},iiSharedCanvas::LineTo{{50,20}},
            iiSharedCanvas::LineTo{{50,40}},iiSharedCanvas::LineTo{{10,40}},iiSharedCanvas::ClosePath{}};
        elementDocument.assets.emplace_back(iiSharedCanvas::VectorAsset{"shape-data",{120,100},{elementPath}});
        elementDocument.layers.emplace_back(iiSharedCanvas::StaticVectorLayer{{"shape","Rectangle"},iiSharedCanvas::StaticSource{"shape-data"}});
        good(elementShell->documentSession()->attach(elementDocument,"shape.iisc"));
        good(elementShell->documentSession()->selectLayer("shape"));good(elementShell->navigate("editor"));
        auto elementState=std::make_shared<detail::WidgetState>([](ii::runtime::Task task){task();});
        good(elementState->construct([&](WidgetBuilder &builder){return buildNativeApplicationShell(builder,{},elementShell);}));
        good(elementState->activate_paint_view());good(elementShell->setViewport({1440,868}));
        (void)take(elementState->drawing_snapshot({1440,868}));
        Widget elementsTool;bool foundElements=false;
        for (const auto &node:take(elementState->snapshots())) if (node.id.ends_with("/editorElementsTool")) {
            elementsTool=take(elementState->find(node.id));foundElements=true;
        }
        check(foundElements,"Elements tool was not mounted");good(elementsTool.activate());
        (void)take(elementState->drawing_snapshot({1440,868}));
        Widget elementWidth,elementHeight,elementPanel;bool foundElementWidth=false,foundElementHeight=false,foundElementPanel=false;
        for (const auto &node:take(elementState->snapshots())) {
            if (node.id.ends_with("/elementsWidth")) {elementWidth=take(elementState->find(node.id));foundElementWidth=true;}
            if (node.id.ends_with("/elementsHeight")) {elementHeight=take(elementState->find(node.id));foundElementHeight=true;}
            if (node.id.ends_with("/elementsPanel")) {elementPanel=take(elementState->find(node.id));foundElementPanel=true;}
        }
        check(foundElementWidth && foundElementHeight && foundElementPanel && elementShell->editorWorkspace()->elementsPanelActive.get() &&
              elementShell->editorWorkspace()->elementsPanel()->elementWidth.get()==40,
              "Elements inspector did not show the selected document vector");
        const auto panelBounds=take(elementPanel.snapshot()).bounds;
        const auto widthBounds=take(elementWidth.snapshot()).bounds,heightBounds=take(elementHeight.snapshot()).bounds;
        check(widthBounds.width>0 && heightBounds.width>0 && widthBounds.x>=panelBounds.x &&
              heightBounds.x+heightBounds.width<=panelBounds.x+panelBounds.width+.01,
              "Elements dimension fields overflowed their panel");
        good(elementWidth.focus());good(elementState->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::select_all}));
        good(elementState->input({.kind=WidgetInputKind::text,.text="80"}));
        good(elementState->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter}));
        const auto elementTransform=iiSharedCanvas::layerProperties(elementShell->documentSession()->document()->layers[0]).transform;
        check(std::abs(elementTransform.m11-2)<1e-6 && std::abs(elementTransform.m22-2)<1e-6 &&
              elementShell->documentSession()->dirty.get(),
              "Elements dimension control did not modify the selected document vector");
        Widget elementFill;bool foundElementFill=false;
        for (const auto &node:take(elementState->snapshots())) if (node.id.ends_with("/elementsFill")) {
            elementFill=take(elementState->find(node.id));foundElementFill=true;
        }
        check(foundElementFill,"Elements fill field was not mounted");
        good(elementFill.focus());good(elementState->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::select_all}));
        good(elementState->input({.kind=WidgetInputKind::text,.text="#ABCDEF"}));
        good(elementState->input({.kind=WidgetInputKind::key_down,.key=WidgetKey::enter}));
        const auto *elementLayer=std::get_if<iiSharedCanvas::StaticVectorLayer>(&elementShell->documentSession()->document()->layers[0]);
        check(elementLayer && iiSharedCanvas::findVectorAsset(*elementShell->documentSession()->document(),elementLayer->content.assetId)
                    ->paths[0].fill->argb==0xffabcdef,
              "Elements fill field did not modify the actual selected vector asset");
        Widget backgroundTool;bool foundBackground=false;
        for (const auto &node:take(elementState->snapshots())) if (node.id.ends_with("/editorBackgroundTool")) {
            backgroundTool=take(elementState->find(node.id));foundBackground=true;
        }
        check(foundBackground,"Background tool was not mounted");good(backgroundTool.activate());
        (void)take(elementState->drawing_snapshot({1440,868}));
        Widget createMask;bool foundCreate=false;
        for (const auto &node:take(elementState->snapshots())) if (node.id.ends_with("/maskCreate")) {
            createMask=take(elementState->find(node.id));foundCreate=true;
        }
        check(foundCreate && elementShell->editorWorkspace()->maskPanelActive.get(),"Mask panel was not mounted");
        good(createMask.activate());(void)take(elementState->drawing_snapshot({1440,868}));
        Widget invertMask,combineMask,stackToggle;bool foundInvert=false,foundCombine=false,foundStack=false;
        for (const auto &node:take(elementState->snapshots())) {
            if (node.id.ends_with("/maskInvert")) {invertMask=take(elementState->find(node.id));foundInvert=true;}
            if (node.id.ends_with("/maskCombine")) {combineMask=take(elementState->find(node.id));foundCombine=true;}
            if (node.id.ends_with("/maskStackEnabled")) {stackToggle=take(elementState->find(node.id));foundStack=true;}
        }
        check(foundInvert && foundCombine && foundStack,"Mask controls were not mounted");
        good(invertMask.activate());
        check(iiSharedCanvas::layerProperties(elementShell->documentSession()->document()->layers[0]).appearance.maskStack[0].mask.inverted,
              "mask inversion control did not edit the document");
        good(combineMask.activate());(void)take(elementState->drawing_snapshot({1440,868}));
        Widget subtractMask;bool foundSubtract=false;
        for (const auto &node:take(elementState->snapshots())) if (node.id.ends_with("/maskCombineSubtract")) {
            subtractMask=take(elementState->find(node.id));foundSubtract=true;
        }
        check(foundSubtract,"Mask combination menu did not open");good(subtractMask.activate());good(stackToggle.activate());
        const auto &maskAppearance=iiSharedCanvas::layerProperties(elementShell->documentSession()->document()->layers[0]).appearance;
        check(maskAppearance.maskStack[0].combine==iiSharedCanvas::MaskCombineMode::Subtract && !maskAppearance.maskStackEnabled,
              "mask combination or stack controls did not edit the document");
        Widget newTimelineLayer;bool foundNewTimelineLayer=false;
        for (const auto &node:take(elementState->snapshots())) if (node.id.ends_with("/timelineNewLayer")) {
            newTimelineLayer=take(elementState->find(node.id));foundNewTimelineLayer=true;
        }
        check(foundNewTimelineLayer,"Timeline layer creation control was not mounted");
        const auto beforeTimelineMenu=elementShell->documentSession()->revision.get();
        good(newTimelineLayer.activate());(void)take(elementState->drawing_snapshot({1440,868}));
        check(elementShell->documentSession()->revision.get()==beforeTimelineMenu,
              "opening the timeline creation menu modified the document");
        Widget newAudioTrack;bool foundNewAudioTrack=false;
        for (const auto &node:take(elementState->snapshots())) if (node.id.ends_with("/timelineNewAudio")) {
            newAudioTrack=take(elementState->find(node.id));foundNewAudioTrack=true;
        }
        check(foundNewAudioTrack,"Timeline audio-track menu item did not open");
        good(newAudioTrack.activate());
        check(elementShell->documentSession()->document()->audioTracks.size()==1 &&
              elementShell->documentSession()->document()->audioTracks.front().clips.empty(),
              "Timeline audio-track menu did not create an actual empty document track");
        good(newTimelineLayer.activate());(void)take(elementState->drawing_snapshot({1440,868}));
        Widget newVectorLayer;bool foundNewVectorLayer=false;
        for (const auto &node:take(elementState->snapshots())) if (node.id.ends_with("/timelineNewStaticVector")) {
            newVectorLayer=take(elementState->find(node.id));foundNewVectorLayer=true;
        }
        check(foundNewVectorLayer,"Timeline vector-layer menu item did not reopen");
        good(newVectorLayer.activate());
        check(elementShell->documentSession()->document()->layers.size()==2 &&
              iiSharedCanvas::layerKind(elementShell->documentSession()->document()->layers.back())==
                  iiSharedCanvas::LayerKind::StaticVector &&
              elementShell->documentSession()->document()->audioTracks.size()==1,
              "Timeline layer menu did not create a vector or preserve the audio track");
        elementState->close();
        state->close();
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
