#pragma once
#include "App/Native/NativeDocumentSession.h"
#include "NativePublicationService.h"
#include <iiSocialIntegration>
#include <iiGUI>
#include <iiSDKCore>
#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace dreamscapes::editor {class FramePresenter;}
namespace dreamscapes::publishing {
class WorkspaceModel;
class PublicationMediaModel;
struct PreparedPublicationMedia;
struct ReviewField final {
    std::string id,label,platform;
    bool isText=false,isChoice=false,isToggle=false;
    ii::core::Observable<std::vector<std::string>> options;
    ii::core::Observable<std::string> text,origin,status;
    ii::core::Observable<bool> checked{false},editable{false},overridden{false};
    ii::core::Observable<int> index{0};
    std::weak_ptr<WorkspaceModel> owner;
    [[nodiscard]] ii::core::Result<void> edit(std::string);
    [[nodiscard]] ii::core::Result<void> select(int);
    [[nodiscard]] ii::core::Result<void> toggle(bool);
    [[nodiscard]] ii::core::Result<void> reset();
    void edited(std::string value) {(void)edit(std::move(value));}
    void selected(int value) {(void)select(value);}
    void toggled(bool value) {(void)toggle(value);}
    void resetOverride() {(void)reset();}
  private:
    friend class WorkspaceModel;
    std::vector<std::string> choices_;
    std::uint64_t documentGeneration_=0;
};
struct Target final {
    std::string id,name;
    bool official=false;
    ii::core::Observable<bool> selected{false},included{false};
    ii::core::Observable<std::string> selectionStatus,accountStatus,resultStatus;
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
    void setPublicationService(std::shared_ptr<PublicationService>);
    [[nodiscard]] ii::core::Result<void> publish();
    [[nodiscard]] std::shared_ptr<ReviewField> reviewField(std::string_view) const;
    void closeReview();
    void setDraftSaveHandler(std::function<ii::core::Result<void>()>);
    void setDraftSaveAvailable(bool);
    void setDraftMessage(std::string);
    [[nodiscard]] ii::core::Result<void> saveDraft();
    void setFramePresenter(std::shared_ptr<editor::FramePresenter>);
    void setMediaModel(std::shared_ptr<PublicationMediaModel>);
    void setVisible(bool);
    void previewSizeChanged(double,double);
    void listSizeChanged(double,double);
    [[nodiscard]] const iiSocialIntegration::PublicationPlan &plan() const noexcept {return plan_;}
    std::shared_ptr<native::DocumentSession> document() const {return document_;}
    void sizeChanged(double,double,double maximumPanel);
    ii::core::Observable<std::vector<std::shared_ptr<Target>>> targets;
    ii::core::Observable<std::vector<std::shared_ptr<ReviewField>>> reviewFields;
    ii::core::Observable<std::string> platformName,policyText,planText{"Media not prepared"},renderStatus{"Canvas renderer unavailable"},draftMessage,publicationStatus;
    ii::core::Observable<std::uint64_t> maximumMedia{0},plannedPosts{0};
    ii::core::Observable<bool> canPublish{false},canThread{false},threaded{false},hasFrame{false},canSaveDraft{false},reviewOpen{false};
    ii::core::Observable<double> width{1},height{1},contentWidth{1},panelWidth{1},previewWidth{1},previewHeight{1};
    ii::core::Observable<double> listWidth{1},listHeight{1};
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> renderedFrame;
  private:
    friend struct ReviewField;
    friend struct Target;
    explicit WorkspaceModel(std::shared_ptr<native::DocumentSession>);
    void start();
    void synchronizeMetadata();
    void synchronizeReviewFields(const iiSharedCanvas::Document *);
    [[nodiscard]] ii::core::Result<std::vector<PublicationRequest>> publicationRequests() const;
    void synchronizePublication();
    [[nodiscard]] ii::core::Result<void> validateReviewField(const ReviewField *) const;
    void rebuild();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    ii::core::ObservableSubscription documentRevision_,documentVisual_,documentGeneration_,documentFrame_;
    ii::core::ObservableSubscription imageObserver_,statusObserver_;
    ii::core::ObservableSubscription mediaObserver_,mediaStatusObserver_;
    ii::core::ObservableSubscription publicationAccounts_,publicationBusy_,publicationStatus_,publicationOutcomes_;
    std::shared_ptr<editor::FramePresenter> framePresenter_;
    std::shared_ptr<PublicationMediaModel> mediaModel_;
    std::shared_ptr<PublicationService> publicationService_;
    std::shared_ptr<const PreparedPublicationMedia> mediaLease_;
    bool visible_=false;
    std::optional<iiSocialIntegration::Platform> platform_;
    std::string platformKey_,settingError_;
    std::optional<iiSocialIntegration::ReviewSource> source_;
    iiSocialIntegration::ReviewOptions options_;
    iiSocialIntegration::PublicationPlan plan_;
    std::function<ii::core::Result<void>()> saveDraft_;
    bool draftSaveAvailable_=false;
    bool synchronizing_=false,pending_=false,rebuilding_=false,rebuildPending_=false;
    std::uint64_t mediaEpoch_=0;
    std::string fieldsPlatform_;
    std::uint64_t fieldsGeneration_=0;
};
}
