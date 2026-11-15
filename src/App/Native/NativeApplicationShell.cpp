#include "NativeApplicationShell.h"
#include "App/Views/Home/NativeHomeWorkspaceView.h"
#include "App/Views/Home/NativeStorageWorkspaceView.h"
#include "App/Views/Home/NativeCanvasWorkspaceView.h"
#include "App/Views/Generation/NativeGenerationWorkspaceView.h"
#include "App/Views/Result/NativeReviewGalleryView.h"
#include "App/Views/Editor/NativeEditorWorkspaceView.h"
#include "App/Views/Publishing/NativePublishWorkspaceView.h"
#include "App/Views/Preferences/NativePreferencesWorkspaceView.h"
#include "NativeAssets.h"
#include "NativeSocietyModels.h"
#include <iiGUI/CompiledViewSource.h>
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
    return {"Describe the image or video to create", "Prompt", "Clear", "Negative prompt", "Done",
        "Media", "Model", "Choose model", "Aspect ratio", "Resolution", "Batch size", "Duration", "Frame rate",
        "Preset", "Load preset", "Save preset", "Name", "Save", "Cancel", "Seed", "Random", "Apply",
        "Attach images", "Paint canvas", "Advanced parameters", "More controls", "Generate",
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
void NativeApplicationShell::start() {
    sidebar_=HomeSidebarModel::create({
        {"home","Home","nodeshomeFolder"}, {"canvas","New Canvas","imagefitContent"},
        {"image","Image","unconditionalImageGeneration"}, {"video","Video","imageToVideo"},
        {"audio","Audio","volume"}, {"board","Board",""}, {"tools","Tools",""},
        {"files","Files","sqlFile"}, {"assets","Assets","asset-library"},
        {"history","Generation History","profileCPU"}, {"editor","Editor",""},
        {"publish","Publish",""}, {"preferences","Preferences",""}});
    const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
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
    // These layout values are the existing Home composition's design inputs.
    const HomeComposerLayoutSpec layout{1000,600,400,{88,1,22,93,138,145},{327,88,144,96,120,228,152}};
    const HomeComposerLayoutMetrics metrics{8,12,22,37,44};
    composerPanel_=createHomeComposerPanel(composer_,composerLabels(),layout,metrics);
    HomeRecentFilesStrings fileStrings;
    HomeRecentGenerationsStrings generationStrings;
    generationStrings.edit=[](std::size_t count) {return "Review ("+std::to_string(count)+")";};
    HomeQueueStrings queueStrings;
    queueStrings.ready="Generation unavailable"; queueStrings.detail="Generation is not connected in this view migration.";
    recentFiles_=HomeRecentFilesModel::create(std::move(fileStrings));
    selectedFile_=HomeSelectedFileModel::create();
    (void)recentFiles_->setAvailabilityMessage("Storage unavailable");
    auto queue=std::make_shared<HomeQueueModel>(std::move(queueStrings));
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
    auto sources=homeWorkspaceSources({sidebar_,homeComposerPanelSource(composerPanel_),recentFiles_,selectedFile_,queue,recentGenerations_,sharedImages_,queueSvg_});
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
    const bool history=source=="src/App/Views/Result/GenerationResult.qml";
    const bool editor=source=="src/App/Views/Editor/CanvasEditor.qml";
    const bool publish=source=="src/App/Views/Publishing/PublishWorkspace.qml";
    const bool storage=source=="src/App/Views/Home/StorageBrowser.qml" && key!="board";
    const bool preferences=source=="src/App/Views/Preferences/PreferencesWindow.qml";
    const bool canvas=source=="src/App/Views/Home/NewCanvasDialog.qml";
    if (image || video) {
        if (modelCatalogWorkspace_) (void)modelCatalogWorkspace_->refresh();
        if (composer_->snapshot.get().video!=video) {
            if (auto changed=composer_->dispatch(HomeComposerCommandKind::chooseMedia,video?"Video":"Image"); !changed) return changed;
        }
        auto model=video?videoGeneration_:imageGeneration_;
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
        view=ii::gui::compiled_view_source([model](ii::gui::WidgetBuilder &builder) {
            return editor::buildWorkspace(builder,{},model);
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
    if (auto result=sidebar_->setSelected(found==routes.end()?std::string{}:key); !result) return result;
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
    auto changed=viewChange_;
    if (changed) if (auto result=changed(); !result && workspace_) (void)workspace_->setError(result.error().message);
}
ii::core::Result<void> NativeApplicationShell::connectFrameRenderer(ii::runtime::EventLoop &loop,
    std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor) {
    if (!executor) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Canvas renderer requires an executor");
    try {
        auto presenter=editor::FramePresenter::create(documentSession_,loop,executor);
        auto publishPresenter=editor::FramePresenter::create(documentSession_,loop,std::move(executor));
        auto playback=editor::TimelinePlayback::create(documentSession_,loop);
        editorWorkspace_->setFramePresenter(presenter);
        publishingWorkspace_->setFramePresenter(publishPresenter);
        editorWorkspace_->setPlayback(playback);
        if (framePresenter_) framePresenter_->close();
        if (publishFramePresenter_) publishFramePresenter_->close();
        if (playback_) playback_->close();
        framePresenter_=std::move(presenter);
        publishFramePresenter_=std::move(publishPresenter);
        playback_=std::move(playback);
        return {};
    } catch (...) {return ii::core::fail(ii::core::ErrorCode::failed,"Cannot connect canvas renderer",std::current_exception());}
}
void NativeApplicationShell::disconnectFrameRenderer() {
    editorWorkspace_->setFramePresenter({});
    publishingWorkspace_->setFramePresenter({});
    editorWorkspace_->setPlayback({});
    if (framePresenter_) framePresenter_->close();
    if (publishFramePresenter_) publishFramePresenter_->close();
    if (playback_) playback_->close();
    framePresenter_.reset();
    publishFramePresenter_.reset();
    playback_.reset();
}
ii::core::Result<void> NativeApplicationShell::connectCanvasFiles(ii::runtime::EventLoop &loop,CanvasFileModel::Submit submit) {
    if (!submit) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Canvas file worker is required");
    try {
        auto model=CanvasFileModel::create(documentSession_,loop,std::move(submit));
        const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
        model->setOpenedHandler([weak] {
            if (auto shell=weak.lock()) if (auto result=shell->navigate("editor"); !result)
                if (shell->canvasFiles_) shell->canvasFiles_->reportError(result.error().message);
        });
        if (canvasFiles_) canvasFiles_->close();
        canvasFiles_=std::move(model);
        canvasFileErrorObserver_=canvasFiles_->error.observe([weak](const auto &message,std::uint64_t) {
            if (auto shell=weak.lock()) {
                if (shell->workspace_) (void)shell->workspace_->setError(message);
                shell->updateHistoryStatus();
            }
        });
        storageWorkspace_->setOpenHandler([weak](vm::Value entry) {
            auto shell=weak.lock(); if (!shell || !shell->canvasFiles_) return;
            const auto path=vm::text(vm::get(entry,"nativePath"));
            if (path.empty()) {shell->canvasFiles_->reportError("The selected file is not available locally"); return;}
            if (auto result=shell->canvasFiles_->open(nativePath(path)); !result)
                shell->canvasFiles_->reportError(result.error().message);
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
    if (auto gallery=reviewGallery_.lock();gallery && gallery->active()) gallery->setEditHandler({});
    canvasFilePicker_={};
    canvasDialogPending_=false;
    canvasFileErrorObserver_={};
    storageWorkspace_->setOpenHandler({});
    editorWorkspace_->setFileModel({},{});
    if (selectedFile_) selectedFile_->setOpenHandler({});
    if (canvasFiles_) canvasFiles_->close();
    canvasFiles_.reset();
}
ii::core::Result<void> NativeApplicationShell::connectSocietyStorage(ii::runtime::EventLoop &loop,
    CanvasFileModel::Submit submit,std::filesystem::path selection,std::filesystem::path explicitRoot) {
    if (!submit || selection.empty())
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Society storage worker and selection are required");
    try {
        disconnectSocietyStorage();
        auto model=SocietyStorageModel::create(storageWorkspace_,loop,submit,selection,explicitRoot);
        auto recentWorkspace=storage::WorkspaceModel::create();
        auto recent=SocietyStorageModel::create(recentWorkspace,loop,submit,selection,explicitRoot,
            [](const auto &path,const auto &,const auto &,const auto &,const auto &root,SocietyStorageModel::Canceled canceled) {
                if (canceled && canceled()) return ii::core::Result<SocietyListing>{ii::core::fail(ii::core::ErrorCode::cancelled,"Recent files scan canceled")};
                return browseSocietyRecentFiles(path,20,root);
            });
        auto modelWorkspace=storage::WorkspaceModel::create();
        auto modelCatalog=SocietyStorageModel::create(modelWorkspace,loop,submit,selection,explicitRoot,
            [](const auto &path,const auto &,const auto &,const auto &,const auto &root,SocietyStorageModel::Canceled canceled) {
                return browseSocietyModels(path,root,std::move(canceled));
            });
        auto history=SocietyHistoryModel::create(historySession_,loop,std::move(submit),std::move(selection),std::move(explicitRoot));
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
                        vm::boolean(vm::get(entry,"image")),vm::boolean(vm::get(entry,"video"))});
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
    const auto path=vm::text(vm::get(entry,"nativePath"));
    if (!canvasFiles_ || path.empty() || !supportsCanvasInput(nativePath(path))) {
        if (workspace_) (void)workspace_->setError("This file cannot be opened in the canvas editor");return;
    }
    if (auto result=canvasFiles_->open(nativePath(path)); !result) canvasFiles_->reportError(result.error().message);
}
void NativeApplicationShell::updateHistoryStatus() {
    const auto error=canvasFiles_?canvasFiles_->error.get():std::string{};
    const auto message=historyVisible_ ? (!error.empty()?error:
        (societyHistory_?societyHistory_->status.get():std::string("Generation history unavailable"))) : std::string{};
    (void)pageStatus.set(message);(void)hasPageStatus.set(!message.empty());
}
void NativeApplicationShell::configureReviewActions(const std::shared_ptr<review::ReviewGalleryModel> &gallery) {
    if (!gallery || !gallery->active()) return;
    if (!canvasFiles_) {gallery->setEditHandler({});return;}
    const std::weak_ptr<NativeApplicationShell> weak=shared_from_this();
    gallery->setEditHandler([weak](std::vector<vm::Value> entries) {
        auto shell=weak.lock();if (!shell || !shell->canvasFiles_ || entries.size()!=1) return;
        const auto path=vm::text(vm::get(entries.front(),"path"));
        if (auto result=shell->canvasFiles_->open(nativePath(path)); !result)
            shell->canvasFiles_->reportError(result.error().message);
    },[weak](const std::vector<vm::Value> &entries) {
        const auto shell=weak.lock();
        if (!shell || !shell->canvasFiles_ || entries.size()!=1) return false;
        const auto path=vm::text(vm::get(entries.front(),"path"));
        return !path.empty() && supportsCanvasInput(nativePath(path));
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
    canvasFilePicker_=std::move(picker); updateCanvasFileBindings();
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
    if (canvasFiles_->busy.get() || canvasDialogPending_)
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
