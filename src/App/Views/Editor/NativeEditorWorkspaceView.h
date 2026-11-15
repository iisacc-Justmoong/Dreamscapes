#pragma once
#include "NativeEditorWorkspace.h"
#include "NativeLayerPanelView.h"
#include "NativeFilePanelView.h"
#include "NativeCanvasPanelView.h"
#include "NativeElementsPanelView.h"
#include "NativeMaskPanelView.h"
#include "NativeTimelinePanelView.h"
#include <iiGUI/NativeWidgets.h>

namespace dreamscapes::editor {
[[nodiscard]] ii::core::Result<void> buildWorkspace(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<WorkspaceModel>);
class PanelSource final : public std::enable_shared_from_this<PanelSource> {
  public:
    static std::shared_ptr<PanelSource> create(const std::shared_ptr<WorkspaceModel> &);
    ii::core::Observable<std::shared_ptr<ii::gui::NativeViewSource>> current;
  private:
    std::shared_ptr<ii::gui::NativeViewSource> file_,layer_,canvas_,elements_,mask_;
    ii::core::ObservableSubscription active_;
};
}
class NativeEditorWorkspaceBackend final {
  public:
    explicit NativeEditorWorkspaceBackend(std::shared_ptr<dreamscapes::editor::WorkspaceModel> value)
        : model(std::move(value)),document(model->document()),width(model->width),height(model->height),
          canvasWidth(model->canvasWidth),panelWidth(model->panelWidth),canPrevious(model->canPrevious),
          canNext(model->canNext),frameText(model->frameText),hasDocument(document->hasDocument),dirty(document->dirty),
          title(document->title),dimensions(document->dimensions),layers(document->layers),
          previewWidth(model->previewWidth),previewHeight(model->previewHeight),hasFrame(model->hasFrame),
          renderStatus(model->renderStatus),renderedFrame(model->renderedFrame),canOpenFile(model->canOpenFile),
          canSaveFile(model->canSaveFile),canSaveFileAs(model->canSaveFileAs),fileMessage(model->fileMessage),
          canPlay(model->canPlay),playText(model->playText),
          timelineWidth(model->timelinePanel()->width),timelineHeight(model->timelinePanel()->height),
          timelineStatus(model->timelinePanel()->status),filePanelActive(model->filePanelActive),layerPanelActive(model->layerPanelActive),
          canvasPanelActive(model->canvasPanelActive),elementsPanelActive(model->elementsPanelActive),maskPanelActive(model->maskPanelActive),
          panelBinding(dreamscapes::editor::PanelSource::create(model)),panelView(panelBinding->current),
          timelineView(dreamscapes::editor::timelinePanelSource(model->timelinePanel())) {}
    std::function<void(double,double)> sizeHandler(double maximumPanel,double toolbarWidth,double gaps);
    void previous() {model->previous();}
    void next() {model->next();}
    void togglePlayback() {model->togglePlayback();}
    void publish() {model->publish();}
    void canvasSizeChanged(double width,double height) {model->canvasSizeChanged(width,height);}
    void timelineSizeChanged(double width,double height) {model->timelinePanel()->sizeChanged(width,height);}
    void openFile() {model->openFile();}
    void saveFile() {model->saveFile();}
    void saveFileAs() {model->saveFileAs();}
    void showFilePanel() {model->showFilePanel();}
    void showLayerPanel() {model->showLayerPanel();}
    void showCanvasPanel() {model->showCanvasPanel();}
    void showElementsPanel() {model->showElementsPanel();}
    void showMaskPanel() {model->showMaskPanel();}
    void createStaticBitmap() {model->createTimelineLayer(iiSharedCanvas::LayerKind::StaticBitmap);}
    void createStaticVector() {model->createTimelineLayer(iiSharedCanvas::LayerKind::StaticVector);}
    void createDynamicBitmap() {model->createTimelineLayer(iiSharedCanvas::LayerKind::DynamicBitmap);}
    void createDynamicVector() {model->createTimelineLayer(iiSharedCanvas::LayerKind::DynamicVector);}
    void createAudioTrack() {model->createTimelineAudioTrack();}
    void attachTimelineMenu(ii::gui::Widget,ii::gui::Widget);
    void openTimelineMenu();
    std::shared_ptr<dreamscapes::editor::WorkspaceModel> model;
    std::shared_ptr<dreamscapes::native::DocumentSession> document;
    ii::core::Observable<double> &width,&height,&canvasWidth,&panelWidth;
    ii::core::Observable<bool> &canPrevious,&canNext;
    ii::core::Observable<std::string> &frameText;
    ii::core::Observable<bool> &hasDocument,&dirty;
    ii::core::Observable<std::string> &title,&dimensions;
    ii::core::Observable<std::vector<std::shared_ptr<dreamscapes::native::DocumentLayerRow>>> &layers;
    ii::core::Observable<double> &previewWidth,&previewHeight;
    ii::core::Observable<bool> &hasFrame;
    ii::core::Observable<std::string> &renderStatus;
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> &renderedFrame;
    ii::core::Observable<bool> &canOpenFile,&canSaveFile,&canSaveFileAs;
    ii::core::Observable<std::string> &fileMessage;
    ii::core::Observable<bool> &canPlay;
    ii::core::Observable<std::string> &playText;
    ii::core::Observable<double> &timelineWidth,&timelineHeight;
    ii::core::Observable<std::string> &timelineStatus;
    ii::core::Observable<bool> &filePanelActive,&layerPanelActive,&canvasPanelActive,&elementsPanelActive,&maskPanelActive;
    std::shared_ptr<dreamscapes::editor::PanelSource> panelBinding;
    ii::core::Observable<std::shared_ptr<ii::gui::NativeViewSource>> &panelView;
    std::shared_ptr<ii::gui::NativeViewSource> timelineView;
  private:
    ii::gui::Widget timelineMenu_,timelineAnchor_;
};
