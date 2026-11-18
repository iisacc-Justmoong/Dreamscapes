#pragma once
#include "NativePublishWorkspace.h"
#include <iiGUI>

namespace dreamscapes::publishing {
[[nodiscard]] ii::core::Result<void> buildWorkspace(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<WorkspaceModel>);
}
class NativePublishWorkspaceBackend final {
  public:
    explicit NativePublishWorkspaceBackend(std::shared_ptr<dreamscapes::publishing::WorkspaceModel> value)
        : model(std::move(value)),document(model->document()),targets(model->targets),reviewFields(model->reviewFields),platformName(model->platformName),
          policyText(model->policyText),planText(model->planText),renderStatus(model->renderStatus),draftMessage(model->draftMessage),publicationStatus(model->publicationStatus),
          canPublish(model->canPublish),canThread(model->canThread),hasFrame(model->hasFrame),canSaveDraft(model->canSaveDraft),reviewOpen(model->reviewOpen),renderedFrame(model->renderedFrame),
          threaded(model->threaded),width(model->width),height(model->height),contentWidth(model->contentWidth),panelWidth(model->panelWidth),
          previewWidth(model->previewWidth),previewHeight(model->previewHeight),listWidth(model->listWidth),listHeight(model->listHeight),
          hasDocument(document->hasDocument),title(document->publicationTitle),description(document->publicationDescription) {}
    std::function<void(double,double)> sizeHandler(double);
    void editTitle(std::string value) {(void)model->editTitle(std::move(value));}
    void editDescription(std::string value) {(void)model->editDescription(std::move(value));}
    void setThreaded(bool value) {(void)model->setThreaded(value);}
    void saveDraft() {(void)model->saveDraft();}
    void publish() {(void)model->publish();}
    void closeReview() {model->closeReview();}
    void previewSizeChanged(double width,double height) {model->previewSizeChanged(width,height);}
    void listSizeChanged(double width,double height) {model->listSizeChanged(width,height);}
    std::shared_ptr<dreamscapes::publishing::WorkspaceModel> model;
    std::shared_ptr<dreamscapes::native::DocumentSession> document;
    ii::core::Observable<std::vector<std::shared_ptr<dreamscapes::publishing::Target>>> &targets;
    ii::core::Observable<std::vector<std::shared_ptr<dreamscapes::publishing::ReviewField>>> &reviewFields;
    ii::core::Observable<std::string> &platformName,&policyText,&planText,&renderStatus,&draftMessage,&publicationStatus;
    ii::core::Observable<bool> &canPublish,&canThread,&hasFrame,&canSaveDraft,&reviewOpen;
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> &renderedFrame;
    ii::core::Observable<bool> &threaded;
    ii::core::Observable<double> &width,&height,&contentWidth,&panelWidth,&previewWidth,&previewHeight,&listWidth,&listHeight;
    ii::core::Observable<bool> &hasDocument;
    ii::core::Observable<std::string> &title,&description;
};
