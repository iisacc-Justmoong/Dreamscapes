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
    maskPanel_=MaskPanelModel::create(document_);
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
}
void WorkspaceModel::showMaskPanel() {
    selectPanel("mask");
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
            (void)maskPanelActive.set(active=="mask");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            filePanel_->setVisible(visible_ && active=="file");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            canvasPanel_->setVisible(visible_ && active=="canvas");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            elementsPanel_->setVisible(visible_ && active=="elements");
            if (panelPending_ || activePanel.get()!=active) {panelPending_=true;continue;}
            maskPanel_->setVisible(visible_ && active=="mask");
            if (activePanel.get()!=active) panelPending_=true;
        } while (panelPending_);
        selectingPanel_=false;
    } catch (...) {selectingPanel_=false;throw;}
}
void WorkspaceModel::setFramePresenter(std::shared_ptr<FramePresenter> presenter) {
    imageObserver_={}; statusObserver_={};
    if (framePresenter_) (void)framePresenter_->setVisible(false);
    framePresenter_=std::move(presenter);
    if (!framePresenter_) {
        (void)renderedFrame.set({}); (void)hasFrame.set(false);
        (void)renderStatus.set("Canvas renderer unavailable"); return;
    }
    const std::weak_ptr<WorkspaceModel> weak=shared_from_this();
    imageObserver_=framePresenter_->image.observe([weak](const auto &image,std::uint64_t){
        if (auto model=weak.lock()) {(void)model->renderedFrame.set(image); (void)model->hasFrame.set(bool(image));}
    },true);
    statusObserver_=framePresenter_->status.observe([weak](const auto &status,std::uint64_t){
        if (auto model=weak.lock()) (void)model->renderStatus.set(status);
    },true);
    (void)framePresenter_->setViewport(previewWidth.get(),previewHeight.get());
    (void)framePresenter_->setVisible(visible_);
}
void WorkspaceModel::setVisible(bool value) {
    visible_=value;
    if (filePanel_) filePanel_->setVisible(value && filePanelActive.get());
    if (canvasPanel_) canvasPanel_->setVisible(value && canvasPanelActive.get());
    if (elementsPanel_) elementsPanel_->setVisible(value && elementsPanelActive.get());
    if (maskPanel_) maskPanel_->setVisible(value && maskPanelActive.get());
    if (!value && playback_) (void)playback_->pause();
    if (framePresenter_) (void)framePresenter_->setVisible(value);
}
void WorkspaceModel::canvasSizeChanged(double w,double h) {
    if (!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0||w>8192||h>8192) return;
    (void)previewWidth.set(w); (void)previewHeight.set(h);
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
