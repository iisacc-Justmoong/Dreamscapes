#pragma once
#include "App/Native/NativeDocumentSession.h"
#include <iiSDKCore/Core/Property/Observable.h>
#include <LVUI/ComboBox.h>
#include <LVUI/Slider.h>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dreamscapes::editor {
struct LayerBlendOption final {
    std::string key,label;
    bool operator==(const LayerBlendOption &) const = default;
};
struct LayerInformationRow final {
    std::string id,label,value;
    bool operator==(const LayerInformationRow &) const = default;
};
class LayerPanelModel final : public std::enable_shared_from_this<LayerPanelModel> {
  public:
    static std::shared_ptr<LayerPanelModel> create(std::shared_ptr<native::DocumentSession>);
    [[nodiscard]] ii::core::Result<void> editOpacityText(std::string);
    [[nodiscard]] ii::core::Result<void> commitOpacity();
    [[nodiscard]] ii::core::Result<void> moveOpacity(double);
    [[nodiscard]] ii::core::Result<void> chooseBlend(std::string_view);
    [[nodiscard]] ii::core::Result<void> createLayer(iiSharedCanvas::LayerKind);
    [[nodiscard]] ii::core::Result<void> duplicateSelected();
    [[nodiscard]] ii::core::Result<void> removeSelected();
    [[nodiscard]] ii::core::Result<void> showLayer();
    [[nodiscard]] ii::core::Result<void> showInformation();
    void sizeChanged(double,double,double horizontalInset);
    std::shared_ptr<native::DocumentSession> document() const {return document_;}
    ii::core::Observable<bool> hasSelection{false},canDuplicate{false},layerMode{true},informationMode{false};
    ii::core::Observable<double> width{lvui::SliderConfiguration{}.width},height{1},
        contentWidth{lvui::SliderConfiguration{}.width},fieldWidth{lvui::ComboBoxConfiguration{}.width},
        blendWidth{lvui::ComboBoxConfiguration{}.width},opacity{0};
    ii::core::Observable<std::string> selectedName,selectedLabel,opacityText,blend,status,viewTitle{"Layer"};
    ii::core::Observable<std::vector<LayerBlendOption>> blends;
    ii::core::Observable<std::vector<LayerInformationRow>> information;
  private:
    explicit LayerPanelModel(std::shared_ptr<native::DocumentSession>);
    void start();
    void synchronize();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    ii::core::ObservableSubscription selectionObserver_,revisionObserver_,generationObserver_,frameObserver_;
    std::string currentId_;
    double acceptedOpacity_=0;
    bool editing_=false,synchronizing_=false,pending_=false;
};
}
