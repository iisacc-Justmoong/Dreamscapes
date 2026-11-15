#pragma once
#include "App/Native/NativeDocumentSession.h"
#include "App/Native/NativeCanvasFileModel.h"
#include "NativeEditorFrame.h"
#include "NativeTimelinePlayback.h"
#include "NativeLayerPanel.h"
#include "NativeElementsPanel.h"
#include "NativeMaskPanel.h"
#include "NativeFilePanel.h"
#include "NativeCanvasPanel.h"
#include "NativeTimelinePanel.h"
#include <iiSDKCore/Core/Property/Observable.h>
#include <functional>
#include <memory>
#include <string>

namespace dreamscapes::editor {
class WorkspaceModel final : public std::enable_shared_from_this<WorkspaceModel> {
  public:
    struct FileActions {
        std::function<ii::core::Result<void>()> open,save,saveAs;
    };
    static std::shared_ptr<WorkspaceModel> create(std::shared_ptr<native::DocumentSession>);
    void setPublishHandler(std::function<ii::core::Result<void>()> handler) {publish_=std::move(handler);}
    void setFramePresenter(std::shared_ptr<FramePresenter>);
    void setPlayback(std::shared_ptr<TimelinePlayback>);
    void setVisible(bool);
    void canvasSizeChanged(double,double);
    void setFileModel(std::shared_ptr<native::CanvasFileModel>,FileActions);
    void openFile();
    void saveFile();
    void saveFileAs();
    void sizeChanged(double width,double height,double maximumPanel,double toolbarWidth,double gaps);
    void showFilePanel();
    void showLayerPanel();
    void showCanvasPanel();
    void showElementsPanel();
    void showMaskPanel();
    void createTimelineLayer(iiSharedCanvas::LayerKind);
    void createTimelineAudioTrack();
    void previous();
    void next();
    void togglePlayback();
    void publish();
    std::shared_ptr<native::DocumentSession> document() const {return document_;}
    std::shared_ptr<LayerPanelModel> layerPanel() const {return layerPanel_;}
    std::shared_ptr<FilePanelModel> filePanel() const {return filePanel_;}
    std::shared_ptr<CanvasPanelModel> canvasPanel() const {return canvasPanel_;}
    std::shared_ptr<ElementsPanelModel> elementsPanel() const {return elementsPanel_;}
    std::shared_ptr<MaskPanelModel> maskPanel() const {return maskPanel_;}
    std::shared_ptr<TimelinePanelModel> timelinePanel() const {return timelinePanel_;}
    ii::core::Observable<double> width{1},height{1},canvasWidth{1},panelWidth{1};
    ii::core::Observable<double> previewWidth{1},previewHeight{1};
    ii::core::Observable<bool> canPrevious{false},canNext{false},canPlay{false},hasFrame{false};
    ii::core::Observable<bool> canOpenFile{false},canSaveFile{false},canSaveFileAs{false};
    ii::core::Observable<bool> filePanelActive{false},layerPanelActive{true},canvasPanelActive{false},elementsPanelActive{false},maskPanelActive{false};
    ii::core::Observable<std::string> activePanel{"layers"};
    ii::core::Observable<std::string> frameText{"No timeline"},renderStatus{"Canvas renderer unavailable"},fileMessage;
    ii::core::Observable<std::string> playText{"Play"};
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> renderedFrame;
  private:
    explicit WorkspaceModel(std::shared_ptr<native::DocumentSession>);
    void start();
    void synchronize();
    void synchronizeFile();
    void selectPanel(std::string);
    void invokeFile(const std::function<ii::core::Result<void>()> &);
    std::shared_ptr<native::DocumentSession> document_;
    std::shared_ptr<LayerPanelModel> layerPanel_;
    std::shared_ptr<FilePanelModel> filePanel_;
    std::shared_ptr<CanvasPanelModel> canvasPanel_;
    std::shared_ptr<ElementsPanelModel> elementsPanel_;
    std::shared_ptr<MaskPanelModel> maskPanel_;
    std::shared_ptr<TimelinePanelModel> timelinePanel_;
    ii::core::ObservableSubscription frameObserver_,countObserver_,documentObserver_;
    ii::core::ObservableSubscription imageObserver_,statusObserver_;
    ii::core::ObservableSubscription playbackObserver_;
    ii::core::ObservableSubscription fileBusyObserver_,fileSaveObserver_,fileSaveAsObserver_,fileStatusObserver_,fileErrorObserver_,dirtyObserver_;
    std::shared_ptr<native::CanvasFileModel> fileModel_;
    FileActions fileActions_;
    std::shared_ptr<FramePresenter> framePresenter_;
    std::shared_ptr<TimelinePlayback> playback_;
    bool visible_=false,selectingPanel_=false,panelPending_=false;
    std::function<ii::core::Result<void>()> publish_;
};
}
