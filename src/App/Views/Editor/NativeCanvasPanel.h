#pragma once
#include "App/Native/NativeDocumentSession.h"
#include <LVUI>
#include <iiSDKCore>
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::editor {
class CanvasPanelModel final : public std::enable_shared_from_this<CanvasPanelModel> {
  public:
    static std::shared_ptr<CanvasPanelModel> create(std::shared_ptr<native::DocumentSession>);
    void setVisible(bool);
    [[nodiscard]] ii::core::Result<void> editWidth(std::string);
    [[nodiscard]] ii::core::Result<void> editHeight(std::string);
    [[nodiscard]] ii::core::Result<void> apply();
    [[nodiscard]] ii::core::Result<void> reset();
    [[nodiscard]] ii::core::Result<void> showSize();
    [[nodiscard]] ii::core::Result<void> showRotate();
    [[nodiscard]] ii::core::Result<void> showMirror();
    [[nodiscard]] ii::core::Result<void> chooseLeft();
    [[nodiscard]] ii::core::Result<void> chooseHalf();
    [[nodiscard]] ii::core::Result<void> chooseRight();
    [[nodiscard]] ii::core::Result<void> editAngle(std::string);
    [[nodiscard]] ii::core::Result<void> commitAngle();
    [[nodiscard]] ii::core::Result<void> moveAngle(double);
    [[nodiscard]] ii::core::Result<void> applyRotate();
    [[nodiscard]] ii::core::Result<void> chooseHorizontal();
    [[nodiscard]] ii::core::Result<void> chooseVertical();
    [[nodiscard]] ii::core::Result<void> applyMirror();
    void sizeChanged(double,double,double);
    ii::core::Observable<std::string> widthText,heightText,status,angleText{"0"};
    ii::core::Observable<bool> editable{false},sizeMode{true},rotateMode{false},mirrorMode{false},horizontal{true},vertical{false};
    ii::core::Observable<bool> left{true},half{false},right{false};
    ii::core::Observable<double> width{1},height{1},contentWidth{lvui::SliderConfiguration{}.width},fieldWidth{1},angle{0};
  private:
    enum class Mode {size,rotate,mirror};
    explicit CanvasPanelModel(std::shared_ptr<native::DocumentSession>);
    void start();
    void synchronize();
    [[nodiscard]] ii::core::Result<void> edit(ii::core::Observable<std::string> &,std::string);
    [[nodiscard]] ii::core::Result<void> selectMode(Mode);
    [[nodiscard]] ii::core::Result<void> selectAxis(bool);
    [[nodiscard]] ii::core::Result<void> selectPreset(int);
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    ii::core::ObservableSubscription revisionObserver_,generationObserver_;
    std::uint64_t generation_=0;
    std::int32_t acceptedWidth_=0,acceptedHeight_=0;
    bool visible_=false,editing_=false,synchronizing_=false,pending_=false,horizontalSelected_=true;
    Mode mode_=Mode::size;
    int preset_=-90;
};
}
