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
class MaskPanelModel;
struct MaskRow final {
    std::string id;
    ii::core::Observable<std::string> name;
    ii::core::Observable<bool> selected{false},enabled{true};
    std::weak_ptr<MaskPanelModel> owner;
    void select();
    void toggle(bool);
    void toggleCurrent();
};
class MaskPanelModel final : public std::enable_shared_from_this<MaskPanelModel> {
  public:
    static std::shared_ptr<MaskPanelModel> create(std::shared_ptr<native::DocumentSession>);
    void setVisible(bool);
    [[nodiscard]] ii::core::Result<void> select(std::string_view);
    [[nodiscard]] ii::core::Result<void> createMask();
    [[nodiscard]] ii::core::Result<void> duplicateSelected();
    [[nodiscard]] ii::core::Result<void> removeSelected();
    [[nodiscard]] ii::core::Result<void> setEnabled(bool);
    [[nodiscard]] ii::core::Result<void> setStackEnabled(bool);
    [[nodiscard]] ii::core::Result<void> selectRow(const MaskRow *);
    [[nodiscard]] ii::core::Result<void> setRowEnabled(const MaskRow *,bool);
    [[nodiscard]] ii::core::Result<void> setInverted(bool);
    [[nodiscard]] ii::core::Result<void> setDensity(double);
    [[nodiscard]] ii::core::Result<void> setFeather(double);
    [[nodiscard]] ii::core::Result<void> setCombine(iiSharedCanvas::MaskCombineMode);
    void sizeChanged(double,double,double,double,double);
    ii::core::Observable<std::vector<std::shared_ptr<MaskRow>>> rows;
    ii::core::Observable<bool> canAdd{false},canEdit{false},enabled{false},inverted{false},stackEnabled{false},legacyMasks{false},empty{true};
    ii::core::Observable<double> density{0},densityPercent{0},feather{0},width{lvui::SliderConfiguration{}.width},height{1},
        contentWidth{lvui::SliderConfiguration{}.width},listHeight{1},comboWidth{lvui::ComboBoxConfiguration{}.width};
    ii::core::Observable<std::string> selectedMask,status,layerName,combine;
  private:
    explicit MaskPanelModel(std::shared_ptr<native::DocumentSession>);
    void start();
    void synchronize();
    [[nodiscard]] ii::core::Result<void> writable() const;
    [[nodiscard]] ii::core::Result<void> update(const native::MaskPatch &);
    void updateListHeight();
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    ii::core::ObservableSubscription selection_,revision_,generation_;
    std::string layer_;
    std::uint64_t layerGeneration_=0;
    double rowHeight_=1,maximumListHeight_=1;
    bool visible_=false,synchronizing_=false,pending_=false;
};
}
