#pragma once
#include "App/Native/NativeDocumentSession.h"
#include <iiGUI>
#include <optional>

namespace dreamscapes::editor {
enum class ShapeKind {rectangle,ellipse,polygon,line};
struct ShapeDefaults final {
    std::uint32_t fill=0,stroke=0;
    double strokeWidth=0,radius=0,smoothing=0;
    int sides=0;
    bool star=false,endArrow=false;
    ShapeKind initial=ShapeKind::rectangle;
};
[[nodiscard]] ii::core::Result<ShapeDefaults> parseShapeDefaults(std::string_view);
struct ShapePreview final {
    bool active=false;
    double x1=0,y1=0,x2=0,y2=0;
    bool operator==(const ShapePreview &) const = default;
};
class ShapeToolModel final : public std::enable_shared_from_this<ShapeToolModel> {
  public:
    static std::shared_ptr<ShapeToolModel> create(std::shared_ptr<native::DocumentSession>,ShapeDefaults);
    [[nodiscard]] ii::core::Result<void> choose(ShapeKind);
    [[nodiscard]] ii::core::Result<void> chooseCurrentShape() {return choose(kind_);}
    [[nodiscard]] ii::core::Result<void> selectTool();
    [[nodiscard]] ii::core::Result<bool> input(const ii::gui::WidgetInput &);
    void setVisible(bool);
    void setViewport(double,double);
    void setFrame(std::shared_ptr<const ii::gui::RasterImage>);
    void cancel();
    ii::core::Observable<bool> rectangle{true},ellipse{false},polygon{false},line{false},dragging{false},selecting{false};
    ii::core::Observable<ShapePreview> preview;
    ii::core::Observable<std::string> status;
    ii::core::Observable<double> width{1},height{1};
  private:
    struct Gesture final {
        ShapeKind kind;
        std::uint64_t generation,revision,device;
        double startX,startY,endX,endY,left,top,displayWidth,displayHeight;
        iiSharedCanvas::CanvasRegion region;
    };
    ShapeToolModel(std::shared_ptr<native::DocumentSession>,ShapeDefaults);
    void start();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    ShapeDefaults defaults_;
    ShapeKind kind_=ShapeKind::rectangle;
    std::shared_ptr<const ii::gui::RasterImage> frame_;
    std::optional<Gesture> gesture_;
    ii::core::ObservableSubscription generationObserver_,revisionObserver_;
    bool visible_=false;
};
}
