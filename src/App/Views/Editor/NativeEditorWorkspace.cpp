#include "NativeEditorWorkspace.h"
#include "NativeAssets.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace dreamscapes::editor {
WorkspaceModel::WorkspaceModel(std::shared_ptr<native::DocumentSession> document) : document_(std::move(document)) {
    if (!document_) throw std::invalid_argument("Editor requires a document session");
}
std::shared_ptr<WorkspaceModel> WorkspaceModel::create(std::shared_ptr<native::DocumentSession> document) {
    auto model=std::shared_ptr<WorkspaceModel>(new WorkspaceModel(std::move(document))); model->start(); return model;
}
void WorkspaceModel::start() {
    const std::weak_ptr<WorkspaceModel> weak=shared_from_this();
    layerPanel_=LayerPanelModel::create(document_);
    filePanel_=FilePanelModel::create(document_,native::filePanelCatalog);
    canvasPanel_=CanvasPanelModel::create(document_);
    elementsPanel_=ElementsPanelModel::create(document_);
    const auto textDefaults=parseTextDefaults(native::toolCatalog);
    if (!textDefaults) throw std::invalid_argument(textDefaults.error().message);
    textTool_=TextToolModel::create(document_,*textDefaults);
    selectionTool_=SelectionToolModel::create(document_);
    toolModeObserver_=elementsPanel_->shapeTool()->selecting.observe([weak](bool,std::uint64_t){
        if (auto value=weak.lock()) value->synchronizeTools();
    });
    maskPanel_=MaskPanelModel::create(document_);
    maskLasso_=MaskLassoModel::create(document_,maskPanel_);
    maskLassoObserver_=maskPanel_->lassoActive.observe([weak](bool,std::uint64_t){
        if (auto value=weak.lock()) value->synchronizeTools();
    });
    lvui::SliderConfiguration mini;
    lvui::apply_slider_size(mini,lvui::SliderSize::mini);
    timelinePanel_=TimelinePanelModel::create(document_,mini.height);
    auto changed=[weak](const auto &,std::uint64_t){if (auto value=weak.lock()) value->synchronize();};
    frameObserver_=document_->frame.observe(changed); countObserver_=document_->frameCount.observe(changed);
    documentObserver_=document_->hasDocument.observe(changed); synchronize();
    dirtyObserver_=document_->dirty.observe([weak](const auto &,std::uint64_t){if (auto value=weak.lock()) value->synchronizeFile();});
}
void WorkspaceModel::synchronize() {
    const auto frame=document_->frame.get(),count=document_->frameCount.get();
    const bool available=document_->hasDocument.get() && count>0;
    (void)canPrevious.set(available && frame>0); (void)canNext.set(available && frame+1<count);
    (void)canPlay.set(available && count>1 && bool(playback_));
    (void)frameText.set(available?std::to_string(frame+1)+" / "+std::to_string(count):"No timeline");
}
void WorkspaceModel::setPlayback(std::shared_ptr<TimelinePlayback> value) {
    playbackObserver_={};
    if (playback_) (void)playback_->pause();
    playback_=std::move(value);
    if (playback_) {
        const std::weak_ptr<WorkspaceModel> weak=shared_from_this();
        playbackObserver_=playback_->playing.observe([weak](bool playing,std::uint64_t) {
            if (auto model=weak.lock()) (void)model->playText.set(playing?"Pause":"Play");
        },true);
    } else (void)playText.set("Play");
    synchronize();
}
void WorkspaceModel::sizeChanged(double w,double h,double maximumPanel,double toolbarWidth,double gaps) {
    if (!std::isfinite(w) || !std::isfinite(h) || !std::isfinite(maximumPanel) || !std::isfinite(toolbarWidth) ||
        !std::isfinite(gaps) || w<0 || h<0 || w>8192 || h>8192 || maximumPanel<=0 || toolbarWidth<0 || gaps<0) return;
    (void)width.set(w); (void)height.set(h);
    const auto available=std::max(1.0,w-toolbarWidth-gaps);
    const auto panel=std::min(maximumPanel,available/3.0);
    (void)panelWidth.set(std::max(1.0,panel)); (void)canvasWidth.set(std::max(1.0,available-panel));
}
void WorkspaceModel::showFilePanel() {
    selectPanel("file");
}
void WorkspaceModel::showLayerPanel() {
    selectPanel("layers");
}
void WorkspaceModel::showCanvasPanel() {
    selectPanel("canvas");
}
void WorkspaceModel::showElementsPanel() {
    selectPanel("elements");
    if (elementsPanelActive.get() && elementsPanel_->shapeTool()->selecting.get())
        (void)elementsPanel_->shapeTool()->chooseCurrentShape();
}
void WorkspaceModel::showTextPanel() {selectPanel("text");}
void WorkspaceModel::showSelectTool() {
    selectPanel("elements");
    if (elementsPanelActive.get()) (void)elementsPanel_->shapeTool()->selectTool();
}
void WorkspaceModel::showMaskPanel() {
    selectPanel("mask");
}
void WorkspaceModel::showColorPanel() {
    if (auto panel=backgroundPanel.get()) {panel->showColor();selectPanel("background-color");}
}
void WorkspaceModel::showReplacePanel() {
    if (auto panel=backgroundPanel.get()) {panel->showReplace();selectPanel("background-color");}
}
void WorkspaceModel::showAudioPanel() {if (audioPanel.get()) selectPanel("audio");}
void WorkspaceModel::showLightPanel() {if (lightPanel.get()) selectPanel("light");}
void WorkspaceModel::showBrushPanel() {if (brushPanel.get()) selectPanel("brush");}
void WorkspaceModel::setBrushPanel(std::shared_ptr<BrushToolModel> panel) {
    brushPreviewObserver_={};
    if (auto previous=brushPanel.get()) previous->setVisible(false);
    (void)brushPanel.set(panel);(void)brushAvailable.set(bool(panel));
    if (!panel && activePanel.get()=="brush") selectPanel("layers");
    else if (panel) {
        const std::weak_ptr<WorkspaceModel> weak=shared_from_this();
        const std::weak_ptr<BrushToolModel> expected=panel;
        brushPreviewObserver_=panel->previewImage.observe([weak,expected](const auto &,std::uint64_t) {
            auto self=weak.lock();auto brush=expected.lock();
            if (self && brush && self->brushPanel.get()==brush) self->synchronizeDisplayedFrame();
        });
        panel->setViewport(previewWidth.get(),previewHeight.get());
        if (framePresenter_ && framePresenter_->current.get()) panel->setFrame(framePresenter_->image.get());
        panel->setVisible(visible_ && activePanel.get()=="brush");
    }
    synchronizeDisplayedFrame();
}
void WorkspaceModel::setLightPanel(std::shared_ptr<LightPanelModel> panel) {
    lightPreviewObserver_={};
    if (auto previous=lightPanel.get()) previous->setVisible(false);
    (void)lightPanel.set(panel);(void)lightAvailable.set(bool(panel));
    if (!panel && activePanel.get()=="light") selectPanel("layers");
    else if (panel) {
        const std::weak_ptr<WorkspaceModel> weak=shared_from_this();
        const std::weak_ptr<LightPanelModel> expected=panel;
        lightPreviewObserver_=panel->previewImage.observe([weak,expected](const auto &,std::uint64_t) {
            auto self=weak.lock();auto light=expected.lock();
            if (self && light && self->lightPanel.get()==light) self->synchronizeDisplayedFrame();
        });
        panel->setViewport(previewWidth.get(),previewHeight.get());
        panel->setVisible(visible_ && activePanel.get()=="light");
    }
    synchronizeDisplayedFrame();
}
void WorkspaceModel::setAudioPanel(std::shared_ptr<AudioPanelModel> panel) {
    if (auto prior=audioPanel.get()) prior->setVisible(false);
    audioStatusObserver_={};timelinePanel_->setAudioClipEdit({});
    (void)audioPanel.set(panel);(void)audioAvailable.set(bool(panel));
    if (!panel && activePanel.get()=="audio") selectPanel("layers");
    else if (panel) {
        const std::weak_ptr<AudioPanelModel> audio=panel;
        timelinePanel_->setAudioClipEdit([audio](std::string track,std::string clip,ii::gui::EditorTimelineGesture kind,
                                                  std::int64_t first,std::int64_t end) {
            if (auto value=audio.lock()) return value->editTimelineClip(std::move(track),std::move(clip),kind,first,end);
            return ii::core::Result<void>(ii::core::fail(ii::core::ErrorCode::closed,"Audio editing is unavailable"));
        });
        const std::weak_ptr<TimelinePanelModel> timeline=timelinePanel_;
        audioStatusObserver_=panel->status.observe([timeline](const auto &message,std::uint64_t) {
            if (auto value=timeline.lock()) (void)value->status.set(message);
        });
        panel->setVisible(visible_ && activePanel.get()=="audio");
    }
}
void WorkspaceModel::setBackgroundPanel(std::shared_ptr<BackgroundPanelModel> panel) {
    if (auto prior=backgroundPanel.get()) {prior->setVisible(false);prior->setMaskAction({});}
    const std::weak_ptr<WorkspaceModel> weak=shared_from_this();
    if (panel) panel->setMaskAction([weak]{if (auto value=weak.lock()) value->showMaskPanel();});
    maskPanel_->setColorAction(panel?std::function<void()>([weak]{if (auto value=weak.lock()) value->showColorPanel();}):std::function<void()>{});
    maskPanel_->setReplaceAction(panel?std::function<void()>([weak]{if (auto value=weak.lock()) value->showReplacePanel();}):std::function<void()>{});
    (void)backgroundPanel.set(panel);
    if (!panel && activePanel.get()=="background-color") selectPanel("mask");
    else if (panel) panel->setVisible(visible_ && activePanel.get()=="background-color");
}
void WorkspaceModel::createTimelineLayer(iiSharedCanvas::LayerKind kind) {
    const auto result=layerPanel_->createLayer(kind);
    (void)timelinePanel_->status.set(result?std::string{}:result.error().message);
}
void WorkspaceModel::createTimelineAudioTrack() {
    const auto result=document_->createAudioTrack();
    (void)timelinePanel_->status.set(result?std::string{}:result.error().message);
}
void WorkspaceModel::selectPanel(std::string panel) {
    if (selectingPanel_) {(void)activePanel.set(std::move(panel));panelPending_=true;return;}
    selectingPanel_=true;
    try {
        (void)activePanel.set(std::move(panel));
        do {
            panelPending_=false;
            const auto active=activePanel.get();
            (void)filePanelActive.set(active=="file");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            (void)layerPanelActive.set(active=="layers");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            (void)canvasPanelActive.set(active=="canvas");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            (void)elementsPanelActive.set(active=="elements");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            (void)textPanelActive.set(active=="text");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            (void)maskPanelActive.set(active=="mask");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            (void)backgroundPanelActive.set(active=="mask" || active=="background-color");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            (void)audioPanelActive.set(active=="audio");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            (void)lightPanelActive.set(active=="light");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            (void)brushPanelActive.set(active=="brush");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            layerPanel_->setVisible(visible_ && active=="layers");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            filePanel_->setVisible(visible_ && active=="file");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            canvasPanel_->setVisible(visible_ && active=="canvas");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            elementsPanel_->setVisible(visible_ && active=="elements");
            synchronizeTools();
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            maskPanel_->setVisible(visible_ && active=="mask");
            if (auto color=backgroundPanel.get()) color->setVisible(visible_ && active=="background-color");
            if (auto audio=audioPanel.get()) audio->setVisible(visible_ && active=="audio");
            if (auto light=lightPanel.get()) light->setVisible(visible_ && active=="light");
            if (auto brush=brushPanel.get()) brush->setVisible(visible_ && active=="brush");
            synchronizeDisplayedFrame();
            if (activePanel.get()!=active) panelPending_=true;
        } while (panelPending_);
        selectingPanel_=false;
    } catch (...) {selectingPanel_=false;throw;}
}
void WorkspaceModel::setFramePresenter(std::shared_ptr<FramePresenter> presenter) {
    imageObserver_={}; statusObserver_={}; frameCurrentObserver_={};
    if (framePresenter_) (void)framePresenter_->setVisible(false);
    framePresenter_=std::move(presenter);
    if (!framePresenter_) {
        if (histogram_) (void)histogram_->setSource({});
        elementsPanel_->shapeTool()->setFrame({});
        textTool_->setFrame({});
        selectionTool_->setFrame({});
        maskLasso_->setFrame({});
        if (auto brush=brushPanel.get()) brush->setFrame({});
        synchronizeDisplayedFrame();
        (void)renderStatus.set("Canvas renderer unavailable"); return;
    }
    const std::weak_ptr<WorkspaceModel> weak=shared_from_this();
    imageObserver_=framePresenter_->image.observe([weak](const auto &image,std::uint64_t){
        if (auto model=weak.lock()) {
            model->synchronizeDisplayedFrame();
            model->synchronizeToolFrame();
        }
    },true);
    statusObserver_=framePresenter_->status.observe([weak](const auto &status,std::uint64_t){
        if (auto model=weak.lock()) (void)model->renderStatus.set(status);
    },true);
    frameCurrentObserver_=framePresenter_->current.observe([weak](bool,std::uint64_t){
        if (auto model=weak.lock()) model->synchronizeToolFrame();
    },true);
    (void)framePresenter_->setViewport(previewWidth.get(),previewHeight.get());
    (void)framePresenter_->setVisible(visible_);
}
void WorkspaceModel::synchronizeDisplayedFrame() {
    auto image=framePresenter_?framePresenter_->image.get():nullptr;
    if (visible_ && lightPanelActive.get())
        if (auto light=lightPanel.get();light && light->previewImage.get()) image=light->previewImage.get();
    if (visible_ && brushPanelActive.get())
        if (auto brush=brushPanel.get();brush && brush->previewImage.get()) image=brush->previewImage.get();
    (void)renderedFrame.set(image);(void)hasFrame.set(bool(image));
    if (histogram_) (void)histogram_->setSource(image);
}
void WorkspaceModel::synchronizeToolFrame() {
    const auto image=framePresenter_ && framePresenter_->current.get()?framePresenter_->image.get():nullptr;
    if (elementsPanel_) elementsPanel_->shapeTool()->setFrame(image);
    if (textTool_) textTool_->setFrame(image);
    if (selectionTool_) selectionTool_->setFrame(image);
    if (maskLasso_) maskLasso_->setFrame(image);
    if (auto brush=brushPanel.get()) brush->setFrame(image);
}
void WorkspaceModel::setHistogram(std::shared_ptr<HistogramModel> value) {
    layerPanel_->setHistogram({});
    histogram_=std::move(value);
    if (histogram_) (void)histogram_->setSource(renderedFrame.get());
    layerPanel_->setHistogram(histogram_);
}
void WorkspaceModel::setVisible(bool value) {
    visible_=value;
    if (layerPanel_) layerPanel_->setVisible(value && layerPanelActive.get());
    if (filePanel_) filePanel_->setVisible(value && filePanelActive.get());
    if (canvasPanel_) canvasPanel_->setVisible(value && canvasPanelActive.get());
    if (elementsPanel_) elementsPanel_->setVisible(value && elementsPanelActive.get());
    synchronizeTools();
    if (maskPanel_) maskPanel_->setVisible(value && maskPanelActive.get());
    if (auto color=backgroundPanel.get()) color->setVisible(value && activePanel.get()=="background-color");
    if (auto audio=audioPanel.get()) audio->setVisible(value && audioPanelActive.get());
    if (auto light=lightPanel.get()) light->setVisible(value && lightPanelActive.get());
    if (auto brush=brushPanel.get()) brush->setVisible(value && brushPanelActive.get());
    synchronizeDisplayedFrame();
    if (!value && playback_) (void)playback_->pause();
    if (framePresenter_) (void)framePresenter_->setVisible(value);
}
void WorkspaceModel::synchronizeTools() {
    if (!elementsPanel_ || !selectionTool_) return;
    const auto visible=visible_ && elementsPanelActive.get();
    const auto selecting=elementsPanel_->shapeTool()->selecting.get();
    elementsPanel_->shapeTool()->setVisible(visible && !selecting);
    selectionTool_->setVisible(visible && selecting);
    if (textTool_) textTool_->setVisible(visible_ && textPanelActive.get());
    if (maskPanel_ && maskLasso_) maskLasso_->setVisible(visible_ && maskPanelActive.get() && maskPanel_->lassoActive.get());
}
void WorkspaceModel::canvasSizeChanged(double w,double h,double handleSize) {
    if (!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0||w>8192||h>8192) return;
    (void)previewWidth.set(w); (void)previewHeight.set(h);
    if (elementsPanel_) elementsPanel_->shapeTool()->setViewport(w,h);
    if (textTool_) textTool_->setViewport(w,h);
    if (selectionTool_) selectionTool_->setViewport(w,h,handleSize);
    if (maskLasso_) maskLasso_->setViewport(w,h);
    if (auto light=lightPanel.get()) light->setViewport(w,h);
    if (auto brush=brushPanel.get()) brush->setViewport(w,h);
    if (framePresenter_) (void)framePresenter_->setViewport(w,h);
}
void WorkspaceModel::setFileModel(std::shared_ptr<native::CanvasFileModel> model,FileActions actions) {
    fileBusyObserver_={}; fileSaveObserver_={}; fileSaveAsObserver_={}; fileStatusObserver_={}; fileErrorObserver_={};
    fileModel_=std::move(model); fileActions_=std::move(actions);
    if (filePanel_) filePanel_->setFileModel(fileModel_);
    if (fileModel_) {
        const std::weak_ptr<WorkspaceModel> weak=shared_from_this();
        auto changed=[weak](const auto &,std::uint64_t){if (auto value=weak.lock()) value->synchronizeFile();};
        fileBusyObserver_=fileModel_->busy.observe(changed); fileSaveObserver_=fileModel_->canSave.observe(changed);
        fileSaveAsObserver_=fileModel_->canSaveAs.observe(changed); fileStatusObserver_=fileModel_->status.observe(changed);
        fileErrorObserver_=fileModel_->error.observe(changed);
    }
    synchronizeFile();
}
void WorkspaceModel::synchronizeFile() {
    const bool ready=fileModel_ && !fileModel_->busy.get();
    (void)canOpenFile.set(ready && bool(fileActions_.open));
    (void)canSaveFileAs.set(ready && fileModel_->canSaveAs.get() && bool(fileActions_.saveAs));
    (void)canSaveFile.set(ready && document_->dirty.get() && bool(fileActions_.save) &&
                          (fileModel_->canSave.get() || (fileModel_->canSaveAs.get() && bool(fileActions_.saveAs))));
    (void)fileMessage.set(fileModel_?(fileModel_->error.get().empty()?fileModel_->status.get():fileModel_->error.get()):std::string{});
}
void WorkspaceModel::invokeFile(const std::function<ii::core::Result<void>()> &action) {
    if (!action) return;
    auto handler=action; auto result=handler();
    if (!result && fileModel_) fileModel_->reportError(result.error().message);
}
void WorkspaceModel::openFile() {invokeFile(fileActions_.open);}
void WorkspaceModel::saveFile() {invokeFile(fileActions_.save);}
void WorkspaceModel::saveFileAs() {invokeFile(fileActions_.saveAs);}
void WorkspaceModel::previous() {if (canPrevious.get()) (void)document_->seek(document_->frame.get()-1);}
void WorkspaceModel::next() {if (canNext.get()) (void)document_->seek(document_->frame.get()+1);}
void WorkspaceModel::togglePlayback() {
    if (!playback_ || !canPlay.get()) return;
    auto result=playback_->playing.get()?playback_->pause():playback_->play();
    if (!result) (void)renderStatus.set(result.error().message);
}
void WorkspaceModel::publish() {if (document_->hasDocument.get() && publish_) {auto handler=publish_; (void)handler();}}
}
