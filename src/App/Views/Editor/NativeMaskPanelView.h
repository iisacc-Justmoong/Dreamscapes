#pragma once
#include "NativeMaskPanel.h"
#include <iiGUI>
#include <functional>
#include <utility>

namespace dreamscapes::editor {
[[nodiscard]] ii::core::Result<void> buildMaskPanel(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<MaskPanelModel>);
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> maskPanelSource(std::shared_ptr<MaskPanelModel>);
}
class NativeMaskPanelBackend final {
  public:
    explicit NativeMaskPanelBackend(std::shared_ptr<dreamscapes::editor::MaskPanelModel> value)
        : model(std::move(value)),rows(model->rows),canAdd(model->canAdd),canEdit(model->canEdit),canLasso(model->canLasso),lassoActive(model->lassoActive),
          stackEnabled(model->stackEnabled),legacyMasks(model->legacyMasks),empty(model->empty),colorAvailable(model->colorAvailable),replaceAvailable(model->replaceAvailable),
          inverted(model->inverted),density(model->densityPercent),feather(model->feather),
          width(model->width),height(model->height),contentWidth(model->contentWidth),listHeight(model->listHeight),comboWidth(model->comboWidth),
          layerName(model->layerName),status(model->status),combine(model->combine) {}
    [[nodiscard]] std::function<void(double,double)> sizeHandler(double inset,double rowHeight,double maximumListHeight);
    void createMask() {(void)model->createMask();}
    void showColor() {model->showColor();}
    void showReplace() {model->showReplace();}
    void toggleLasso() {(void)model->setLassoActive(!model->lassoActive.get());}
    void duplicateMask() {(void)model->duplicateSelected();}
    void removeMask() {(void)model->removeSelected();}
    void toggleInverted() {(void)model->setInverted(!model->inverted.get());}
    void setStackEnabled(bool value) {(void)model->setStackEnabled(value);}
    void setDensity(double percent) {(void)model->setDensity(percent/100);}
    void setFeather(double value) {(void)model->setFeather(value);}
    void setAdd() {(void)model->setCombine(iiSharedCanvas::MaskCombineMode::Add);}
    void setSubtract() {(void)model->setCombine(iiSharedCanvas::MaskCombineMode::Subtract);}
    void setIntersect() {(void)model->setCombine(iiSharedCanvas::MaskCombineMode::Intersect);}
    void setReplace() {(void)model->setCombine(iiSharedCanvas::MaskCombineMode::Replace);}
    void attachMenu(ii::gui::Widget,ii::gui::Widget);
    void openCombineMenu();
    std::shared_ptr<dreamscapes::editor::MaskPanelModel> model;
    ii::core::Observable<std::vector<std::shared_ptr<dreamscapes::editor::MaskRow>>> &rows;
    ii::core::Observable<bool> &canAdd,&canEdit,&canLasso,&lassoActive,&stackEnabled,&legacyMasks,&empty,&colorAvailable,&replaceAvailable,&inverted;
    ii::core::Observable<double> &density,&feather,&width,&height,&contentWidth,&listHeight,&comboWidth;
    ii::core::Observable<std::string> &layerName,&status,&combine;
  private:
    ii::gui::Widget menu_,anchor_;
};
