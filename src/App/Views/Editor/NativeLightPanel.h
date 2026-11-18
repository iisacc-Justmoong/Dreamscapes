#pragma once
#include "NativeLightAdjustment.h"
#include "App/Native/NativeDocumentSession.h"
#include <Render/AsyncFrameRenderQueue.h>
#include <iiGUI>
#include <iiSDKCore>
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dreamscapes::editor {
class LightPanelModel;
struct LightField final {
    std::string key,label,range;
    double minimum=0,maximum=0,step=1;
    ii::core::Observable<double> value{0};
    ii::core::Observable<std::string> text{"0"};
    ii::core::Observable<bool> invalid{false};
    std::weak_ptr<LightPanelModel> owner;
    [[nodiscard]] ii::core::Result<void> setValue(double);
    void moved(double value) {(void)setValue(value);}
    void edited(std::string);
    void submitted();
  private:
    friend class LightPanelModel;
    bool pending_=false;
};
class LightPanelModel final : public std::enable_shared_from_this<LightPanelModel> {
  public:
    static std::shared_ptr<LightPanelModel> create(std::shared_ptr<native::DocumentSession>,ii::runtime::EventLoop &,
        std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>,std::string_view catalog);
    ~LightPanelModel();
    [[nodiscard]] std::shared_ptr<LightField> field(std::string_view) const;
    [[nodiscard]] ii::core::Result<void> requestAuto();
    [[nodiscard]] ii::core::Result<void> requestPreview();
    [[nodiscard]] ii::core::Result<void> apply();
    void reset();
    void setVisible(bool);
    void setViewport(double,double);
    void sizeChanged(double,double,double,double,double,double);
    void close();
    ii::core::Observable<std::vector<std::shared_ptr<LightField>>> fields;
    ii::core::Observable<bool> available{false},busy{false},canPreview{false},canApply{false};
    ii::core::Observable<std::string> status,availability,layerName;
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> previewImage;
    ii::core::Observable<double> width{1},height{1},contentWidth{1},sliderWidth{1},listHeight{1};
  private:
    friend struct LightField;
    struct Bridge;
    LightPanelModel(std::shared_ptr<native::DocumentSession>,ii::runtime::EventLoop::Sender,
        std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>,std::string_view);
    void start();
    [[nodiscard]] ii::core::Result<void> writable() const;
    [[nodiscard]] ii::core::Result<void> setField(LightField &,double);
    void editField(LightField &,std::string);
    void submitField(LightField &);
    void retirePrepared();
    void invalidate(bool resetValues);
    void updateAvailability();
    void deliver();
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    ii::runtime::EventLoop::Sender sender_;
    std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor_;
    std::shared_ptr<Bridge> bridge_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    ii::runtime::TaskToken wake_;
    ii::core::ObservableSubscription generation_,revision_,selection_,frame_;
    std::optional<PreparedLightAdjustment> prepared_;
    std::uint64_t epoch_=0,inFlight_=0;
    double viewportWidth_=1,viewportHeight_=1;
    bool active_=true,visible_=false;
};
}
