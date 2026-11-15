#pragma once
#include "App/Native/NativeDocumentSession.h"
#include <iiSocialIntegration/Review.h>
#include <iiGUI/Image.h>
#include <iiSDKCore/Core/Property/Observable.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace dreamscapes::editor {class FramePresenter;}
namespace dreamscapes::publishing {
class WorkspaceModel;
struct Target final {
    std::string id,name;
    bool official=false;
    ii::core::Observable<bool> selected{false},included{false};
    ii::core::Observable<std::string> selectionStatus;
    std::weak_ptr<WorkspaceModel> owner;
    void choose();
    void setIncluded(bool);
};
class WorkspaceModel final : public std::enable_shared_from_this<WorkspaceModel> {
  public:
    static std::shared_ptr<WorkspaceModel> create(std::shared_ptr<native::DocumentSession>);
    [[nodiscard]] ii::core::Result<void> select(std::string_view);
    [[nodiscard]] ii::core::Result<void> setTargetIncluded(std::string_view,bool);
    [[nodiscard]] ii::core::Result<void> editTitle(std::string);
    [[nodiscard]] ii::core::Result<void> editDescription(std::string);
    [[nodiscard]] ii::core::Result<void> setReviewSource(iiSocialIntegration::ReviewSource);
    [[nodiscard]] ii::core::Result<void> setThreaded(bool);
    void setDraftSaveHandler(std::function<ii::core::Result<void>()>);
    void setDraftSaveAvailable(bool);
    void setDraftMessage(std::string);
    [[nodiscard]] ii::core::Result<void> saveDraft();
    void setFramePresenter(std::shared_ptr<editor::FramePresenter>);
    void setVisible(bool);
    void previewSizeChanged(double,double);
    [[nodiscard]] const iiSocialIntegration::PublicationPlan &plan() const noexcept {return plan_;}
    std::shared_ptr<native::DocumentSession> document() const {return document_;}
    void sizeChanged(double,double,double maximumPanel);
    ii::core::Observable<std::vector<std::shared_ptr<Target>>> targets;
    ii::core::Observable<std::string> platformName,policyText,planText{"Media not prepared"},renderStatus{"Canvas renderer unavailable"},draftMessage;
    ii::core::Observable<std::uint64_t> maximumMedia{0},plannedPosts{0};
    ii::core::Observable<bool> canPublish{false},canThread{false},threaded{false},hasFrame{false},canSaveDraft{false};
    ii::core::Observable<double> width{1},height{1},contentWidth{1},panelWidth{1},previewWidth{1},previewHeight{1};
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> renderedFrame;
  private:
    explicit WorkspaceModel(std::shared_ptr<native::DocumentSession>);
    void start();
    void synchronizeMetadata();
    void rebuild();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    ii::core::ObservableSubscription documentRevision_,documentVisual_,documentGeneration_;
    ii::core::ObservableSubscription imageObserver_,statusObserver_;
    std::shared_ptr<editor::FramePresenter> framePresenter_;
    bool visible_=false;
    std::optional<iiSocialIntegration::Platform> platform_;
    std::string platformKey_,settingError_;
    std::optional<iiSocialIntegration::ReviewSource> source_;
    iiSocialIntegration::ReviewOptions options_;
    iiSocialIntegration::PublicationPlan plan_;
    std::function<ii::core::Result<void>()> saveDraft_;
    bool draftSaveAvailable_=false;
    bool synchronizing_=false,pending_=false;
};
}
