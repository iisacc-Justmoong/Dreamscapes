#pragma once

#include "App/Generation/NativeHomeReferenceInputs.h"
#include "NativeHomeComposerFactory.h"
#include "NativeHomeComposerSession.h"
#include "NativeHomeComposerWorkspaceBinding.h"
#include "NativeHomeContentSession.h"
#include "NativeHomeQueueSession.h"
#include "NativeHomeReferenceBinding.h"
#include "NativeHomeReferencePicker.h"
#include "NativeHomeWorkspace.h"
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>

class GenerationComposerModel;
class GenerationController;
namespace dreamscapes {
class WorkspaceSession;
namespace home {
struct NativeHomeProductOptions final {
    std::vector<HomeSidebarDefinition> sidebar;
    std::string initialSidebarKey;
    std::function<void(std::string)> navigate;
    HomeWorkspaceLabels workspaceLabels;
    HomeComposerLabels composerLabels;
    HomeComposerChoices choices;
    HomeComposerLayoutSpec layout{};
    HomeComposerLayoutMetrics metrics{};
    HomeRecentFilesStrings files;
    HomeSelectedFileStrings selectedFile;
    HomeRecentGenerationsStrings generations;
    HomeQueueStrings queue;
    std::string queueReadySvg, deviceName, idleStatus;
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images;
    NativeHomeReferenceBinding::Detail referenceDetail;
    ii::gui::FileDialogOptions dialog;
    NativeHomeReferencePicker::Chooser chooseFile;
    std::function<void(const ii::core::Error &)> failure;
};

// Owns the product adapters and native Home models. A window may retain source().
class NativeHomeProductSession final : public QObject {
  public:
    NativeHomeProductSession(WorkspaceSession *, GenerationComposerModel *, NativeHomeProductOptions, QObject *parent = nullptr);
    ~NativeHomeProductSession() override;
    [[nodiscard]] std::shared_ptr<HomeWorkspaceModel> workspaceModel() const { return workspaceModel_; }
    [[nodiscard]] std::shared_ptr<HomeSidebarModel> sidebar() const { return sidebar_; }
    [[nodiscard]] std::shared_ptr<HomeComposerModel> composer() const { return composer_ ? composer_->model() : nullptr; }
    [[nodiscard]] std::shared_ptr<HomeQueueModel> queue() const { return queue_ ? queue_->model() : nullptr; }
    [[nodiscard]] NativeHomeContentSession *content() const { return content_.get(); }
    [[nodiscard]] NativeHomeReferenceInputs *inputs() const { return inputs_.get(); }
    [[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> source() const { return source_; }
  private:
    void rebindGeneration();
    void updateError();
    void updateStatus();
    void updateRuntimeNames();
    void report(const ii::core::Error &);
    void retire();
    QPointer<WorkspaceSession> workspace_;
    QPointer<GenerationComposerModel> draft_;
    QPointer<GenerationController> generation_;
    QMetaObject::Connection mirrorChanged_, storageChanged_, submissionQueued_, generationDestroyed_;
    std::function<void(std::string)> navigate_;
    std::function<void(const ii::core::Error &)> failure_;
    std::string deviceName_, idleStatus_;
    std::unique_ptr<NativeHomeReferenceInputs> inputs_;
    std::unique_ptr<NativeHomeComposerSession> composer_;
    std::unique_ptr<NativeHomeReferenceBinding> references_;
    std::unique_ptr<NativeHomeReferencePicker> picker_;
    std::unique_ptr<NativeHomeContentSession> content_;
    std::unique_ptr<NativeHomeQueueSession> queue_;
    std::shared_ptr<HomeSidebarModel> sidebar_;
    std::shared_ptr<HomeComposerPanelModel> panel_;
    std::shared_ptr<HomeWorkspaceModel> workspaceModel_;
    std::shared_ptr<HomeComposerWorkspaceBinding> height_;
    std::shared_ptr<ii::gui::NativeViewSource> source_;
    ii::core::ObservableSubscription composerObservation_;
    bool active_ = true;
};
} // namespace home
} // namespace dreamscapes
