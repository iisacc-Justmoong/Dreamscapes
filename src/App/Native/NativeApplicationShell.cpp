#include "NativeApplicationShell.h"
#include "App/Generation/NativeFailurePresenter.h"
#include "App/Views/Home/NativeHomeWorkspaceView.h"
#include "App/Views/Home/NativeStorageWorkspaceView.h"
#include "App/Views/Home/NativeCanvasWorkspaceView.h"
#include "App/Views/Generation/NativeGenerationWorkspaceView.h"
#include "App/Views/Result/NativeReviewGalleryView.h"
#include "App/Views/Result/NativeGenerationReviewWorkspace.h"
#include "App/Views/Editor/NativeEditorWorkspaceView.h"
#include "App/Views/Publishing/NativePublishWorkspaceView.h"
#include "App/Views/Publishing/NativePublishMedia.h"
#include "App/Views/Preferences/NativePreferencesWorkspaceView.h"
#include "NativeAssets.h"
#include "NativeSocietyModels.h"
#include <iiGUI>
#include <iiJson>
#include <iiSDKCore>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace dreamscapes::native {
class SharedImageResolver final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    void set(std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> value) {(void)target_.set(std::move(value));}
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view source,std::uint32_t width,std::uint32_t height) const override {
        auto target=target_.get(); return target?target->image(source,width,height):nullptr;
    }
  private:
    ii::core::Observable<std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>>> target_;
};
namespace {
using namespace dreamscapes::home;
std::filesystem::path nativePath(std::string_view value) {
    std::u8string bytes; bytes.reserve(value.size());
    for (const unsigned char byte:value) bytes.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(bytes);
}
HomeComposerLabels composerLabels() {
    return {"Prompt", "Prompt", "Clear", "Negative prompt", "Done",
        "Media", "Model", "Choose model", "Aspect ratio", "Resolution", "Batch size", "Duration", "Frame rate",
        "Preset", "Load preset", "Save preset", "Name", "Save", "Cancel", "Seed", "Random", "Apply",
        "Attach reference files", "Paint canvas", "Advanced parameters", "More controls", "Generate",
        [](int count,bool video){return std::to_string(count)+(video?" videos":" images");},
        [](const auto &name){return "Remove "+name;}};
}
const std::map<std::string,std::string,std::less<>> routes{
    {"canvas","src/App/Views/Home/NewCanvasDialog.qml"},
    {"image","src/App/Views/Home/ImageGenerationWorkspace.qml"},
    {"video","src/App/Views/Video/VideoGenerationWorkspace.qml"},
    {"audio","src/App/Views/Home/AdvancedGenerate.qml"},
    {"board","src/App/Views/Home/StorageBrowser.qml"},
    {"tools","src/App/Views/Editor/CanvasEditor.qml"},
    {"files","src/App/Views/Home/StorageBrowser.qml"},
    {"assets","src/App/Views/Home/StorageBrowser.qml"},
    {"history","src/App/Views/Result/GenerationResult.qml"},
    {"review","src/App/Views/Result/GenerationResult.qml"},
    {"editor","src/App/Views/Editor/CanvasEditor.qml"},
    {"publish","src/App/Views/Publishing/PublishWorkspace.qml"},
    {"preferences","src/App/Views/Preferences/PreferencesWindow.qml"},
};
}
NativeApplicationShell::NativeApplicationShell(std::string svg) : queueSvg_(std::move(svg)) {}
std::shared_ptr<NativeApplicationShell> NativeApplicationShell::create(std::string svg) {
    auto shell=std::shared_ptr<NativeApplicationShell>(new NativeApplicationShell(std::move(svg)));
    shell->start(); return shell;
}
ii::core::Result<void> NativeApplicationShell::connectPublication(ii::runtime::EventLoop &loop,CanvasFileModel::Submit submit,
    std::map<std::string,iiSocialIntegration::Account,std::less<>> accounts,
    std::shared_ptr<iiSocialIntegration::Transport> transport) {
    try {
        auto service=publishing::PublicationService::create(loop,std::move(submit),std::move(transport));
        for (auto &[id,account]:accounts) if (auto configured=service->configure(id,std::move(account));!configured)
            return configured;
        disconnectPublication();publicationService_=std::move(service);
        publishingWorkspace_->setPublicationService(publicationService_);return {};
    } catch (...) {return ii::core::fail(ii::core::ErrorCode::failed,"Cannot initialize publishing service",std::current_exception());}
}
void NativeApplicationShell::disconnectPublication() {
    publishingWorkspace_->setPublicationService({});
    if (publicationService_) publicationService_->close();
    publicationService_.reset();
}
void NativeApplicationShell::start() {
    sidebar_=HomeSidebarModel::create({
        {"home","Home","nodeshomeFolder"}, {"home-divider","","",true},
        {"canvas","New Canvas","imagefitContent"},
        {"image","Image","unconditionalImageGeneration"}, {"video","Video","imageToVideo"},
        {"audio","Audio","volume"}, {"board","Board","pattern"}, {"tools","Tools","collection-asset"},
        {"storage-divider","","",true},
        {"files","Files","sqlFile"}, {"assets","Assets","asset-library"},
        {"history","Generation History","profileCPU"}});
    const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
    titleBar_=NativeTitleBarModel::create(false,{},[weak] {
        if (auto shell=weak.lock()) {
            if (auto preferences=shell->preferencesWorkspace()) (void)preferences->select("Account");
            (void)shell->navigate("preferences");
        }
    });
    sidebar_->setHandler([weak](std::string key){if (auto shell=weak.lock()) (void)shell->navigate(key);});
    (void)sidebar_->setSelected("home");

    localDraft_=NativeHomeLocalDraft::create();
    localDraft_->setAdvancedHandler([weak](bool video) -> ii::core::Result<void> {
        auto shell=weak.lock();
        if (!shell) return ii::core::fail(ii::core::ErrorCode::closed,"Native application is closed");
        if (auto adopted=shell->generation(video)->adoptQuickSettings(shell->composer_->snapshot.get()); !adopted) return adopted;
        return shell->navigate(video?"video":"image");
    });
    composer_=localDraft_->model();
    imageGeneration_=generation::GenerationWorkspaceModel::create(false,composer_);
    videoGeneration_=generation::GenerationWorkspaceModel::create(true,composer_);
    reviewSession_=std::make_shared<review::Session>();
    historySession_=std::make_shared<review::Session>();
    reviewQueue_=review::QueueModel::create(reviewSession_);
    generationFailure_=std::make_shared<failure::FailureDetailsModel>();
    documentSession_=DocumentSession::create();
    editorWorkspace_=editor::WorkspaceModel::create(documentSession_);
    publishingWorkspace_=publishing::WorkspaceModel::create(documentSession_);
    publishingWorkspace_->setDraftSaveHandler([weak]() -> ii::core::Result<void> {
        if (auto shell=weak.lock()) return shell->requestCanvasSave();
        return ii::core::fail(ii::core::ErrorCode::closed,"Native application is closed");
    });
    publishSaveObserver_=editorWorkspace_->canSaveFile.observe([weak](bool available,std::uint64_t) {
        if (auto shell=weak.lock()) shell->publishingWorkspace_->setDraftSaveAvailable(available);
    },true);
    publishSaveMessageObserver_=editorWorkspace_->fileMessage.observe([weak](const auto &message,std::uint64_t) {
        if (auto shell=weak.lock()) shell->publishingWorkspace_->setDraftMessage(message);
    },true);
    storageWorkspace_=storage::WorkspaceModel::create();
    preferencesWorkspace_=preferences::WorkspaceModel::create(preferences::parseCatalog(preferencesCatalog));
    if (auto result=preferencesWorkspace_->setSnapshot("About",{{"version","Version",applicationVersion,false}}); !result)
        throw std::runtime_error(result.error().message);
    editorWorkspace_->setPublishHandler([weak]() -> ii::core::Result<void> {
        if (auto value=weak.lock()) return value->navigate("publish");
        return ii::core::fail(ii::core::ErrorCode::closed,"Native application is closed");
    });
    // Figma 1756:75057 fixes the media and Generate widths. LVUI supplies the control height.
    const HomeComposerLayoutSpec layout{1000,600,400,{65,1,22,93,138,71},{327,88,144,96,120,228,152},true};
    const HomeComposerLayoutMetrics metrics{8,12,22,37,44};
    composerPanel_=createHomeComposerPanel(composer_,composerLabels(),layout,metrics);
    HomeRecentFilesStrings fileStrings;
    HomeRecentGenerationsStrings generationStrings;
    generationStrings.edit=[](std::size_t count) {return "Review ("+std::to_string(count)+")";};
    HomeQueueStrings queueStrings;
    queueStrings.ready="Generation unavailable";
    recentFiles_=HomeRecentFilesModel::create(std::move(fileStrings));
    selectedFile_=HomeSelectedFileModel::create();
    (void)recentFiles_->setAvailabilityMessage("Storage unavailable");
    homeQueue_=std::make_shared<HomeQueueModel>(std::move(queueStrings));
    recentGenerations_=HomeRecentGenerationsModel::create(std::move(generationStrings));
    (void)recentGenerations_->setAvailabilityMessage("Generation history unavailable");
    sharedImages_=std::make_shared<SharedImageResolver>();
    recentFiles_->setHandlers({.select=[weak](std::string key) {if (auto shell=weak.lock()) shell->selectRecentFile(key);},
        .open=[weak](std::string,vm::Value entry) {if (auto shell=weak.lock()) shell->openRecentCanvas(entry);},
        .browse=[weak] {if (auto shell=weak.lock()) (void)shell->navigate("files");}});
    recentGenerations_->setHandlers({.open=[weak](std::string,vm::Value entry) {
            if (auto shell=weak.lock()) shell->openRecentHistory(entry);
        },.edit=[weak](std::vector<std::string> tokens) {
            if (auto shell=weak.lock()) shell->reviewRecentHistory(tokens);
        },.browse=[weak] {if (auto shell=weak.lock()) (void)shell->navigate("history");}});
    auto sources=homeWorkspaceSources({sidebar_,homeComposerPanelSource(composerPanel_),recentFiles_,selectedFile_,homeQueue_,recentGenerations_,sharedImages_,queueSvg_});
    workspace_=HomeWorkspaceModel::create(std::move(sources),sidebar_,{"Workspace","","Open file","New canvas","Storage unavailable"},
        {.openFile=[weak] {
            if (auto shell=weak.lock()) if (auto result=shell->requestCanvasOpen(); !result) (void)shell->workspace_->setError(result.error().message);
        },.newCanvas=[weak] {if (auto shell=weak.lock()) (void)shell->navigate("canvas");}});
    composerHeight_=HomeComposerWorkspaceBinding::create(composerPanel_,workspace_);
    (void)workspace_->setStatus("Services unavailable");
}
void NativeApplicationShell::prepareCanvasWorkspace() {
    if (canvasWorkspace_) return;
    canvasWorkspace_=canvas::WorkspaceModel::create(canvasCatalog);
    const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
    canvasWorkspace_->setCreateHandler([weak](vm::Value specification) -> ii::core::Result<void> {
        auto shell=weak.lock();
        if (!shell) return ii::core::fail(ii::core::ErrorCode::closed,"Native application is closed");
        if (shell->documentSession_->hasDocument.get() && shell->documentSession_->dirty.get())
            return ii::core::fail(ii::core::ErrorCode::failed,"Current canvas has unsaved changes");
        iiSharedCanvas::Document document;
        document.extent={static_cast<std::int32_t>(vm::number(vm::get(specification,"pixelWidth"))),
                         static_cast<std::int32_t>(vm::number(vm::get(specification,"pixelHeight")))};
        const auto background=vm::text(vm::get(specification,"background"));
        if (background!="Transparent") {
            const double width=document.extent.width,height=document.extent.height;
            iiSharedCanvas::VectorPath path;
            path.commands={iiSharedCanvas::MoveTo{{0,0}},iiSharedCanvas::LineTo{{width,0}},
                iiSharedCanvas::LineTo{{width,height}},iiSharedCanvas::LineTo{{0,height}},iiSharedCanvas::ClosePath{}};
            path.fill=iiSharedCanvas::SolidPaint{background=="Black"?0xff000000U:0xffffffffU};
            document.assets.emplace_back(iiSharedCanvas::VectorAsset{"canvas.background",document.extent,{path}});
            document.layers.emplace_back(iiSharedCanvas::StaticVectorLayer{
                {"canvas.background.layer","Background",true,1.0,{},RasterBlendMode::SourceOver},
                iiSharedCanvas::StaticSource{"canvas.background"}});
        }
        auto name=vm::text(vm::get(specification,"name"),"New canvas");
        if (auto result=shell->documentSession_->attach(std::move(document),std::move(name),true); !result) return result;
        return shell->navigate("editor");
    });
}
ii::core::Result<void> NativeApplicationShell::navigate(const std::string &key) {
    if (license_ && license_->gateVisible.get())
        return ii::core::fail(ii::core::ErrorCode::unsupported,"A verified Dreamscapes license is required");
    if (key!="review" && generationFailure_ && generationFailure_->open.get()) generationFailure_->dismiss();
    if (key=="home") {
        if (auto result=sidebar_->setSelected(key); !result) return result;
        editorRoute_=false;publishRoute_=false;
        editorWorkspace_->setVisible(false);
        publishingWorkspace_->setVisible(false);
        historyVisible_=false; (void)hasPageStatus.set(false); (void)pageStatus.set({});
        if (recentFilesWorkspace_) (void)recentFilesWorkspace_->refresh();
        (void)atHome.set(true);
        auto changed=viewChange_; return changed ? changed() : ii::core::Result<void>{};
    }
    const auto found=routes.find(key);
    std::string source;
    if (found!=routes.end()) source=found->second;
    else {
        const auto &views=migratedViews();
        const auto match=std::ranges::find_if(views,[&](const auto &view) {return view.source==key || view.replacement==key;});
        if (match==views.end()) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Unknown native view");
        source=match->source;
    }
    std::shared_ptr<ii::gui::NativeViewSource> view;
    const bool image=source=="src/App/Views/Home/ImageGenerationWorkspace.qml";
    const bool video=source=="src/App/Views/Video/VideoGenerationWorkspace.qml";
    const bool review=key=="review";
    const bool history=source=="src/App/Views/Result/GenerationResult.qml" && !review;
    const bool filePanel=source=="src/App/Views/Editor/EditorFilePanel.qml";
    const bool layerPanel=source=="src/App/Views/Editor/EditorLayersPanel.qml";
    const bool elementsPanel=source=="src/App/Views/Editor/EditorElementsPanel.qml";
    const bool maskPanel=source=="src/App/Views/Editor/EditorBackgroundPanel.qml";
    const bool colorPanel=source=="src/App/Views/Editor/EditorBackgroundColorPanel.qml";
    const bool replacePanel=source=="src/App/Views/Editor/EditorBackgroundReplacePanel.qml";
    const bool editor=source=="src/App/Views/Editor/CanvasEditor.qml" || filePanel || layerPanel || elementsPanel || maskPanel || colorPanel || replacePanel;
    const bool publish=source=="src/App/Views/Publishing/PublishWorkspace.qml";
    const bool storage=source=="src/App/Views/Home/StorageBrowser.qml" && key!="board";
    const bool preferences=source=="src/App/Views/Preferences/PreferencesWindow.qml";
    const bool canvas=source=="src/App/Views/Home/NewCanvasDialog.qml";
    if (image || video) {
        if (modelCatalogWorkspace_) (void)modelCatalogWorkspace_->refresh();
        const bool matchingMedia = composer_->snapshot.get().video == video;
        if (!matchingMedia) {
            if (auto changed=composer_->dispatch(HomeComposerCommandKind::chooseMedia,video?"Video":"Image"); !changed) return changed;
        }
        auto model=video?videoGeneration_:imageGeneration_;
        if (atHome.get() && matchingMedia) (void)model->adoptQuickSettings(composer_->snapshot.get());
        view=ii::gui::compiled_view_source([model](ii::gui::WidgetBuilder &builder) {
            return generation::buildGenerationWorkspace(builder,{},model);
        });
    } else if (canvas) {
        try {prepareCanvasWorkspace();}
        catch (...) {return ii::core::fail(ii::core::ErrorCode::failed,"Canvas catalogue could not be loaded",std::current_exception());}
        auto model=canvasWorkspace_;
        view=ii::gui::compiled_view_source([model](ii::gui::WidgetBuilder &builder) {
            return canvas::buildWorkspace(builder,{},model);
        });
    } else if (preferences) {
        auto model=preferencesWorkspace_;
        view=ii::gui::compiled_view_source([model](ii::gui::WidgetBuilder &builder) {
            return preferences::buildWorkspace(builder,{},model);
        });
    } else if (storage) {
        auto model=storageWorkspace_;
        const bool sameSection=model->heading.get()==(key=="assets"?"Assets":"Files");
        if (auto changed=model->setSection(key=="assets"?"asset-library":"files"); !changed) return changed;
        if (societyStorage_ && sameSection) if (auto refreshed=model->refresh(); !refreshed) return refreshed;
        view=ii::gui::compiled_view_source([model](ii::gui::WidgetBuilder &builder) {
            return storage::buildWorkspace(builder,{},model);
        });
    } else if (publish) {
        auto model=publishingWorkspace_;
        view=ii::gui::compiled_view_source([model](ii::gui::WidgetBuilder &builder) {
            return publishing::buildWorkspace(builder,{},model);
        });
    } else if (editor) {
        auto model=editorWorkspace_;
        if ((colorPanel || replacePanel) && !model->backgroundPanel.get())
            return ii::core::fail(ii::core::ErrorCode::unsupported,"Background provider is unavailable");
        if (filePanel) model->showFilePanel();
        else if (layerPanel) model->showLayerPanel();
        else if (elementsPanel) model->showElementsPanel();
        else if (maskPanel) model->showMaskPanel();
        else if (colorPanel) model->showColorPanel();
        else if (replacePanel) model->showReplacePanel();
        view=ii::gui::compiled_view_source([model](ii::gui::WidgetBuilder &builder) {
            return editor::buildWorkspace(builder,{},model);
        });
    } else if (review) {
        const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
        auto model=review::GenerationReviewWorkspace::create(reviewSession_,reviewQueue_,sharedImages_,generationFailure_,
            [weak](const auto &gallery) {if (auto shell=weak.lock()) {
                shell->reviewGallery_=gallery;gallery->setImageResolver(shell->reviewImages_);shell->configureReviewActions(gallery);
            }},[weak] {if (auto shell=weak.lock()) (void)shell->navigate("home");});
        view=ii::gui::compiled_view_source([model](ii::gui::WidgetBuilder &builder) {
            return review::buildGenerationReviewWorkspace(builder,{},model);
        });
    } else if (history) {
        auto session=societyHistory_?historySession_:reviewSession_;
        const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
        view=ii::gui::compiled_view_source([session,weak](ii::gui::WidgetBuilder &builder) {
            return review::buildReviewGallery(builder,{},[weak](const auto &model) {
                if (auto shell=weak.lock()) {
                    shell->reviewGallery_=model;model->setImageResolver(shell->reviewImages_);shell->configureReviewActions(model);
                }
            },session);
        });
    } else {
        auto model=std::make_shared<MigratedViewModel>();
        view=ii::gui::compiled_view_source([name=std::move(source),model](ii::gui::WidgetBuilder &builder) {
            return buildMigratedView(builder,{},name,model);
        });
    }
    const auto sidebarRows=sidebar_->rows.get();
    const bool sidebarRoute=std::ranges::any_of(sidebarRows,[&](const auto &row) {return !row.divider && row.key==key;});
    if (auto result=sidebar_->setSelected(sidebarRoute?key:std::string{}); !result) return result;
    editorRoute_=editor;publishRoute_=publish;
    editorWorkspace_->setVisible(editor);
    publishingWorkspace_->setVisible(publish);
    historyVisible_=history;
    if (history && societyHistory_) {
        if (auto refreshed=societyHistory_->refresh(); !refreshed) return refreshed;
    }
    updateHistoryStatus();
    (void)page.set(std::move(view)); (void)atHome.set(false);
    auto changed=viewChange_; return changed ? changed() : ii::core::Result<void>{};
}
void NativeApplicationShell::setLaunchLicense(std::shared_ptr<LaunchLicense> value) {
    if (license_==value) return;
    licenseGateObserver_={};licenseCheckingObserver_={};licenseDecidedObserver_={};licenseGateBridge_.reset();
    auto previous=std::exchange(license_,std::move(value));
    if (previous) previous->close();
    if (!license_) {updateLicensePreferences();updateLicenseGate();return;}
    const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
    const std::weak_ptr<LaunchLicense> source=license_;
    licenseGateObserver_=license_->gateVisible.observe([weak,source](bool,std::uint64_t) {
        if (auto shell=weak.lock();shell && shell->license_==source.lock()) shell->updateLicenseGate();
    });
    const auto update=[weak,source](bool,std::uint64_t) {
        if (auto shell=weak.lock();shell && shell->license_==source.lock()) shell->updateLicensePreferences();
    };
    licenseCheckingObserver_=license_->checking.observe(update);
    licenseDecidedObserver_=license_->decided.observe(update);
    updateLicensePreferences();updateLicenseGate();
}
void NativeApplicationShell::updateLicensePreferences() {
    const auto session=license_;
    if (!session) {(void)preferencesWorkspace_->clearSnapshot("Account");return;}
    const bool finished=session->decided.get(),connected=finished && session->connected.get();
    const auto status=finished?(connected?(session->licensed.get()?"Licensed":"Not licensed"):"Unavailable"):
        (session->checking.get()?"Checking":"Not checked");
    const auto subject=connected && !session->subject.get().empty()?session->subject.get():std::string{"Unavailable"};
    (void)preferencesWorkspace_->setSnapshot("Account",{{"license","Dreamscapes license",status,false},
        {"connection","Society connection",connected?"Connected":"Unavailable",false},
        {"account","Account",subject,false}});
}
void NativeApplicationShell::updateLicenseGate() {
    const bool blocked=license_ && license_->gateVisible.get();
    editorWorkspace_->setVisible(!blocked && editorRoute_);
    publishingWorkspace_->setVisible(!blocked && publishRoute_);
    auto changed=licenseGateChange_;
    if (changed) if (auto result=changed(); !result && workspace_) (void)workspace_->setError(result.error().message);
}
ii::core::Result<void> NativeApplicationShell::connectGenerationQueue(ii::runtime::EventLoop &loop,
    generation::Queue::Submit submit,generation::QueueRunner runner,bool image,bool video) {
    if (!submit || !runner || (!image && !video))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Generation service requires a worker, provider and supported media");
    try {
        disconnectGenerationQueue();
        generationQueue_=generation::Queue::create(loop,std::move(submit),std::move(runner));
        generationReview_=review::GenerationReviewBridge::create(generationQueue_,reviewSession_,reviewQueue_,homeQueue_);
        imageGenerationAvailable_=image;videoGenerationAvailable_=video;
        const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
        generationOutputObserver_=generationQueue_->outputs.observe([weak](const auto &,std::uint64_t) {
            if (auto shell=weak.lock();shell && shell->societyHistory_)
                (void)shell->societyHistory_->refresh(shell->historyVisible_?0:20);
        });
        reviewQueue_->setFailureHandler([weak](const std::string &id) {if (auto shell=weak.lock()) shell->showGenerationFailure(id);});
        const auto handler=[weak](bool video,vm::Value values) -> ii::core::Result<void> {
            if (auto shell=weak.lock()) return shell->submitGeneration(video,std::move(values));
            return ii::core::fail(ii::core::ErrorCode::closed,"Native application is closed");
        };
        if (image) imageGeneration_->setSubmitHandler([handler](vm::Value values){return handler(false,std::move(values));});
        if (video) videoGeneration_->setSubmitHandler([handler](vm::Value values){return handler(true,std::move(values));});
        localDraft_->setSubmitHandler([handler](const HomeComposerSnapshot &state) {
            vm::Object values{{"model",vm::Value(state.modelId)},{"prompt",vm::Value(state.prompt)},
                {"negativePrompt",vm::Value(state.negativePrompt)},{"width",vm::Value(state.width)},
                {"height",vm::Value(state.height)},{"outputCount",vm::Value(state.outputCount)},
                {"seed",vm::Value(static_cast<std::int64_t>(state.seed))},{"quick",vm::Value(true)}};
            if (state.video) {values["duration"]=vm::Value(state.videoDuration);values["fps"]=vm::Value(state.videoFps);}
            return handler(state.video,vm::Value(std::move(values)));
        },image,video);
        homeQueue_->setOpenQueueHandler([weak] {if (auto shell=weak.lock()) (void)shell->navigate("review");});
        return {};
    } catch (...) {
        disconnectGenerationQueue();
        return ii::core::fail(ii::core::ErrorCode::failed,"Cannot connect generation service",std::current_exception());
    }
}
void NativeApplicationShell::disconnectGenerationQueue() {
    generationOutputObserver_={};
    imageGenerationAvailable_=false;videoGenerationAvailable_=false;
    if (localDraft_) localDraft_->setSubmitHandler({},false,false);
    if (imageGeneration_) imageGeneration_->setSubmitHandler({});
    if (videoGeneration_) videoGeneration_->setSubmitHandler({});
    if (homeQueue_) homeQueue_->setOpenQueueHandler({});
    if (reviewQueue_ && reviewQueue_->active()) reviewQueue_->setFailureHandler({});
    if (generationFailure_ && generationFailure_->open.get()) generationFailure_->dismiss();
    if (generationReview_) generationReview_->close();
    generationReview_.reset();
    if (generationQueue_) generationQueue_->close();
    generationQueue_.reset();
    if (homeQueue_) (void)homeQueue_->setSnapshot({vm::Value(vm::Array{}),"Generation unavailable",{},"Automatic"});
}
void NativeApplicationShell::showGenerationFailure(const std::string &id) {
    if (!generationQueue_ || !generationFailure_) return;
    const auto &jobs=generationQueue_->jobs.get();
    const auto found=std::ranges::find_if(jobs,[&](const auto &job){return job->id==id;});
    if (found==jobs.end()) return;
    const auto job=*found;
    const bool canceled=job->state.get()==generation::QueueState::Canceled && !job->error.get().empty();
    if (job->state.get()!=generation::QueueState::Failed && !canceled) return;
    failure::NativeFailurePresenter presenter(generationFailure_);
    if (auto result=presenter.open(*job);!result) {
        generationFailure_->setReportFailure(result.error().message);return;
    }
}
ii::core::Result<void> NativeApplicationShell::submitGeneration(bool video,vm::Value parameters) {
    if (!generationQueue_ || !(video?videoGenerationAvailable_:imageGenerationAvailable_))
        return ii::core::fail(ii::core::ErrorCode::unsupported,"Generation service is unavailable");
    if (license_ && license_->gateVisible.get())
        return ii::core::fail(ii::core::ErrorCode::unsupported,"A verified Dreamscapes license is required");
    const auto model=vm::text(vm::get(parameters,"model"));
    if (model.empty() || vm::trim(vm::text(vm::get(parameters,"prompt"))).empty())
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Choose a model and enter a prompt");
    const auto &draft=composer_->snapshot.get();
    const auto modelLabel=draft.modelId==model && draft.video==video?draft.modelLabel:std::string{};
    auto queued=generationQueue_->enqueue({video?"Video":"Image",model,std::move(parameters),modelLabel,localDraft_->modelSource(model)});
    if (!queued) return std::unexpected(queued.error());
    if ((*queued)->state.get()==generation::QueueState::Failed)
        return ii::core::fail(ii::core::ErrorCode::failed,(*queued)->error.get());
    if ((*queued)->state.get()==generation::QueueState::Canceled)
        return ii::core::fail(ii::core::ErrorCode::cancelled,"Generation was canceled");
    if (auto shown=navigate("review");!shown && workspace_) (void)workspace_->setError(shown.error().message);
    return {};
}
ii::core::Result<void> NativeApplicationShell::connectFrameRenderer(ii::runtime::EventLoop &loop,
    std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor,std::filesystem::path publicationStagingRoot) {
    if (!executor) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Canvas renderer requires an executor");
    try {
        auto presenter=editor::FramePresenter::create(documentSession_,loop,executor);
        auto publishPresenter=editor::FramePresenter::create(documentSession_,loop,executor);
        auto publicationMedia=publishing::PublicationMediaModel::create(documentSession_,loop,executor,std::move(publicationStagingRoot));
        auto histogram=editor::HistogramModel::create(loop,executor);
        auto background=editor::BackgroundPanelModel::create(documentSession_,loop,executor,backgroundCatalog);
        auto audio=editor::AudioPanelModel::create(documentSession_,loop,executor);
        auto light=editor::LightPanelModel::create(documentSession_,loop,executor,toolCatalog);
        auto brush=editor::BrushToolModel::create(documentSession_,loop,executor);
        const auto brushSpec=editor::brushPanelSpec();
        iiSharedCanvas::BitmapBrush settings;settings.size=brushSpec.size.initial;
        settings.hardness=brushSpec.hardness.initial/100;settings.opacity=brushSpec.opacity.initial/100;settings.flow=brushSpec.flow.initial/100;
        if (auto configured=brush->setBrush(settings);!configured) return configured;
        auto playback=editor::TimelinePlayback::create(documentSession_,loop);
        disconnectTimelineAudio();
        editorWorkspace_->setHistogram(histogram);
        editorWorkspace_->setBackgroundPanel(background);
        editorWorkspace_->setAudioPanel(audio);
        editorWorkspace_->setLightPanel(light);
        editorWorkspace_->setBrushPanel(brush);
        editorWorkspace_->setFramePresenter(presenter);
        publishingWorkspace_->setFramePresenter(publishPresenter);
        publishingWorkspace_->setMediaModel(publicationMedia);
        editorWorkspace_->setPlayback(playback);
        if (framePresenter_) framePresenter_->close();
        if (publishFramePresenter_) publishFramePresenter_->close();
        if (publicationMedia_) publicationMedia_->close();
        if (histogram_) histogram_->close();
        if (backgroundPanel_) backgroundPanel_->close();
        if (audioPanel_) audioPanel_->close();
        if (lightPanel_) lightPanel_->close();
        if (brushPanel_) brushPanel_->close();
        if (playback_) playback_->close();
        framePresenter_=std::move(presenter);
        publishFramePresenter_=std::move(publishPresenter);
        publicationMedia_=std::move(publicationMedia);
        histogram_=std::move(histogram);
        backgroundPanel_=std::move(background);
        audioPanel_=std::move(audio);
        lightPanel_=std::move(light);
        brushPanel_=std::move(brush);
        playback_=std::move(playback);
        updateAudioFileBindings();
        updateBackgroundFileBindings();
        return {};
    } catch (...) {return ii::core::fail(ii::core::ErrorCode::failed,"Cannot connect canvas renderer",std::current_exception());}
}
ii::core::Result<void> NativeApplicationShell::connectTimelineAudio(ii::runtime::EventLoop &loop,
    editor::TimelineAudio::Submit submit,editor::TimelineAudioRunner runner) {
    if (!playback_ || !submit) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Timeline playback and audio worker are required");
    try {
        auto audio=editor::TimelineAudio::create(documentSession_,playback_,loop,std::move(submit),std::move(runner));
        disconnectTimelineAudio();timelineAudio_=std::move(audio);
        const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
        timelineAudioStatus_=timelineAudio_->status.observe([weak](const auto &message,std::uint64_t) {
            if (auto shell=weak.lock()) (void)shell->editorWorkspace_->timelinePanel()->status.set(message);
        },true);
        return {};
    } catch (...) {return ii::core::fail(ii::core::ErrorCode::failed,"Cannot connect timeline audio",std::current_exception());}
}
void NativeApplicationShell::disconnectTimelineAudio() {
    timelineAudioStatus_={};if (timelineAudio_) timelineAudio_->close();timelineAudio_.reset();
}
void NativeApplicationShell::disconnectFrameRenderer() {
    disconnectTimelineAudio();
    editorWorkspace_->setHistogram({});
    editorWorkspace_->setBackgroundPanel({});
    editorWorkspace_->setAudioPanel({});
    editorWorkspace_->setLightPanel({});
    editorWorkspace_->setBrushPanel({});
    editorWorkspace_->setFramePresenter({});
    publishingWorkspace_->setFramePresenter({});
    publishingWorkspace_->setMediaModel({});
    editorWorkspace_->setPlayback({});
    if (framePresenter_) framePresenter_->close();
    if (publishFramePresenter_) publishFramePresenter_->close();
    if (publicationMedia_) publicationMedia_->close();
    if (histogram_) histogram_->close();
    if (backgroundPanel_) backgroundPanel_->close();
    if (audioPanel_) audioPanel_->close();
    if (lightPanel_) lightPanel_->close();
    if (brushPanel_) brushPanel_->close();
    if (playback_) playback_->close();
    framePresenter_.reset();
    publishFramePresenter_.reset();
    publicationMedia_.reset();
    histogram_.reset();
    backgroundPanel_.reset();
    audioPanel_.reset();
    lightPanel_.reset();
    brushPanel_.reset();
    playback_.reset();
}
ii::core::Result<void> NativeApplicationShell::connectCanvasFiles(ii::runtime::EventLoop &loop,CanvasFileModel::Submit submit) {
    if (!submit) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Canvas file worker is required");
    try {
        auto model=CanvasFileModel::create(documentSession_,loop,submit);
        auto reviewSave=ReviewFileSave::create(loop,std::move(submit));
        reviewSave->setPicker(canvasFilePicker_);
        const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
        model->setOpenedHandler([weak] {
            if (auto shell=weak.lock()) if (auto result=shell->navigate("editor"); !result)
                if (shell->canvasFiles_) shell->canvasFiles_->reportError(result.error().message);
        });
        if (canvasFiles_) canvasFiles_->close();
        if (reviewFileSave_) reviewFileSave_->close();
        canvasFiles_=std::move(model);
        reviewFileSave_=std::move(reviewSave);
        reviewSaveStatusObserver_=reviewFileSave_->status.observe([weak](const auto &,std::uint64_t) {
            if (auto shell=weak.lock()) shell->updateHistoryStatus();
        });
        reviewSaveBusyObserver_=reviewFileSave_->busy.observe([weak](bool,std::uint64_t) {
            if (auto shell=weak.lock()) if (auto gallery=shell->reviewGallery_.lock();gallery && gallery->active())
                shell->configureReviewActions(gallery);
        });
        canvasFileErrorObserver_=canvasFiles_->error.observe([weak](const auto &message,std::uint64_t) {
            if (auto shell=weak.lock()) {
                if (shell->workspace_) (void)shell->workspace_->setError(message);
                shell->updateHistoryStatus();
            }
        });
        storageWorkspace_->setOpenHandler([weak](vm::Value entry) {
            if (auto shell=weak.lock()) shell->openRecentCanvas(entry);
        },[](const vm::Value &entry) {
            const auto path=vm::text(vm::get(entry,"nativePath"));
            return !path.empty() && supportsCanvasInput(nativePath(path));
        });
        updateCanvasFileBindings();
        if (auto gallery=reviewGallery_.lock();gallery && gallery->active()) configureReviewActions(gallery);
        selectRecentFile(recentFiles_->selectedKey.get());
        return {};
    } catch (...) {return ii::core::fail(ii::core::ErrorCode::failed,"Cannot connect canvas file services",std::current_exception());}
}
void NativeApplicationShell::disconnectCanvasFiles() {
    if (auto gallery=reviewGallery_.lock();gallery && gallery->active()) {gallery->setEditHandler({});gallery->setSaveFileHandler({});}
    canvasFilePicker_={};
    canvasDialogPending_=false;
    canvasFileErrorObserver_={};
    reviewSaveStatusObserver_={};reviewSaveBusyObserver_={};
    if (reviewFileSave_) reviewFileSave_->close();
    reviewFileSave_.reset();
    storageWorkspace_->setOpenHandler({});
    editorWorkspace_->setFileModel({},{});
    if (selectedFile_) selectedFile_->setOpenHandler({});
    if (canvasFiles_) canvasFiles_->close();
    canvasFiles_.reset();
    updateHistoryStatus();
}
ii::core::Result<void> NativeApplicationShell::connectSocietyStorage(ii::runtime::EventLoop &loop,
    CanvasFileModel::Submit submit,std::filesystem::path selection,std::filesystem::path explicitRoot) {
    if (selection.empty())
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Society storage selection is required");
    return connectSocietyStorage(loop,std::move(submit),localSocietyStorage(std::move(selection),std::move(explicitRoot)));
}

// Each view keeps its existing worker and refresh lifecycle. Only the source
// of its directory inventory and file activation is replaced.
ii::core::Result<void> NativeApplicationShell::connectSocietyStorage(ii::runtime::EventLoop &loop,
    CanvasFileModel::Submit submit,SocietyStorageAccess access) {
    if (!submit || !access.read)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Society storage worker and provider are required");
    try {
        disconnectSocietyStorage();
        auto model=SocietyStorageModel::create(storageWorkspace_,loop,submit,access.read);
        auto recentWorkspace=storage::WorkspaceModel::create();
        auto recent=SocietyStorageModel::create(recentWorkspace,loop,submit,
            [read=access.read](const SocietyStorageQuery &,SocietyStorageModel::Canceled canceled) {
                return read({"files","","",SocietyListingKind::recent,20},std::move(canceled));
            });
        auto modelWorkspace=storage::WorkspaceModel::create();
        auto modelCatalog=SocietyStorageModel::create(modelWorkspace,loop,submit,
            [read=access.read](const SocietyStorageQuery &,SocietyStorageModel::Canceled canceled) {
                return browseSocietyModels(read,std::move(canceled));
            });
        auto history=SocietyHistoryModel::create(historySession_,loop,std::move(submit),std::move(access.read));
        societyMaterialize_=std::move(access.materialize);
        const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
        model->setLocationHandler([weak](std::optional<std::filesystem::path> path) {
            if (auto shell=weak.lock()) {
                if (!path) {(void)shell->preferencesWorkspace_->clearSnapshot("Storage"); return;}
                const auto value=path->u8string();
                (void)shell->preferencesWorkspace_->setSnapshot("Storage",{{"societyLocation","Society location",
                    std::string(value.begin(),value.end()),false}});
            }
        });
        societyStorage_=std::move(model);
        modelCatalogWorkspace_=std::move(modelWorkspace);modelCatalogProvider_=std::move(modelCatalog);
        modelCatalogProvider_->setListingHandler([weak](const auto &listing) {
            if (auto shell=weak.lock()) {
                std::vector<home::GenerationModelOption> options;
                if (listing) for (const auto &entry:vm::array(listing->aliases))
                    options.push_back({vm::text(vm::get(entry,"id")),vm::text(vm::get(entry,"name")),
                        vm::boolean(vm::get(entry,"image")),vm::boolean(vm::get(entry,"video")),vm::get(entry,"source")});
                const auto error=listing?std::string{}:std::string("Model catalogue unavailable: ")+listing.error().message;
                if (auto changed=shell->setGenerationModelCatalog(std::move(options),error); !changed && shell->workspace_)
                    (void)shell->workspace_->setError(changed.error().message);
            }
        });
        recentFilesWorkspace_=std::move(recentWorkspace);recentFilesProvider_=std::move(recent);
        recentFilesProvider_->setListingHandler([weak](const auto &listing) {
            if (auto shell=weak.lock()) {
                shell->recentFileEntries_.clear();
                if (listing) for (const auto &entry:vm::array(listing->aliases)) {
                    auto file=vm::object(entry);
                    const auto name=vm::lower(vm::text(vm::get(entry,"name")));
                    const auto dot=name.rfind('.');const auto extension=dot==name.npos?std::string{}:name.substr(dot);
                    const bool image=extension==".png" || extension==".jpg" || extension==".jpeg" || extension==".webp" ||
                        extension==".bmp" || extension==".gif" || extension==".tif" || extension==".tiff" || extension==".avif" || extension==".heic" || extension==".heif";
                    const auto path=vm::text(vm::get(entry,"nativePath"));
                    file["path"]=vm::Value(path);file["isImage"]=vm::Value(image);
                    if (image || extension==".iisc") {
                        auto url=societyFileUrl(path);
                        if (url.size()<8000) file["thumbnailSource"]=vm::Value(url+"?v="+vm::text(vm::get(entry,"sourceRevision")));
                    }
                    shell->recentFileEntries_.emplace_back(std::move(file));
                }
                shell->updateRecentFiles();
            }
        });
        recentFilesStatusObserver_=recentFilesWorkspace_->status.observe([weak](const auto &,std::uint64_t) {
            if (auto shell=weak.lock()) shell->updateRecentFiles();
        });
        societyHistory_=std::move(history);
        societyHistory_->setSnapshotHandler([weak](const auto &entries) {
            if (auto shell=weak.lock()) {
                shell->recentHistory_.assign(entries.begin(),entries.begin()+std::min<std::size_t>(20,entries.size()));
                shell->updateRecentHistory();
            }
        });
        historyStatusObserver_=societyHistory_->status.observe([weak](const auto &,std::uint64_t) {
            if (auto shell=weak.lock()) {shell->updateHistoryStatus();shell->updateRecentHistory();}
        });
        if (auto refreshed=storageWorkspace_->refresh(); !refreshed) {disconnectSocietyStorage(); return refreshed;}
        if (auto refreshed=recentFilesWorkspace_->refresh(); !refreshed) {disconnectSocietyStorage(); return refreshed;}
        if (auto refreshed=modelCatalogWorkspace_->refresh(); !refreshed) {disconnectSocietyStorage(); return refreshed;}
        if (historyVisible_) return navigate("history");
        if (auto refreshed=societyHistory_->refresh(20); !refreshed) {disconnectSocietyStorage(); return refreshed;}
        return {};
    } catch (...) {return ii::core::fail(ii::core::ErrorCode::failed,"Cannot connect Society storage",std::current_exception());}
}
void NativeApplicationShell::disconnectSocietyStorage() {
    societyMaterialize_={};
    if (modelCatalogProvider_) modelCatalogProvider_->close();
    modelCatalogProvider_.reset();modelCatalogWorkspace_.reset();
    if (localDraft_ && preferencesWorkspace_) (void)setGenerationModelCatalog({},"Model catalogue unavailable");
    recentFilesStatusObserver_={};
    if (recentFilesProvider_) recentFilesProvider_->close();
    recentFilesProvider_.reset();recentFilesWorkspace_.reset();recentFileEntries_.clear();
    historyStatusObserver_={};
    if (societyHistory_) societyHistory_->close();
    societyHistory_.reset();
    recentHistory_.clear();
    if (societyStorage_) societyStorage_->close();
    societyStorage_.reset();
    if (preferencesWorkspace_) (void)preferencesWorkspace_->clearSnapshot("Storage");
    updateHistoryStatus();
    updateRecentHistory();
    updateRecentFiles();
}
void NativeApplicationShell::updateRecentFiles() {
    if (!recentFiles_) return;
    const bool loading=recentFilesWorkspace_ && recentFilesWorkspace_->loading.get();
    const auto unavailable=!recentFilesWorkspace_?std::string("Storage unavailable"):
        (!loading && !recentFilesWorkspace_->available.get()?recentFilesWorkspace_->status.get():std::string{});
    if (!loading && !unavailable.empty()) recentFileEntries_.clear();
    std::vector<std::string> previews;previews.reserve(recentFileEntries_.size());
    for (const auto &entry:recentFileEntries_) previews.push_back(vm::text(vm::get(entry,"thumbnailSource")));
    if (auto changed=recentFiles_->setAvailabilityMessage(unavailable); !changed && workspace_)
        (void)workspace_->setError(changed.error().message);
    if (auto changed=recentFiles_->setFiles(vm::Value(vm::Array(recentFileEntries_)),std::move(previews),loading); !changed && workspace_)
        (void)workspace_->setError(changed.error().message);
    selectRecentFile(recentFiles_->selectedKey.get());
}
void NativeApplicationShell::selectRecentFile(const std::string &key) {
    if (!selectedFile_) return;
    if (updatingRecentSelection_) {pendingRecentSelection_=key;return;}
    const auto keepAlive=shared_from_this();
    struct Reset {bool &value;~Reset(){value=false;}} reset{updatingRecentSelection_};
    updatingRecentSelection_=true;
    auto requested=key;
    for (int attempt=0;attempt<32;++attempt) {
        pendingRecentSelection_.reset();
        const auto found=std::ranges::find_if(recentFileEntries_,[&](const auto &entry) {return vm::text(vm::get(entry,"key"))==requested;});
        const auto entry=found==recentFileEntries_.end()?vm::Value(vm::Object{}):*found;
        const auto path=vm::text(vm::get(entry,"nativePath"));
        const bool canOpen=canvasFiles_ && !path.empty() && supportsCanvasInput(nativePath(path));
        const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
        try {selectedFile_->setOpenHandler(canOpen?std::function<void()>([weak,entry] {if (auto shell=weak.lock()) shell->openRecentCanvas(entry);}):std::function<void()>{});}
        catch (const std::exception &error) {if (workspace_) (void)workspace_->setError(error.what());return;}
        if (!pendingRecentSelection_) {
            if (auto result=selectedFile_->setFile(entry,vm::text(vm::get(entry,"thumbnailSource"))); !result && workspace_)
                (void)workspace_->setError(result.error().message);
        }
        if (!pendingRecentSelection_) return;
        requested=std::move(*pendingRecentSelection_);
    }
    pendingRecentSelection_.reset();
    if (workspace_) (void)workspace_->setError("Recent file selection did not settle");
}
void NativeApplicationShell::openRecentCanvas(const vm::Value &entry) {
    const auto path=nativePath(vm::text(vm::get(entry,"nativePath"),vm::text(vm::get(entry,"path"))));
    if (!canvasFiles_ || !supportsCanvasInput(path)) {
        if (workspace_) (void)workspace_->setError("This file cannot be opened in the canvas editor");return;
    }
    // History keeps its file reference alongside the result metadata. Other
    // Society views already carry the reference on the selected row.
    const auto &reference=vm::has(entry,"societyReference")?vm::get(entry,"societyReference"):entry;
    CanvasFileModel::Resolve resolve;
    if (vm::has(reference,"referenceRoot")) {
        if (!societyMaterialize_) {canvasFiles_->reportError("Society storage is unavailable"); return;}
        resolve=[prepare=societyMaterialize_,reference](std::stop_token stop) {
            return prepare(reference,[stop]{return stop.stop_requested();});
        };
    }
    if (auto result=canvasFiles_->open(path,std::move(resolve)); !result)
        canvasFiles_->reportError(result.error().message);
}
void NativeApplicationShell::updateHistoryStatus() {
    const auto error=canvasFiles_?canvasFiles_->error.get():std::string{};
    const auto saveStatus=reviewFileSave_?reviewFileSave_->status.get():std::string{};
    const auto message=historyVisible_ ? (!error.empty()?error:!saveStatus.empty()?saveStatus:
        (societyHistory_?societyHistory_->status.get():std::string("Generation history unavailable"))) : std::string{};
    (void)pageStatus.set(message);(void)hasPageStatus.set(!message.empty());
}
void NativeApplicationShell::configureReviewActions(const std::shared_ptr<review::ReviewGalleryModel> &gallery) {
    if (!gallery || !gallery->active()) return;
    if (!canvasFiles_) {gallery->setEditHandler({});gallery->setSaveFileHandler({});return;}
    const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
    gallery->setEditHandler([weak](std::vector<vm::Value> entries) {
        auto shell=weak.lock();if (!shell || !shell->canvasFiles_ || entries.size()!=1) return;
        shell->openRecentCanvas(entries.front());
    },[weak](const std::vector<vm::Value> &entries) {
        const auto shell=weak.lock();
        if (!shell || !shell->canvasFiles_ || entries.size()!=1) return false;
        const auto path=vm::text(vm::get(entries.front(),"path"));
        return !path.empty() && supportsCanvasInput(nativePath(path));
    });
    gallery->setSaveFileHandler([weak](std::vector<vm::Value> entries) {
        auto shell=weak.lock();if (!shell || !shell->reviewFileSave_ || entries.size()!=1) return;
        if (shell->canvasFiles_) shell->canvasFiles_->reportError({});
        if (auto result=shell->reviewFileSave_->save(entries.front()); !result && shell->canvasFiles_)
            shell->canvasFiles_->reportError(result.error().message);
    },[weak](const std::vector<vm::Value> &entries) {
        const auto shell=weak.lock();
        return shell && shell->reviewFileSave_ && entries.size()==1 && shell->reviewFileSave_->canSave(entries.front());
    });
}
void NativeApplicationShell::updateRecentHistory() {
    if (!recentGenerations_) return;
    const bool loading=societyHistory_ && societyHistory_->loading.get();
    const auto unavailable=!societyHistory_?std::string("Generation history unavailable"):
        (!loading && !societyHistory_->available.get()?societyHistory_->status.get():std::string{});
    std::vector<std::string> previews;previews.reserve(recentHistory_.size());
    for (const auto &entry:recentHistory_) previews.push_back(vm::text(vm::get(entry,"thumbnailSource")));
    if (auto changed=recentGenerations_->setAvailabilityMessage(unavailable); !changed && workspace_)
        (void)workspace_->setError(changed.error().message);
    if (auto changed=recentGenerations_->setFiles(vm::Value(vm::Array(recentHistory_)),std::move(previews),loading); !changed && workspace_)
        (void)workspace_->setError(changed.error().message);
}
void NativeApplicationShell::openRecentHistory(const vm::Value &entry) {
    if (!societyHistory_) return;
    historySession_->setFilter("all");
    const auto index=historySession_->indexForKey(review::Session::keyForEntry(entry));
    if (index<0) return;
    historySession_->focusAt(index);
    if (auto result=navigate("history"); !result && workspace_) (void)workspace_->setError(result.error().message);
}
void NativeApplicationShell::reviewRecentHistory(const std::vector<std::string> &tokens) {
    if (!societyHistory_) return;
    auto state=historySession_->navigationState();state.selectedKeys.clear();
    for (const auto &token:tokens) if (const auto index=recentGenerations_->sourceIndexForToken(token); index && *index<recentHistory_.size()) {
        const auto key=review::Session::keyForEntry(recentHistory_[*index]);
        if (!key.empty()) state.selectedKeys.push_back(key);
    }
    if (state.selectedKeys.empty()) return;
    historySession_->setFilter("all");
    state.currentKey=state.anchorKey=state.selectedKeys.front();
    if (auto result=historySession_->restoreNavigation(std::move(state)); !result) {
        if (workspace_) (void)workspace_->setError(result.error().message);return;
    }
    if (auto result=navigate("history"); !result && workspace_) (void)workspace_->setError(result.error().message);
}
ii::core::Result<void> NativeApplicationShell::connectGenerationPreferences(ii::runtime::EventLoop &loop,
    CanvasFileModel::Submit submit,std::filesystem::path path) {
    if (!submit || path.empty()) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Preference worker and path are required");
    try {
        disconnectGenerationPreferences();
        generationPreferences_=preferences::GenerationPreferences::create(preferencesWorkspace_,loop,std::move(submit),std::move(path));
        generationPreferences_->setDefaultsHandler([draft=std::weak_ptr(localDraft_)](const auto &defaults) {
            if (auto value=draft.lock()) (void)value->setDefaultPrompts(defaults);
        });
        return {};
    } catch (...) {return ii::core::fail(ii::core::ErrorCode::failed,"Cannot connect generation preferences",std::current_exception());}
}
void NativeApplicationShell::disconnectGenerationPreferences() {
    if (generationPreferences_) generationPreferences_->close();
    generationPreferences_.reset();
}
void NativeApplicationShell::setCanvasFilePicker(CanvasFilePicker picker) {
    canvasFilePicker_=std::move(picker);
    if (reviewFileSave_) reviewFileSave_->setPicker(canvasFilePicker_);
    updateCanvasFileBindings();
    updateAudioFileBindings();
    updateBackgroundFileBindings();
    if (videoGeneration_) {
        const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
        if (canvasFilePicker_) videoGeneration_->setReferencePicker([weak](std::string_view key) -> ii::core::Result<void> {
            if (auto shell=weak.lock()) return shell->chooseVideoReference(key);
            return ii::core::fail(ii::core::ErrorCode::closed,"Native application is closed");
        });
        else videoGeneration_->setReferencePicker({});
    }
    if (auto gallery=reviewGallery_.lock();gallery && gallery->active()) configureReviewActions(gallery);
}
void NativeApplicationShell::updateAudioFileBindings() {
    if (!audioPanel_) return;
    if (!canvasFilePicker_) {audioPanel_->setPickHandler({});return;}
    const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
    audioPanel_->setPickHandler([weak]() -> ii::core::Result<void> {
        if (auto shell=weak.lock()) return shell->chooseAudioFile();
        return ii::core::fail(ii::core::ErrorCode::closed,"Native application is closed");
    });
}
ii::core::Result<void> NativeApplicationShell::chooseAudioFile() {
    if (!audioPanel_ || !canvasFilePicker_) return ii::core::fail(ii::core::ErrorCode::unsupported,"Audio file picker is unavailable");
    if (canvasDialogPending_ || audioDialogPending_ || backgroundDialogPending_ || videoDialogPending_ || audioPanel_->busy.get())
        return ii::core::fail(ii::core::ErrorCode::busy,"A file operation is already in progress");
    ii::gui::FileDialogOptions options;options.mode=ii::gui::FileDialogMode::open;
    options.title="Import audio";options.filters={{"PCM16 WAV",{"wav"}}};
    const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
    const std::weak_ptr<editor::AudioPanelModel> expected=audioPanel_;
    const auto generation=documentSession_->documentGeneration.get(),revision=documentSession_->revision.get();
    const auto frame=documentSession_->frame.get();const auto track=audioPanel_->selectedTrackId();
    auto panel=audioPanel_;auto picker=canvasFilePicker_;audioDialogPending_=true;
    ii::core::Result<void> result;
    try {result=picker(std::move(options),[weak,expected,generation,revision,frame,track](ii::core::Result<ii::gui::FileDialogSelection> selected) {
        auto shell=weak.lock();if (!shell) return;shell->audioDialogPending_=false;
        auto panel=expected.lock();if (!panel || shell->audioPanel_!=panel) return;
        if (!selected) {panel->reportError(selected.error().message);return;}
        if (selected->canceled) return;
        if (shell->documentSession_->documentGeneration.get()!=generation || shell->documentSession_->revision.get()!=revision ||
            shell->documentSession_->frame.get()!=frame || panel->selectedTrackId()!=track) {
            panel->reportError("The canvas or audio selection changed while the file picker was open");return;
        }
        if (selected->paths.size()!=1 || selected->paths.front().empty()) {panel->reportError("Choose one WAV file");return;}
        if (auto imported=panel->importFile(nativePath(selected->paths.front()));!imported) panel->reportError(imported.error().message);
    });} catch (...) {result=ii::core::fail(ii::core::ErrorCode::failed,"Cannot open the audio file picker",std::current_exception());}
    if (!result) {audioDialogPending_=false;if (audioPanel_==panel) panel->reportError(result.error().message);}
    return result;
}
void NativeApplicationShell::updateBackgroundFileBindings() {
    if (!backgroundPanel_) return;
    if (!canvasFilePicker_) {backgroundPanel_->setImagePickHandler({});return;}
    const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
    backgroundPanel_->setImagePickHandler([weak]() -> ii::core::Result<void> {
        if (auto shell=weak.lock()) return shell->chooseBackgroundFile();
        return ii::core::fail(ii::core::ErrorCode::closed,"Native application is closed");
    });
}
ii::core::Result<void> NativeApplicationShell::chooseBackgroundFile() {
    if (!backgroundPanel_ || !canvasFilePicker_) return ii::core::fail(ii::core::ErrorCode::unsupported,"Image picker is unavailable");
    if (canvasDialogPending_ || audioDialogPending_ || backgroundDialogPending_ || videoDialogPending_ || backgroundPanel_->busy.get())
        return ii::core::fail(ii::core::ErrorCode::busy,"A file operation is already in progress");
    ii::gui::FileDialogOptions options;options.mode=ii::gui::FileDialogMode::open;
    options.title="Choose background image or canvas";
    options.filters={{"Images and canvas",{"png","jpg","jpeg","bmp","webp","tif","tiff","tga","iisc"}}};
    const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();const std::weak_ptr<editor::BackgroundPanelModel> expected=backgroundPanel_;
    const auto generation=documentSession_->documentGeneration.get(),revision=documentSession_->revision.get();
    auto panel=backgroundPanel_;auto picker=canvasFilePicker_;backgroundDialogPending_=true;
    ii::core::Result<void> result;
    try {result=picker(std::move(options),[weak,expected,generation,revision](ii::core::Result<ii::gui::FileDialogSelection> selected) {
        auto shell=weak.lock();if (!shell) return;shell->backgroundDialogPending_=false;
        auto panel=expected.lock();if (!panel || shell->backgroundPanel_!=panel) return;
        if (!selected) {panel->reportError(selected.error().message);return;}
        if (selected->canceled) return;
        if (shell->documentSession_->documentGeneration.get()!=generation || shell->documentSession_->revision.get()!=revision ||
            !shell->editorRoute_ || !panel->replaceMode.get() || shell->editorWorkspace_->activePanel.get()!="background-color") {
            panel->reportError("The canvas or background view changed while the file picker was open");return;
        }
        if (selected->paths.size()!=1 || selected->paths.front().empty()) {panel->reportError("Choose one image or canvas file");return;}
        if (auto loaded=panel->setImagePath(nativePath(selected->paths.front()));!loaded) panel->reportError(loaded.error().message);
    });} catch (...) {result=ii::core::fail(ii::core::ErrorCode::failed,"Cannot open the image picker",std::current_exception());}
    if (!result) {backgroundDialogPending_=false;if (backgroundPanel_==panel) panel->reportError(result.error().message);}
    return result;
}
ii::core::Result<void> NativeApplicationShell::chooseVideoReference(std::string_view key) {
    if (!videoGeneration_ || !canvasFilePicker_ || (key!="firstFrame" && key!="lastFrame"))
        return ii::core::fail(ii::core::ErrorCode::unsupported,"Video image picker is unavailable");
    if (canvasDialogPending_ || audioDialogPending_ || backgroundDialogPending_ || videoDialogPending_)
        return ii::core::fail(ii::core::ErrorCode::busy,"A file operation is already in progress");
    ii::gui::FileDialogOptions options;options.mode=ii::gui::FileDialogMode::open;
    options.title=key=="firstFrame"?"Choose first frame image":"Choose last frame image";
    options.filters={{"Images",{"png","jpg","jpeg","bmp","webp","tif","tiff","tga"}}};
    const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
    const std::weak_ptr<generation::GenerationWorkspaceModel> expected=videoGeneration_;
    const auto revision=videoGeneration_->referenceRevision();const auto field=std::string(key);
    auto picker=canvasFilePicker_;videoDialogPending_=true;
    ii::core::Result<void> result;
    try {result=picker(std::move(options),[weak,expected,revision,field](ii::core::Result<ii::gui::FileDialogSelection> selected) {
        auto shell=weak.lock();if (!shell) return;shell->videoDialogPending_=false;
        auto model=expected.lock();if (!model || shell->videoGeneration_!=model) return;
        if (!selected) {(void)model->serviceStatus.set(selected.error().message);return;}
        if (selected->canceled) return;
        if (model->referenceRevision()!=revision) {
            (void)model->serviceStatus.set("Video image conditions changed while the file picker was open");return;
        }
        if (selected->paths.size()!=1 || selected->paths.front().empty()) {
            (void)model->serviceStatus.set("Choose one image file");return;
        }
        const auto &path=selected->paths.front();const auto native=ii::core::path_from_utf8(path);
        if (!native || path.find('\0')!=path.npos || !native->is_absolute() || *native!=native->lexically_normal()) {
            (void)model->serviceStatus.set("Choose an image with an absolute UTF-8 path");return;
        }
        if (auto changed=model->edit(field,path);!changed) (void)model->serviceStatus.set(changed.error().message);
        else (void)model->serviceStatus.set(shell->videoGenerationAvailable_?std::string{}:"Generation service unavailable");
    });} catch (...) {result=ii::core::fail(ii::core::ErrorCode::failed,"Cannot open the video image picker",std::current_exception());}
    if (!result) videoDialogPending_=false;
    return result;
}
void NativeApplicationShell::setReviewImageResolver(std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> value) {
    reviewImages_=std::move(value);
    if (sharedImages_) sharedImages_->set(reviewImages_);
    if (auto gallery=reviewGallery_.lock(); gallery && gallery->active()) gallery->setImageResolver(reviewImages_);
}
ii::core::Result<void> NativeApplicationShell::setGenerationModelCatalog(std::vector<home::GenerationModelOption> values,std::string error) {
    if (generationCatalogRevision_==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ii::core::ErrorCode::out_of_range,"Model catalogue revision is exhausted");
    const auto priorRevision=generationCatalogRevision_;
    std::vector<preferences::Choice> images,videos;
    for (const auto &model:values) {
        if (model.image) images.push_back({model.id,model.name});
        if (model.video) videos.push_back({model.id,model.name});
    }
    const bool available=error.empty();
    if (auto result=localDraft_->setModelCatalog(std::move(values),std::move(error)); !result) return result;
    if (generationCatalogRevision_!=priorRevision) return {};
    const auto revision=++generationCatalogRevision_;
    if (auto result=preferencesWorkspace_->setChoices("Image","model",std::move(images),available); !result) return result;
    if (generationCatalogRevision_!=revision) return {};
    return preferencesWorkspace_->setChoices("Video","model",std::move(videos),available);
}
void NativeApplicationShell::updateCanvasFileBindings() {
    if (!canvasFiles_) {editorWorkspace_->setFileModel({},{}); return;}
    const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
    editor::WorkspaceModel::FileActions actions;
    if (canvasFilePicker_) {
        actions.open=[weak]() -> ii::core::Result<void> {
            if (auto shell=weak.lock()) return shell->requestCanvasOpen();
            return ii::core::fail(ii::core::ErrorCode::closed,"Native application is closed");
        };
        actions.saveAs=[weak]() -> ii::core::Result<void> {
            if (auto shell=weak.lock()) return shell->requestCanvasSaveAs();
            return ii::core::fail(ii::core::ErrorCode::closed,"Native application is closed");
        };
    }
    actions.save=[weak]() -> ii::core::Result<void> {
        if (auto shell=weak.lock()) return shell->requestCanvasSave();
        return ii::core::fail(ii::core::ErrorCode::closed,"Native application is closed");
    };
    editorWorkspace_->setFileModel(canvasFiles_,std::move(actions));
}
ii::core::Result<void> NativeApplicationShell::chooseCanvasFile(bool save) {
    if (!canvasFiles_ || !canvasFilePicker_)
        return ii::core::fail(ii::core::ErrorCode::unsupported,"Canvas file picker is unavailable");
    if (canvasFiles_->busy.get() || canvasDialogPending_ || audioDialogPending_ || backgroundDialogPending_ || videoDialogPending_)
        return ii::core::fail(ii::core::ErrorCode::busy,"A canvas file operation is already running");
    ii::gui::FileDialogOptions options;
    options.mode=save?ii::gui::FileDialogMode::save:ii::gui::FileDialogMode::open;
    options.title=save?"Save canvas":"Open canvas";
    options.filters=save?std::vector<ii::gui::FileFilter>{{"Dreamscapes canvas",{"iisc"}}}:
        std::vector<ii::gui::FileFilter>{{"Supported files",{"iisc","png","jpg","jpeg","bmp","webp","tif","tiff","tga"}},
            {"Dreamscapes canvas",{"iisc"}},{"Images",{"png","jpg","jpeg","bmp","webp","tif","tiff","tga"}}};
    const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
    const std::weak_ptr<CanvasFileModel> expected=canvasFiles_;
    const auto generation=documentSession_->documentGeneration.get();
    auto picker=canvasFilePicker_;
    canvasDialogPending_=true;
    auto started=picker(std::move(options),[weak,expected,generation,save](ii::core::Result<ii::gui::FileDialogSelection> selected) {
        auto shell=weak.lock();
        auto model=expected.lock();
        if (!shell || !model || shell->canvasFiles_!=model) return;
        shell->canvasDialogPending_=false;
        if (!selected) {shell->canvasFiles_->reportError(selected.error().message); return;}
        if (selected->canceled) return;
        if (shell->documentSession_->documentGeneration.get()!=generation) {
            shell->canvasFiles_->reportError("Canvas changed while the file picker was open"); return;
        }
        if (selected->paths.size()!=1 || selected->paths.front().empty()) {
            shell->canvasFiles_->reportError("Choose one canvas file"); return;
        }
        auto path=nativePath(selected->paths.front());
        if (save && path.extension().empty()) path += ".iisc";
        auto result=save?shell->canvasFiles_->saveAs(std::move(path)):shell->canvasFiles_->open(std::move(path));
        if (!result) shell->canvasFiles_->reportError(result.error().message);
    });
    if (!started) {canvasDialogPending_=false; canvasFiles_->reportError(started.error().message);}
    return started;
}
ii::core::Result<void> NativeApplicationShell::requestCanvasOpen() {
    if (documentSession_->hasDocument.get() && documentSession_->dirty.get())
        return ii::core::fail(ii::core::ErrorCode::busy,"Save the current canvas before opening another");
    return chooseCanvasFile(false);
}
ii::core::Result<void> NativeApplicationShell::requestCanvasSaveAs() {
    if (!canvasFiles_ || !documentSession_->hasDocument.get())
        return ii::core::fail(ii::core::ErrorCode::closed,"No canvas document is open");
    return chooseCanvasFile(true);
}
ii::core::Result<void> NativeApplicationShell::requestCanvasSave() {
    if (!canvasFiles_) return ii::core::fail(ii::core::ErrorCode::unsupported,"Canvas file service is unavailable");
    if (canvasFiles_->busy.get()) return ii::core::fail(ii::core::ErrorCode::busy,"A canvas file operation is already running");
    if (canvasFiles_->canSave.get()) return canvasFiles_->save();
    if (canvasFiles_->canSaveAs.get() && documentSession_->dirty.get()) return requestCanvasSaveAs();
    if (documentSession_->hasDocument.get() && !documentSession_->dirty.get()) return {};
    return ii::core::fail(ii::core::ErrorCode::closed,"No canvas document is open");
}
void NativeApplicationShell::sizeChanged(double w,double h) {
    if (!std::isfinite(w) || !std::isfinite(h) || w<0 || h<0 || w>8192 || h>8192) return;
    (void)width.set(w); (void)height.set(h);
}
ii::core::Result<void> NativeApplicationShell::setViewport(ii::gui::Size size) {
    if (!std::isfinite(size.width) || !std::isfinite(size.height) || size.width<=0 || size.height<=0 ||
        size.width>8192 || size.height>8192)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Invalid native application viewport");
    if (root_.is_null()) return ii::core::fail(ii::core::ErrorCode::closed,"Native application view is not mounted");
    return root_.set_extent(size);
}
}
