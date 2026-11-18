#pragma once
#include "NativeEditorWorkspace.h"
#include "NativeLayerPanelView.h"
#include "NativeFilePanelView.h"
#include "NativeCanvasPanelView.h"
#include "NativeElementsPanelView.h"
#include "NativeTextPanelView.h"
#include "NativeMaskPanelView.h"
#include "NativeBackgroundPanelView.h"
#include "NativeAudioPanelView.h"
#include "NativeLightPanelView.h"
#include "NativeBrushPanelView.h"
#include "NativeBrushSurface.h"
#include "NativeTimelinePanelView.h"
#include "NativeShapeSurface.h"
#include "NativeSelectionSurface.h"
#include "NativeMaskLassoSurface.h"
#include <iiGUI>

namespace dreamscapes::editor {
[[nodiscard]] ii::core::Result<void> buildWorkspace(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<WorkspaceModel>);
class PanelSource final : public std::enable_shared_from_this<PanelSource> {
  public:
    static std::shared_ptr<PanelSource> create(const std::shared_ptr<WorkspaceModel> &);
    ii::core::Observable<std::shared_ptr<ii::gui::NativeViewSource>> current;
    ii::core::Observable<std::shared_ptr<ii::gui::NativeViewSource>> brushSurface;
  private:
    std::shared_ptr<ii::gui::NativeViewSource> file_,layer_,canvas_,elements_,text_,mask_,color_,audio_,light_,brush_;
    ii::core::ObservableSubscription active_,colorModel_,audioModel_,lightModel_,brushModel_;
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
          canvasPanelActive(model->canvasPanelActive),elementsPanelActive(model->elementsPanelActive),textPanelActive(model->textPanelActive),maskPanelActive(model->maskPanelActive),backgroundPanelActive(model->backgroundPanelActive),
          audioPanelActive(model->audioPanelActive),audioAvailable(model->audioAvailable),lightPanelActive(model->lightPanelActive),lightAvailable(model->lightAvailable),
          brushPanelActive(model->brushPanelActive),brushAvailable(model->brushAvailable),
          panelBinding(dreamscapes::editor::PanelSource::create(model)),panelView(panelBinding->current),
          brushView(panelBinding->brushSurface),
          timelineView(dreamscapes::editor::timelinePanelSource(model->timelinePanel())),
          selectingTool(model->elementsPanel()->shapeTool()->selecting),
          selectionStatus(model->selectionTool()->status),
          maskLassoActive(model->maskPanel()->lassoActive),
          shapeView(dreamscapes::editor::shapeSurfaceSource(model->elementsPanel()->shapeTool())),
          textView(dreamscapes::editor::textSurfaceSource(model->textTool())),
          selectionView(dreamscapes::editor::selectionSurfaceSource(model->selectionTool())),
          maskLassoView(dreamscapes::editor::maskLassoSurfaceSource(model->maskLasso())) {}
    std::function<void(double,double)> sizeHandler(double maximumPanel,double toolbarWidth,double gaps);
    std::function<void(double,double)> canvasSizeHandler(double handleSize);
    void previous() {model->previous();}
    void next() {model->next();}
    void togglePlayback() {model->togglePlayback();}
    void publish() {model->publish();}
    void timelineSizeChanged(double width,double height) {model->timelinePanel()->sizeChanged(width,height);}
    void openFile() {model->openFile();}
    void saveFile() {model->saveFile();}
    void saveFileAs() {model->saveFileAs();}
    void showFilePanel() {model->showFilePanel();}
    void showLayerPanel() {model->showLayerPanel();}
    void showCanvasPanel() {model->showCanvasPanel();}
    void showElementsPanel() {model->showElementsPanel();}
    void showTextPanel() {model->showTextPanel();}
    void showSelectTool() {model->showSelectTool();}
    void showMaskPanel() {model->showMaskPanel();}
    void showAudioPanel() {model->showAudioPanel();}
    void showLightPanel() {model->showLightPanel();}
    void showBrushPanel() {model->showBrushPanel();}
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
    ii::core::Observable<bool> &filePanelActive,&layerPanelActive,&canvasPanelActive,&elementsPanelActive,&textPanelActive,&maskPanelActive,&backgroundPanelActive,&audioPanelActive,&audioAvailable,&lightPanelActive,&lightAvailable,&brushPanelActive,&brushAvailable;
    std::shared_ptr<dreamscapes::editor::PanelSource> panelBinding;
    ii::core::Observable<std::shared_ptr<ii::gui::NativeViewSource>> &panelView;
    ii::core::Observable<std::shared_ptr<ii::gui::NativeViewSource>> &brushView;
    std::shared_ptr<ii::gui::NativeViewSource> timelineView;
    ii::core::Observable<bool> &selectingTool;
    ii::core::Observable<std::string> &selectionStatus;
    ii::core::Observable<bool> &maskLassoActive;
    std::shared_ptr<ii::gui::NativeViewSource> shapeView;
    std::shared_ptr<ii::gui::NativeViewSource> textView;
    std::shared_ptr<ii::gui::NativeViewSource> selectionView;
    std::shared_ptr<ii::gui::NativeViewSource> maskLassoView;
  private:
    ii::gui::Widget timelineMenu_,timelineAnchor_;
};
