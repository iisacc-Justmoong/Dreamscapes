#pragma once
#include "NativeBackgroundPanel.h"
#include <iiGUI>

namespace dreamscapes::editor {
[[nodiscard]] ii::core::Result<void> buildBackgroundPanel(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<BackgroundPanelModel>);
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> backgroundPanelSource(std::shared_ptr<BackgroundPanelModel>);
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> backgroundPickerSurface(std::shared_ptr<BackgroundPanelModel>,bool rail);
}
class NativeBackgroundPanelBackend final {
  public:
    explicit NativeBackgroundPanelBackend(std::shared_ptr<dreamscapes::editor::BackgroundPanelModel> value)
        :model(std::move(value)),modeNames(model->modeNames),fillNames(model->fillNames),presetNames(model->presetNames),swatchNames(model->swatchNames),imageFits(model->imageFits),
         channels(model->channels),allChannels(model->allChannels),stopNames(model->stopNames),recentNames(model->recentNames),modeIndex(model->modeIndex),fillIndex(model->fillIndex),stopIndex(model->stopIndex),imageFit(model->imageFit),
         canApply(model->canApply),hasPlane(model->hasPlane),gradient(model->gradient),perceptual(model->perceptual),canRemoveStop(model->canRemoveStop),replaceMode(model->replaceMode),canChooseImage(model->canChooseImage),canApplyImage(model->canApplyImage),busy(model->busy),
         alpha(model->alpha),opacity(model->opacity),angle(model->angle),stopPosition(model->stopPosition),width(model->width),height(model->height),
         contentWidth(model->contentWidth),planeWidth(model->planeWidth),railWidth(model->railWidth),planeHeight(model->planeHeight),
         channelListHeight(model->channelListHeight),allChannelListHeight(model->allChannelListHeight),channelSliderWidth(model->channelSliderWidth),
         hex(model->hex),status(model->status),availability(model->availability),alphaText(model->alphaText),opacityText(model->opacityText),angleText(model->angleText),stopPositionText(model->stopPositionText),imageName(model->imageName),
         planeView(dreamscapes::editor::backgroundPickerSurface(model,false)),railView(dreamscapes::editor::backgroundPickerSurface(model,true)) {}
    std::function<void(double,double)> sizeHandler(double inset,double rail,double gap,double rowHeight);
    void showMask() {model->showMask();}
    void showColor() {model->showColor();}
    void showReplace() {model->showReplace();}
    void chooseImage() {(void)model->chooseImage();}
    void selectImageFit(int v) {(void)model->selectImageFit(v);}
    void applyImage() {(void)model->applyImage();}
    void selectMode(int v) {(void)model->selectMode(v);}
    void selectFill(int v) {(void)model->selectFill(v);}
    void selectStop(int v) {(void)model->selectStop(v);}
    void selectPreset(int v) {(void)model->selectPreset(v);}
    void selectSwatch(int v) {(void)model->selectSwatch(v);}
    void selectRecent(int v) {(void)model->selectRecent(v);}
    void editHex(std::string v) {(void)model->editHex(std::move(v));}
    void commitHex() {(void)model->commitHex();}
    void setAlpha(double v) {(void)model->setAlpha(v);}
    void setOpacity(double v) {(void)model->setOpacity(v);}
    void setAngle(double v) {(void)model->setAngle(v);}
    void setStopPosition(double v) {(void)model->setStopPosition(v);}
    void setPerceptual(bool v) {(void)model->setPerceptual(v);}
    void addStop() {(void)model->addStop();}
    void removeStop() {(void)model->removeStop();}
    void apply() {(void)model->apply();}
    std::shared_ptr<dreamscapes::editor::BackgroundPanelModel> model;
    const std::vector<std::string> &modeNames,&fillNames,&presetNames,&swatchNames,&imageFits;
    ii::core::Observable<std::vector<std::shared_ptr<dreamscapes::editor::BackgroundChannel>>> &channels,&allChannels;
    ii::core::Observable<std::vector<std::string>> &stopNames,&recentNames;
    ii::core::Observable<int> &modeIndex,&fillIndex,&stopIndex,&imageFit;
    ii::core::Observable<bool> &canApply,&hasPlane,&gradient,&perceptual,&canRemoveStop,&replaceMode,&canChooseImage,&canApplyImage,&busy;
    ii::core::Observable<double> &alpha,&opacity,&angle,&stopPosition,&width,&height,&contentWidth,&planeWidth,&railWidth,&planeHeight,&channelListHeight,&allChannelListHeight,&channelSliderWidth;
    ii::core::Observable<std::string> &hex,&status,&availability,&alphaText,&opacityText,&angleText,&stopPositionText,&imageName;
    std::shared_ptr<ii::gui::NativeViewSource> planeView,railView;
};
