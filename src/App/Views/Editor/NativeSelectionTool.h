#pragma once
#include "App/Native/NativeDocumentSession.h"
#include "NativeVectorGeometry.h"
#include <iiGUI>
#include <array>
#include <optional>

namespace dreamscapes::editor {
struct SelectionOverlay final {
    bool active=false;
    std::array<iiSharedCanvas::Point,4> corners{};
    double handleSize=0;
    friend bool operator==(const SelectionOverlay &,const SelectionOverlay &)=default;
};
// Transforms are previewed in view state and committed once on pointer release.
class SelectionToolModel final : public std::enable_shared_from_this<SelectionToolModel> {
  public:
    static std::shared_ptr<SelectionToolModel> create(std::shared_ptr<native::DocumentSession>);
    void setVisible(bool);
    void setViewport(double width,double height,double handleSize);
    void setFrame(std::shared_ptr<const ii::gui::RasterImage>);
    [[nodiscard]] ii::core::Result<bool> input(const ii::gui::WidgetInput &);
    void cancel();
    ii::core::Observable<SelectionOverlay> overlay;
    ii::core::Observable<bool> dragging{false};
    ii::core::Observable<std::string> status;
    ii::core::Observable<double> width{1},height{1};
  private:
    struct Mapping {double left,top,width,height;iiSharedCanvas::CanvasRegion region;};
    struct Gesture {
        std::string id;
        std::uint64_t generation,revision,device;
        std::int64_t frame;
        int handle; // -1 moves; 0...7 resize in clockwise order.
        iiSharedCanvas::Point start;
        Mapping mapping;
        VectorGeometry geometry;
        AffineTransform current;
    };
    explicit SelectionToolModel(std::shared_ptr<native::DocumentSession>);
    void start();
    [[nodiscard]] ii::core::Result<void> writable() const;
    [[nodiscard]] std::optional<Mapping> mapping() const;
    [[nodiscard]] std::optional<VectorGeometry> geometry(std::string_view) const;
    [[nodiscard]] SelectionOverlay project(const VectorGeometry &,const Mapping &) const;
    void synchronize();
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    std::shared_ptr<const ii::gui::RasterImage> frame_;
    std::optional<Gesture> gesture_;
    double handleSize_=1;
    bool visible_=false;
    ii::core::ObservableSubscription selected_,revision_,generation_,frameObserver_;
};
}
