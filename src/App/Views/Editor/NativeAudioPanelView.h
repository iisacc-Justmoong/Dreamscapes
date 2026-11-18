#pragma once
#include "NativeAudioPanel.h"
#include <iiGUI>

namespace dreamscapes::editor {
[[nodiscard]] ii::core::Result<void> buildAudioPanel(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<AudioPanelModel>);
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> audioPanelSource(std::shared_ptr<AudioPanelModel>);
}
class NativeAudioPanelBackend final {
  public:
    explicit NativeAudioPanelBackend(std::shared_ptr<dreamscapes::editor::AudioPanelModel> value)
        :model(std::move(value)),trackNames(model->trackNames),clipNames(model->clipNames),selectedTrack(model->selectedTrack),selectedClip(model->selectedClip),canCreate(model->canCreate),
         canChoose(model->canChoose),canApply(model->canApply),editable(model->editable),clipEditable(model->clipEditable),muted(model->muted),
         importMode(model->importMode),trimMode(model->trimMode),mixMode(model->mixMode),canTrim(model->canTrim),gainText(model->gainText),summary(model->summary),status(model->status),trimRange(model->trimRange),
         width(model->width),height(model->height),contentWidth(model->contentWidth),fieldWidth(model->fieldWidth),gainValue(model->gainValue) {}
    std::function<void(double,double)> sizeHandler(double inset,double maximumField);
    void selectTrack(int v) {(void)model->selectTrack(v);}
    void selectClip(int v) {(void)model->selectClip(v);}
    void createTrack() {(void)model->createTrack();}
    void chooseFile() {(void)model->chooseFile();}
    void setMuted(bool v) {(void)model->setMutedDraft(v);}
    void editGain(std::string v) {(void)model->editGain(std::move(v));}
    void moveGain(double v) {(void)model->setGainDraft(v);}
    void applyTrack() {(void)model->applyTrack();}
    void resetTrack() {(void)model->resetTrack();}
    void showImport() {model->showImport();}
    void showTrim() {model->showTrim();}
    void showMix() {model->showMix();}
    void editTrim(std::string v) {(void)model->editTrim(std::move(v));}
    void applyTrim() {(void)model->applyTrim();}
    void resetTrim() {(void)model->resetTrim();}
    std::shared_ptr<dreamscapes::editor::AudioPanelModel> model;
    ii::core::Observable<std::vector<std::string>> &trackNames,&clipNames;
    ii::core::Observable<int> &selectedTrack,&selectedClip;
    ii::core::Observable<bool> &canCreate,&canChoose,&canApply,&editable,&clipEditable,&muted,&importMode,&trimMode,&mixMode,&canTrim;
    ii::core::Observable<std::string> &gainText,&summary,&status,&trimRange;
    const double minimumGain=dreamscapes::editor::AudioPanelModel::minimumGain,maximumGain=dreamscapes::editor::AudioPanelModel::maximumGain;
    ii::core::Observable<double> &width,&height,&contentWidth,&fieldWidth,&gainValue;
};
