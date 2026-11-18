#include "NativeShapeTool.h"
#include "App/ViewModels/Native/ViewValue.h"
#include <iiColorManager>
#include <iiJson>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace dreamscapes::editor {
namespace {
using ii::core::ErrorCode;
using namespace iiSharedCanvas;
std::optional<std::uint32_t> color(std::string_view value) {
    const auto parsed=iiColorManager::parseHexValue(std::string(value));if (!parsed) return {};
    const auto byte=[](double channel){return static_cast<std::uint32_t>(std::lround(std::clamp(channel,0.0,1.0)*255));};
    return (byte(parsed->a)<<24)|(byte(parsed->r)<<16)|(byte(parsed->g)<<8)|byte(parsed->b);
}
std::vector<VectorPath> geometry(ShapeKind kind,Point a,Point b,const ShapeDefaults &settings) {
    const double left=std::min(a.x,b.x),right=std::max(a.x,b.x),top=std::min(a.y,b.y),bottom=std::max(a.y,b.y);
    const double w=right-left,h=bottom-top,cx=(left+right)/2,cy=(top+bottom)/2;
    VectorPath path;
    if (kind!=ShapeKind::line) path.fill=SolidPaint{settings.fill};
    if (settings.strokeWidth>0) path.stroke=StrokeStyle{SolidPaint{settings.stroke},settings.strokeWidth};
    constexpr double bezier=.5522847498307936;
    if (kind==ShapeKind::rectangle) {
        const double r=std::min({settings.radius*(1+settings.smoothing/200),w/2,h/2});
        if (r<=0) path.commands={MoveTo{{left,top}},LineTo{{right,top}},LineTo{{right,bottom}},LineTo{{left,bottom}},ClosePath{}};
        else {
            const double k=r*bezier;
            path.commands={MoveTo{{left+r,top}},LineTo{{right-r,top}},CubicTo{{right-r+k,top},{right,top+r-k},{right,top+r}},
                LineTo{{right,bottom-r}},CubicTo{{right,bottom-r+k},{right-r+k,bottom},{right-r,bottom}},
                LineTo{{left+r,bottom}},CubicTo{{left+r-k,bottom},{left,bottom-r+k},{left,bottom-r}},
                LineTo{{left,top+r}},CubicTo{{left,top+r-k},{left+r-k,top},{left+r,top}},ClosePath{}};
        }
    } else if (kind==ShapeKind::ellipse) {
        const double x=w/2,y=h/2,kx=x*bezier,ky=y*bezier;
        path.commands={MoveTo{{cx+x,cy}},CubicTo{{cx+x,cy+ky},{cx+kx,cy+y},{cx,cy+y}},
            CubicTo{{cx-kx,cy+y},{cx-x,cy+ky},{cx-x,cy}},CubicTo{{cx-x,cy-ky},{cx-kx,cy-y},{cx,cy-y}},
            CubicTo{{cx+kx,cy-y},{cx+x,cy-ky},{cx+x,cy}},ClosePath{}};
    } else if (kind==ShapeKind::polygon) {
        const int count=settings.sides*(settings.star?2:1);
        for (int i=0;i<count;++i) {
            const auto angle=double(i)*2*std::numbers::pi/double(count)-std::numbers::pi/2;
            const double factor=settings.star && i%2?.5:1;
            const Point p{cx+std::cos(angle)*w*.5*factor,cy+std::sin(angle)*h*.5*factor};
            if (i==0) path.commands.emplace_back(MoveTo{p});else path.commands.emplace_back(LineTo{p});
        }
        path.commands.emplace_back(ClosePath{});
    } else path.commands={MoveTo{a},LineTo{b}};
    std::vector<VectorPath> result;result.push_back(std::move(path));
    if (kind==ShapeKind::line && settings.endArrow && settings.strokeWidth>0) {
        const double angle=std::atan2(b.y-a.y,b.x-a.x),size=std::max(4.0,settings.strokeWidth*3);
        VectorPath arrow;arrow.fill=SolidPaint{settings.stroke};
        arrow.commands={MoveTo{b},LineTo{{b.x-std::cos(angle-.5)*size,b.y-std::sin(angle-.5)*size}},
            LineTo{{b.x-std::cos(angle+.5)*size,b.y-std::sin(angle+.5)*size}},ClosePath{}};
        result.push_back(std::move(arrow));
    }
    return result;
}
std::string label(ShapeKind kind) {
    switch (kind) {case ShapeKind::rectangle:return "Rectangle";case ShapeKind::ellipse:return "Ellipse";
        case ShapeKind::polygon:return "Polygon";case ShapeKind::line:return "Line / Arrow";}
    return {};
}
}
ii::core::Result<ShapeDefaults> parseShapeDefaults(std::string_view source) {
    const auto parsed=ii::json::parse(source);
    if (!parsed) return ii::core::fail(ErrorCode::invalid_argument,"Cannot read Elements defaults");
    const auto &tools=vm::array(*parsed);
    const auto found=std::ranges::find_if(tools,[](const auto &item){return vm::text(vm::get(item,"key"))=="elements";});
    if (found==tools.end()) return ii::core::fail(ErrorCode::invalid_argument,"Elements defaults are missing");
    const auto &fields=vm::array(vm::get(*found,"fields"));
    auto initial=[&](std::string_view key)->const vm::Value & {
        const auto item=std::ranges::find_if(fields,[&](const auto &entry){return vm::text(vm::get(entry,"id"))==key;});
        return item==fields.end()?vm::get(vm::Value{},""):vm::get(*item,"initial");
    };
    const auto fill=color(vm::text(initial("field-16"))),stroke=color(vm::text(initial("field-17")));
    const auto radius=vm::number(initial("field-1"),-1),smooth=vm::number(initial("field-8"),-1);
    const auto width=vm::number(initial("field-10"),-1),sides=vm::number(initial("field-7"),-1);
    const auto &star=initial("field-9");
    const auto selector=vm::text(vm::get(vm::get(*found,"selector"),"initial"));
    const auto arrow=vm::text(initial("field-13"));
    const auto fillMode=vm::text(initial("field-2")),start=vm::text(initial("field-12"));
    const auto sweep=vm::number(initial("field-4"),-1),inner=vm::number(initial("field-5"),-1);
    ShapeKind initialKind;
    if (selector=="Rectangle") initialKind=ShapeKind::rectangle;
    else if (selector=="Ellipse") initialKind=ShapeKind::ellipse;
    else if (selector=="Polygon") initialKind=ShapeKind::polygon;
    else if (selector=="Line / Arrow") initialKind=ShapeKind::line;
    else return ii::core::fail(ErrorCode::invalid_argument,"Unknown initial Elements shape");
    if (!fill || !stroke || !std::isfinite(radius) || radius<0 || radius>256 || !std::isfinite(smooth) || smooth<0 || smooth>100 ||
        !std::isfinite(width) || width<0 || width>256 || !std::isfinite(sides) || std::floor(sides)!=sides || sides<3 || sides>64 ||
        star.kind()!=ii::core::ValueKind::boolean || (arrow!="Arrow" && arrow!="None") ||
        fillMode!="Solid" || start!="None" || sweep!=360 || inner!=0)
        return ii::core::fail(ErrorCode::invalid_argument,"Elements defaults are invalid");
    return ShapeDefaults{.fill=*fill,.stroke=*stroke,.strokeWidth=width,.radius=radius,.smoothing=smooth,
        .sides=static_cast<int>(sides),.star=vm::boolean(star),.endArrow=arrow=="Arrow",.initial=initialKind};
}
ShapeToolModel::ShapeToolModel(std::shared_ptr<native::DocumentSession> document,ShapeDefaults defaults)
    :document_(std::move(document)),defaults_(defaults) {
    if (!document_ || !std::isfinite(defaults_.strokeWidth) || defaults_.strokeWidth<0 || defaults_.strokeWidth>256 ||
        !std::isfinite(defaults_.radius) || defaults_.radius<0 || defaults_.radius>256 ||
        !std::isfinite(defaults_.smoothing) || defaults_.smoothing<0 || defaults_.smoothing>100 ||
        defaults_.sides<3 || defaults_.sides>64) throw std::invalid_argument("Invalid shape tool configuration");
}
std::shared_ptr<ShapeToolModel> ShapeToolModel::create(std::shared_ptr<native::DocumentSession> document,ShapeDefaults defaults) {
    auto value=std::shared_ptr<ShapeToolModel>(new ShapeToolModel(std::move(document),defaults));value->start();return value;
}
void ShapeToolModel::start() {
    if (auto selected=choose(defaults_.initial);!selected) throw std::invalid_argument(selected.error().message);
    const std::weak_ptr<ShapeToolModel> weak=shared_from_this();
    generationObserver_=document_->documentGeneration.observe([weak](const auto &,std::uint64_t){
        if (auto self=weak.lock()) {self->cancel();self->frame_.reset();}
    });
    revisionObserver_=document_->revision.observe([weak](auto revision,std::uint64_t){
        if (auto self=weak.lock();self && self->gesture_ && self->gesture_->revision!=revision) self->cancel();
    });
}
ii::core::Result<void> ShapeToolModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Shape tool requires its owner thread");
    return {};
}
ii::core::Result<void> ShapeToolModel::choose(ShapeKind value) {
    if (auto valid=writable();!valid) return valid;
    if (value!=ShapeKind::rectangle && value!=ShapeKind::ellipse && value!=ShapeKind::polygon && value!=ShapeKind::line)
        return ii::core::fail(ErrorCode::invalid_argument,"Unknown shape");
    cancel();kind_=value;
    (void)rectangle.set(value==ShapeKind::rectangle);(void)ellipse.set(value==ShapeKind::ellipse);
    (void)polygon.set(value==ShapeKind::polygon);(void)line.set(value==ShapeKind::line);(void)status.set({});
    (void)selecting.set(false);return {};
}
ii::core::Result<void> ShapeToolModel::selectTool() {
    if (auto valid=writable();!valid) return valid;
    cancel();(void)rectangle.set(false);(void)ellipse.set(false);(void)polygon.set(false);(void)line.set(false);
    (void)selecting.set(true);return {};
}
void ShapeToolModel::setVisible(bool value) {
    if (!writable()) return;visible_=value;if (!value) cancel();
}
void ShapeToolModel::setViewport(double w,double h) {
    if (!writable() || !std::isfinite(w) || !std::isfinite(h) || w<=0 || h<=0 || w>8192 || h>8192) return;
    if (w!=width.get() || h!=height.get()) cancel();
    (void)width.set(w);(void)height.set(h);
}
void ShapeToolModel::setFrame(std::shared_ptr<const ii::gui::RasterImage> value) {
    if (!writable()) return;
    if (!value || !frame_ || value->width()!=frame_->width() || value->height()!=frame_->height()) cancel();
    frame_=std::move(value);
}
void ShapeToolModel::cancel() {
    if (!writable()) return;
    gesture_.reset();(void)dragging.set(false);(void)preview.set({});
}
ii::core::Result<bool> ShapeToolModel::input(const ii::gui::WidgetInput &input) {
    if (auto valid=writable();!valid) return std::unexpected(valid.error());
    if (input.local_position && (!std::isfinite(input.local_position->x) || !std::isfinite(input.local_position->y)))
        return ii::core::fail(ErrorCode::invalid_argument,"Shape pointer coordinates must be finite");
    using Kind=ii::gui::WidgetInputKind;
    if (input.kind==Kind::focus_lost || (input.kind==Kind::key_down && input.key==ii::gui::WidgetKey::escape)
#if defined(IIGUI_POINTER_CANCEL_API)
        || input.kind==Kind::pointer_cancel
#endif
    ) {const bool consumed=gesture_.has_value();cancel();return consumed;}
    if (!visible_ || !frame_ || !document_->document()) return false;
    if (input.kind==Kind::pointer_down) {
        if (gesture_ || (input.buttons && !(input.buttons&1)) || !input.local_position) return false;
        const auto region=documentViewRegion(*document_->document());
        if (region.extent.width<=0 || region.extent.height<=0) return false;
        const auto scale=std::min(width.get()/frame_->width(),height.get()/frame_->height());
        const auto displayWidth=frame_->width()*scale,displayHeight=frame_->height()*scale;
        const auto left=(width.get()-displayWidth)/2,top=(height.get()-displayHeight)/2;
        const auto point=*input.local_position;
        if (!(scale>0) || point.x<left || point.y<top || point.x>left+displayWidth || point.y>top+displayHeight) return false;
        gesture_=Gesture{kind_,document_->documentGeneration.get(),document_->revision.get(),input.device_id,
            point.x,point.y,point.x,point.y,left,top,displayWidth,displayHeight,region};
        (void)status.set({});if (!gesture_) return true;
        (void)dragging.set(true);if (!gesture_) return true;
        (void)preview.set({true,point.x,point.y,point.x,point.y});return true;
    }
    if (!gesture_ || input.device_id!=gesture_->device || !input.local_position ||
        (input.kind!=Kind::pointer_move && input.kind!=Kind::pointer_up)) return false;
    auto &gesture=*gesture_;
    gesture.endX=std::clamp(input.local_position->x,gesture.left,gesture.left+gesture.displayWidth);
    gesture.endY=std::clamp(input.local_position->y,gesture.top,gesture.top+gesture.displayHeight);
    if (input.kind==Kind::pointer_move) {
        (void)preview.set({true,gesture.startX,gesture.startY,gesture.endX,gesture.endY});return true;
    }
    const auto final=gesture;cancel();
    if (final.generation!=document_->documentGeneration.get() || final.revision!=document_->revision.get()) return true;
    const auto project=[&](double x,double y) -> Point {
        return {final.region.origin.x+(x-final.left)*final.region.extent.width/final.displayWidth,
                final.region.origin.y+(y-final.top)*final.region.extent.height/final.displayHeight};
    };
    const auto a=project(final.startX,final.startY),b=project(final.endX,final.endY);
    if (final.kind==ShapeKind::line?std::hypot(b.x-a.x,b.y-a.y)<1e-6:
        std::abs(b.x-a.x)<1e-6 || std::abs(b.y-a.y)<1e-6) return true;
    if (final.kind==ShapeKind::line && defaults_.strokeWidth==0) {
        (void)status.set("A line requires a visible stroke");return true;
    }
    try {
        auto result=document_->insertVectorDrawing(label(final.kind),geometry(final.kind,a,b,defaults_));
        if (!result) (void)status.set(result.error().message);
        else if (visible_ && kind_==final.kind && !selecting.get()) (void)selectTool();
    } catch (...) {(void)status.set("Cannot prepare the shape");}
    return true;
}
}
