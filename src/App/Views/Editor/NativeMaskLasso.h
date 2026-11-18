#pragma once
#include "NativeMaskPanel.h"
#include <iiGUI>
#include <optional>

namespace dreamscapes::editor {
struct MaskLassoPreview final {
    bool active=false;
    std::vector<iiSharedCanvas::Point> points;
    friend bool operator==(const MaskLassoPreview &,const MaskLassoPreview &)=default;
};
class MaskLassoModel final : public std::enable_shared_from_this<MaskLassoModel> {
  public:
    static std::shared_ptr<MaskLassoModel> create(std::shared_ptr<native::DocumentSession>,std::shared_ptr<MaskPanelModel>);
    void setVisible(bool);
    void setViewport(double,double);
    void setFrame(std::shared_ptr<const ii::gui::RasterImage>);
    [[nodiscard]] ii::core::Result<bool> input(const ii::gui::WidgetInput &);
    void cancel();
    ii::core::Observable<MaskLassoPreview> preview;
    ii::core::Observable<std::string> status;
    ii::core::Observable<double> width{1},height{1};
  private:
    struct Gesture {
        std::string layer;
        std::uint64_t generation,revision,device;
        std::int64_t frame;
        double left,top,width,height;
        iiSharedCanvas::CanvasRegion region;
        std::vector<iiSharedCanvas::Point> points;
    };
    MaskLassoModel(std::shared_ptr<native::DocumentSession>,std::shared_ptr<MaskPanelModel>);
    void start();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    std::shared_ptr<MaskPanelModel> panel_;
    std::shared_ptr<const ii::gui::RasterImage> frame_;
    std::optional<Gesture> gesture_;
    ii::core::ObservableSubscription selected_,revision_,generation_,frameObserver_;
    bool visible_=false;
};
}
