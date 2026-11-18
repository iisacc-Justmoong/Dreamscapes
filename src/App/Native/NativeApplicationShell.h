#pragma once
#include "NativeMigratedView.h"
#include "NativeCanvasFileModel.h"
#include "NativeReviewFileSave.h"
#include "NativeSocietyStorageModel.h"
#include "NativeSocietyHistory.h"
#include "NativeLaunchLicense.h"
#include "NativeLicenseBridge.h"
#include "NativeTitleBar.h"
#include "App/Views/Result/NativeGenerationReviewBridge.h"
#include "App/Views/Failure/NativeFailureDetails.h"
#include "App/Views/Home/NativeHomeComposerFactory.h"
#include "App/Views/Home/NativeHomeComposerWorkspaceBinding.h"
#include "App/Views/Home/NativeHomeWorkspaceSources.h"
#include "App/Views/Home/NativeHomeLocalDraft.h"
#include "App/Views/Home/NativeStorageWorkspace.h"
#include "App/Views/Home/NativeCanvasWorkspace.h"
#include "App/Views/Generation/NativeGenerationWorkspace.h"
#include "App/Views/Result/NativeReviewSession.h"
#include "App/Views/Editor/NativeEditorWorkspace.h"
#include "App/Views/Editor/NativeTimelineAudio.h"
#include "App/Views/Publishing/NativePublishWorkspace.h"
#include "App/Views/Preferences/NativePreferencesWorkspace.h"
#include "App/Views/Preferences/NativeGenerationPreferences.h"
#include <iiGUI>
#include <iiSDKCore>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace dreamscapes::review {class ReviewGalleryModel;}
namespace dreamscapes::native {
class SharedImageResolver;
// A native window's presentation state; product services are deliberately not
// created until their backends have been connected to the new view.
class NativeApplicationShell final : public std::enable_shared_from_this<NativeApplicationShell> {
  public:
    static std::shared_ptr<NativeApplicationShell> create(std::string queueReadySvg);
    [[nodiscard]] ii::core::Result<void> navigate(const std::string &);
    [[nodiscard]] ii::core::Result<void> setViewport(ii::gui::Size);
    [[nodiscard]] ii::core::Result<void> connectFrameRenderer(ii::runtime::EventLoop &,
        std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>,std::filesystem::path publicationStagingRoot={});
    void disconnectFrameRenderer();
    [[nodiscard]] ii::core::Result<void> connectTimelineAudio(ii::runtime::EventLoop &,editor::TimelineAudio::Submit,
                                                              editor::TimelineAudioRunner runner={});
    void disconnectTimelineAudio();
    using CanvasFilePicker=std::function<ii::core::Result<void>(ii::gui::FileDialogOptions,ii::gui::FileDialogCompletion)>;
    [[nodiscard]] ii::core::Result<void> connectCanvasFiles(ii::runtime::EventLoop &,CanvasFileModel::Submit);
    void disconnectCanvasFiles();
    [[nodiscard]] ii::core::Result<void> connectSocietyStorage(ii::runtime::EventLoop &,CanvasFileModel::Submit,std::filesystem::path,std::filesystem::path explicitRoot={});
    void disconnectSocietyStorage();
    [[nodiscard]] ii::core::Result<void> connectGenerationPreferences(ii::runtime::EventLoop &,CanvasFileModel::Submit,std::filesystem::path);
    void disconnectGenerationPreferences();
    [[nodiscard]] ii::core::Result<void> connectGenerationQueue(ii::runtime::EventLoop &,
        generation::Queue::Submit,generation::QueueRunner,bool image,bool video);
    void disconnectGenerationQueue();
    [[nodiscard]] ii::core::Result<void> connectPublication(ii::runtime::EventLoop &,CanvasFileModel::Submit,
        std::map<std::string,iiSocialIntegration::Account,std::less<>> accounts={},
        std::shared_ptr<iiSocialIntegration::Transport> transport={});
    void disconnectPublication();
    void setLaunchLicense(std::shared_ptr<LaunchLicense>);
    std::shared_ptr<LaunchLicense> launchLicense() const {return license_;}
    void setLicenseGateBridge(std::shared_ptr<NativeLicenseBridge> value) {licenseGateBridge_=std::move(value);}
    void setCanvasFilePicker(CanvasFilePicker);
    void setReviewImageResolver(std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>>);
    [[nodiscard]] ii::core::Result<void> setGenerationModelCatalog(std::vector<home::GenerationModelOption>,std::string error={});
    [[nodiscard]] ii::core::Result<void> requestCanvasOpen();
    [[nodiscard]] ii::core::Result<void> requestCanvasSave();
    [[nodiscard]] ii::core::Result<void> requestCanvasSaveAs();
    void setRoot(ii::gui::Widget root) {root_=std::move(root);}
    void sizeChanged(double width,double height);
    void setViewChangeHandler(std::function<ii::core::Result<void>()> value) {viewChange_=std::move(value);}
    std::shared_ptr<home::HomeSidebarModel> sidebar() const {return sidebar_;}
    std::shared_ptr<home::HomeWorkspaceModel> workspace() const {return workspace_;}
    std::shared_ptr<home::HomeComposerModel> composer() const {return composer_;}
    std::shared_ptr<generation::GenerationWorkspaceModel> generation(bool video) const {return video?videoGeneration_:imageGeneration_;}
    std::shared_ptr<review::Session> reviewSession() const {return reviewSession_;}
    std::shared_ptr<generation::Queue> generationQueue() const {return generationQueue_;}
    std::shared_ptr<home::HomeQueueModel> homeQueue() const {return homeQueue_;}
    std::shared_ptr<review::QueueModel> reviewQueue() const {return reviewQueue_;}
    std::shared_ptr<failure::FailureDetailsModel> generationFailure() const {return generationFailure_;}
    std::shared_ptr<review::Session> historySession() const {return historySession_;}
    std::shared_ptr<review::ReviewGalleryModel> reviewGallery() const {return reviewGallery_.lock();}
    std::shared_ptr<home::HomeRecentGenerationsModel> recentGenerations() const {return recentGenerations_;}
    std::shared_ptr<home::HomeRecentFilesModel> recentFiles() const {return recentFiles_;}
    std::shared_ptr<home::HomeSelectedFileModel> selectedFile() const {return selectedFile_;}
    std::shared_ptr<DocumentSession> documentSession() const {return documentSession_;}
    std::shared_ptr<CanvasFileModel> canvasFiles() const {return canvasFiles_;}
    std::shared_ptr<ReviewFileSave> reviewFileSave() const {return reviewFileSave_;}
    std::shared_ptr<editor::WorkspaceModel> editorWorkspace() const {return editorWorkspace_;}
    std::shared_ptr<publishing::WorkspaceModel> publishingWorkspace() const {return publishingWorkspace_;}
    std::shared_ptr<publishing::PublicationService> publicationService() const {return publicationService_;}
    std::shared_ptr<storage::WorkspaceModel> storageWorkspace() const {return storageWorkspace_;}
    std::shared_ptr<preferences::WorkspaceModel> preferencesWorkspace() const {return preferencesWorkspace_;}
    std::shared_ptr<NativeTitleBarModel> titleBar() const {return titleBar_;}
    std::shared_ptr<canvas::WorkspaceModel> canvasWorkspace() const {return canvasWorkspace_;}
    ii::core::Observable<std::shared_ptr<ii::gui::NativeViewSource>> page;
    ii::core::Observable<bool> atHome{true};
    ii::core::Observable<bool> hasPageStatus{false};
    ii::core::Observable<std::string> pageStatus;
    ii::core::Observable<double> width{1},height{1};
  private:
    explicit NativeApplicationShell(std::string);
    void start();
    void prepareCanvasWorkspace();
    void updateHistoryStatus();
    void configureReviewActions(const std::shared_ptr<review::ReviewGalleryModel> &);
    void updateLicensePreferences();
    void updateLicenseGate();
    void updateRecentHistory();
    void openRecentHistory(const vm::Value &);
    void reviewRecentHistory(const std::vector<std::string> &);
    void updateRecentFiles();
    void selectRecentFile(const std::string &);
    void openRecentCanvas(const vm::Value &);
    void updateCanvasFileBindings();
    void updateAudioFileBindings();
    [[nodiscard]] ii::core::Result<void> chooseAudioFile();
    void updateBackgroundFileBindings();
    [[nodiscard]] ii::core::Result<void> chooseBackgroundFile();
    [[nodiscard]] ii::core::Result<void> chooseVideoReference(std::string_view);
    void showGenerationFailure(const std::string &);
    [[nodiscard]] ii::core::Result<void> submitGeneration(bool video,vm::Value parameters);
    [[nodiscard]] ii::core::Result<void> chooseCanvasFile(bool save);
    std::string queueSvg_;
    std::shared_ptr<home::HomeSidebarModel> sidebar_;
    std::shared_ptr<home::NativeHomeLocalDraft> localDraft_;
    std::shared_ptr<home::HomeComposerModel> composer_;
    std::shared_ptr<generation::GenerationWorkspaceModel> imageGeneration_,videoGeneration_;
    std::shared_ptr<review::Session> reviewSession_,historySession_;
    std::shared_ptr<generation::Queue> generationQueue_;
    ii::core::ObservableSubscription generationOutputObserver_;
    std::shared_ptr<review::QueueModel> reviewQueue_;
    std::shared_ptr<home::HomeQueueModel> homeQueue_;
    std::shared_ptr<review::GenerationReviewBridge> generationReview_;
    std::shared_ptr<failure::FailureDetailsModel> generationFailure_;
    bool imageGenerationAvailable_=false,videoGenerationAvailable_=false;
    std::weak_ptr<review::ReviewGalleryModel> reviewGallery_;
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> reviewImages_;
    std::shared_ptr<SharedImageResolver> sharedImages_;
    std::shared_ptr<home::HomeRecentGenerationsModel> recentGenerations_;
    std::vector<vm::Value> recentHistory_;
    std::shared_ptr<home::HomeRecentFilesModel> recentFiles_;
    std::shared_ptr<home::HomeSelectedFileModel> selectedFile_;
    std::vector<vm::Value> recentFileEntries_;
    bool updatingRecentSelection_=false;
    std::optional<std::string> pendingRecentSelection_;
    std::shared_ptr<DocumentSession> documentSession_;
    std::shared_ptr<CanvasFileModel> canvasFiles_;
    std::shared_ptr<ReviewFileSave> reviewFileSave_;
    CanvasFilePicker canvasFilePicker_;
    bool canvasDialogPending_=false,audioDialogPending_=false,backgroundDialogPending_=false,videoDialogPending_=false;
    ii::core::ObservableSubscription canvasFileErrorObserver_;
    ii::core::ObservableSubscription reviewSaveStatusObserver_,reviewSaveBusyObserver_;
    std::shared_ptr<editor::WorkspaceModel> editorWorkspace_;
    std::shared_ptr<editor::FramePresenter> framePresenter_;
    std::shared_ptr<editor::FramePresenter> publishFramePresenter_;
    std::shared_ptr<editor::HistogramModel> histogram_;
    std::shared_ptr<editor::BackgroundPanelModel> backgroundPanel_;
    std::shared_ptr<editor::AudioPanelModel> audioPanel_;
    std::shared_ptr<editor::LightPanelModel> lightPanel_;
    std::shared_ptr<editor::BrushToolModel> brushPanel_;
    std::shared_ptr<editor::TimelinePlayback> playback_;
    std::shared_ptr<editor::TimelineAudio> timelineAudio_;
    ii::core::ObservableSubscription timelineAudioStatus_;
    std::shared_ptr<publishing::WorkspaceModel> publishingWorkspace_;
    std::shared_ptr<publishing::PublicationMediaModel> publicationMedia_;
    std::shared_ptr<publishing::PublicationService> publicationService_;
    ii::core::ObservableSubscription publishSaveObserver_,publishSaveMessageObserver_;
    std::shared_ptr<storage::WorkspaceModel> storageWorkspace_;
    std::shared_ptr<SocietyStorageModel> societyStorage_;
    std::shared_ptr<storage::WorkspaceModel> recentFilesWorkspace_;
    std::shared_ptr<SocietyStorageModel> recentFilesProvider_;
    ii::core::ObservableSubscription recentFilesStatusObserver_;
    std::shared_ptr<storage::WorkspaceModel> modelCatalogWorkspace_;
    std::shared_ptr<SocietyStorageModel> modelCatalogProvider_;
    std::uint64_t generationCatalogRevision_=0;
    std::shared_ptr<SocietyHistoryModel> societyHistory_;
    ii::core::ObservableSubscription historyStatusObserver_;
    bool historyVisible_=false;
    std::shared_ptr<preferences::WorkspaceModel> preferencesWorkspace_;
    std::shared_ptr<NativeTitleBarModel> titleBar_;
    std::shared_ptr<preferences::GenerationPreferences> generationPreferences_;
    std::shared_ptr<LaunchLicense> license_;
    ii::core::ObservableSubscription licenseGateObserver_,licenseCheckingObserver_,licenseDecidedObserver_;
    std::shared_ptr<NativeLicenseBridge> licenseGateBridge_;
    bool editorRoute_=false,publishRoute_=false;
    std::shared_ptr<canvas::WorkspaceModel> canvasWorkspace_;
    std::shared_ptr<home::HomeComposerPanelModel> composerPanel_;
    std::shared_ptr<home::HomeWorkspaceModel> workspace_;
    std::shared_ptr<home::HomeComposerWorkspaceBinding> composerHeight_;
    std::function<ii::core::Result<void>()> viewChange_;
    ii::gui::Widget root_;
};
}
