#pragma once
#include "NativeLayerPanel.h"
#include <iiGUI>

namespace dreamscapes::editor {
[[nodiscard]] ii::core::Result<void> buildLayerPanel(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<LayerPanelModel>);
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> layerPanelSource(std::shared_ptr<LayerPanelModel>);
}
class NativeLayerPanelBackend final {
  public:
    explicit NativeLayerPanelBackend(std::shared_ptr<dreamscapes::editor::LayerPanelModel> value)
        : model(std::move(value)),document(model->document()),hasDocument(document->hasDocument),hasSelection(model->hasSelection),canDuplicate(model->canDuplicate),
          layerMode(model->layerMode),informationMode(model->informationMode),histogramMode(model->histogramMode),
          histogramHasPlot(model->histogramHasPlot),histogramBusy(model->histogramBusy),
          histogramRgb(model->histogramRgb),histogramRed(model->histogramRed),histogramGreen(model->histogramGreen),histogramBlue(model->histogramBlue),
          histogramPlot(model->histogramPlot),histogramSummary(model->histogramSummary),histogramStatus(model->histogramStatus),
          width(model->width),height(model->height),contentWidth(model->contentWidth),fieldWidth(model->fieldWidth),
          blendWidth(model->blendWidth),opacity(model->opacity),
          selectedLabel(model->selectedLabel),opacityText(model->opacityText),blend(model->blend),status(model->status),
          viewTitle(model->viewTitle),blends(model->blends),information(model->information),layers(document->layers) {}
    [[nodiscard]] std::function<void(double,double)> sizeHandler(double inset);
    void editOpacity(std::string value) {(void)model->editOpacityText(std::move(value));}
    void commitOpacity() {(void)model->commitOpacity();}
    void moveOpacity(double value) {(void)model->moveOpacity(value);}
    void chooseBlend(std::string value) {(void)model->chooseBlend(value);}
    void duplicateSelected() {(void)model->duplicateSelected();}
    void createStaticBitmap() {(void)model->createLayer(iiSharedCanvas::LayerKind::StaticBitmap);}
    void createStaticVector() {(void)model->createLayer(iiSharedCanvas::LayerKind::StaticVector);}
    void createDynamicBitmap() {(void)model->createLayer(iiSharedCanvas::LayerKind::DynamicBitmap);}
    void createDynamicVector() {(void)model->createLayer(iiSharedCanvas::LayerKind::DynamicVector);}
    void removeSelected() {(void)model->removeSelected();}
    void showLayer() {(void)model->showLayer();}
    void showInformation() {(void)model->showInformation();}
    void showHistogram() {(void)model->showHistogram();}
    void selectRgb() {(void)model->selectHistogram(dreamscapes::editor::HistogramChannel::rgb);}
    void selectRed() {(void)model->selectHistogram(dreamscapes::editor::HistogramChannel::red);}
    void selectGreen() {(void)model->selectHistogram(dreamscapes::editor::HistogramChannel::green);}
    void selectBlue() {(void)model->selectHistogram(dreamscapes::editor::HistogramChannel::blue);}
    void attachMenu(ii::gui::Widget,ii::gui::Widget);
    void openBlendMenu();
    void attachNewMenu(ii::gui::Widget,ii::gui::Widget);
    void openNewMenu();
    std::shared_ptr<dreamscapes::editor::LayerPanelModel> model;
    std::shared_ptr<dreamscapes::native::DocumentSession> document;
    ii::core::Observable<bool> &hasDocument,&hasSelection,&canDuplicate,&layerMode,&informationMode,&histogramMode;
    ii::core::Observable<bool> &histogramHasPlot,&histogramBusy,&histogramRgb,&histogramRed,&histogramGreen,&histogramBlue;
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> &histogramPlot;
    ii::core::Observable<std::string> &histogramSummary,&histogramStatus;
    ii::core::Observable<double> &width,&height,&contentWidth,&fieldWidth,&blendWidth,&opacity;
    ii::core::Observable<std::string> &selectedLabel,&opacityText,&blend,&status,&viewTitle;
    ii::core::Observable<std::vector<dreamscapes::editor::LayerBlendOption>> &blends;
    ii::core::Observable<std::vector<dreamscapes::editor::LayerInformationRow>> &information;
    ii::core::Observable<std::vector<std::shared_ptr<dreamscapes::native::DocumentLayerRow>>> &layers;
  private:
    ii::gui::Widget menu_,anchor_,newMenu_,newAnchor_;
};
