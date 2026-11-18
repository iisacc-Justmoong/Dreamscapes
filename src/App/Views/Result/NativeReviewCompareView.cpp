#include "NativeReviewCompareView.h"
#include "NativeReviewGallery.h"
#include <LVUI>
#include <iiGUI>
#include <algorithm>
#include <cmath>
#include <optional>

namespace dreamscapes::review {
namespace {
struct Binding final {
    std::weak_ptr<CompareModel> model;
    CompareModel::Item item;
    ii::gui::Widget surface;
    std::shared_ptr<lvui::native::Bindings> images;
    ii::core::ObservableSubscription image,transform,width,height;
    std::optional<std::uint64_t> pointer;
    double lastX=0,lastY=0;
};
std::shared_ptr<const ii::gui::WidgetDrawing> draw(const CompareItem &item,ii::gui::Size size) {
    auto drawing=std::make_shared<ii::gui::WidgetDrawing>();const auto image=item.image.get();
    if (!image || !image->width() || !image->height() || size.width<=0 || size.height<=0) return drawing;
    const auto t=item.transform.get();const double w=image->width()*t.scale,h=image->height()*t.scale;
    const double left=size.width/2-t.centerX*w,top=size.height/2-t.centerY*h;
    const double x1=std::max(0.0,left),y1=std::max(0.0,top),x2=std::min(size.width,left+w),y2=std::min(size.height,top+h);
    if (x2>x1 && y2>y1) drawing->images.push_back({.bounds={float(x1),float(y1),float(x2-x1),float(y2-y1)},
        .image=image,.source={float((x1-left)/w),float((y1-top)/h),float((x2-x1)/w),float((y2-y1)/h)}});
    return drawing;
}
}
std::shared_ptr<ii::gui::NativeViewSource> comparePaneSource(std::weak_ptr<CompareModel> model,
        std::weak_ptr<ReviewGalleryModel> gallery,const CompareModel::Item &item) {
    const std::weak_ptr<CompareItem> weakItem=item;
    return ii::gui::compiled_view_source([model,gallery,weakItem](ii::gui::WidgetBuilder &builder)->ii::core::Result<void> {
        auto owner=model.lock();auto value=weakItem.lock();
        if (!owner || !value) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Comparison pane is no longer available");
        auto surface=builder.absolute_stack({},"comparePane",{owner->cellWidth.get(),owner->paneHeight.get()});
        if (!surface) return std::unexpected(surface.error());
        auto binding=std::make_shared<Binding>();binding->model=model;binding->item=value;binding->surface=*surface;
        const std::weak_ptr<Binding> weak=binding;
        if (auto result=surface->set_paint_handler([weak](const auto &state)->ii::core::Result<std::shared_ptr<const ii::gui::WidgetDrawing>> {
            if (auto p=weak.lock()) return draw(*p->item,state.size);return std::make_shared<const ii::gui::WidgetDrawing>();
        });!result) return result;
        if (auto result=surface->set_input_handler([weak](const ii::gui::WidgetInput &event)->ii::core::Result<bool> {
            auto p=weak.lock();auto owner=p?p->model.lock():nullptr;if (!owner || !owner->visible.get()) return false;
            using Kind=ii::gui::WidgetInputKind;
            if (event.kind==Kind::focus_lost || event.kind==Kind::pointer_cancel) {const bool captured=p->pointer.has_value();p->pointer.reset();return captured;}
            if (event.kind==Kind::scroll && std::isfinite(event.scroll_y) && event.scroll_y!=0) {
                (void)owner->zoom(p->item,event.scroll_y>0?1.1:1.0/1.1);return true;
            }
            if (event.kind==Kind::pointer_down && (!event.buttons || (event.buttons&1)) && event.local_position) {
                owner->activate(p->item);p->pointer=event.device_id;p->lastX=event.local_position->x;p->lastY=event.local_position->y;return true;
            }
            if (!p->pointer || *p->pointer!=event.device_id || !event.local_position ||
                (event.kind!=Kind::pointer_move && event.kind!=Kind::pointer_up)) return false;
            const auto x=event.local_position->x,y=event.local_position->y;
            if (std::isfinite(x) && std::isfinite(y)) (void)owner->pan(p->item,x-p->lastX,y-p->lastY);
            p->lastX=x;p->lastY=y;if (event.kind==Kind::pointer_up) p->pointer.reset();return true;
        });!result) return result;
        const auto repaint=[weak](const auto &,std::uint64_t) {if (auto p=weak.lock()) (void)p->surface.request_repaint();};
        binding->image=value->image.observe(repaint);binding->transform=value->transform.observe(repaint);
        const auto resize=[weak](const auto &,std::uint64_t) {if (auto p=weak.lock()) if (auto c=p->model.lock())
            (void)p->surface.set_extent({c->cellWidth.get(),c->paneHeight.get()});};
        binding->width=owner->cellWidth.observe(resize);binding->height=owner->paneHeight.observe(resize);
        binding->images=std::make_shared<lvui::native::Bindings>();
        auto effect=binding->images->bind([model,gallery,weakItem](lvui::native::ReadContext &context) {
            auto c=model.lock();auto g=gallery.lock();auto item=weakItem.lock();
            if (!c || !g || !g->active() || !item) return ReviewGalleryModel::ResolvedPreview{{},"Preview unavailable"};
            const auto w=std::uint32_t(std::clamp(std::ceil(context.read(c->cellWidth)),1.0,8192.0));
            const auto h=std::uint32_t(std::clamp(std::ceil(context.read(c->paneHeight)),1.0,8192.0));
            return lvui::native::Methods::evaluate(context,[&]{return g->previewForSource(item->source,w,h);});
        },[model,weakItem](const ReviewGalleryModel::ResolvedPreview &next)->ii::core::Result<void> {
            if (auto c=model.lock()) if (auto item=weakItem.lock()) (void)c->publishImage(item,next.image,next.status);return {};
        });
        if (!effect) return std::unexpected(effect.error());
        if (auto retained=builder.retain(binding);!retained) return retained;
        return builder.on_retire([weak]{if (auto p=weak.lock()) {(void)p->images->close();p->pointer.reset();}});
    });
}
}
